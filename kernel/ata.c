/* YoroOS 0.2 - ATA PIO disk driver (LBA28, primary/secondary, master/slave) */
#include "kernel.h"

static inline void outb_p(u16 port, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port)); }
static inline u8 inb_p(u16 port) { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline void outw_p(u16 port, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(port)); }
static inline u16 inw_p(u16 port) { u16 r; __asm__ volatile("inw %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline void io_delay(void) { outb_p(0x80, 0); }

#define ATA_REG_DATA   0
#define ATA_REG_SECCNT 2
#define ATA_REG_LBA0   3
#define ATA_REG_LBA1   4
#define ATA_REG_LBA2   5
#define ATA_REG_DRIVE  6
#define ATA_REG_STATUS 7
#define ATA_REG_CMD    7

#define ATA_SR_BSY 0x80
#define ATA_SR_DF  0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static const u16 io_base[2] = { 0x1F0, 0x170 };
static const u16 ctrl_base[2] = { 0x3F6, 0x376 };

static u8  present[4];
static u8  is_atapi[4];
static char model[4][41];
static u32 sectors[4];

static int chan_of(int d) { return d / 2; }
static int slave_of(int d) { return d % 2; }

static int wait_bsy(int d, int loops) {
    u16 io = io_base[chan_of(d)];
    int t = loops;
    while (t-- > 0) {
        u8 st = inb_p(io + ATA_REG_STATUS);
        if (st == 0xFF) return -1;
        if (!(st & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int wait_drq_n(int d, int t) {
    u16 io = io_base[chan_of(d)];
    while (t-- > 0) {
        u8 st = inb_p(io + ATA_REG_STATUS);
        if (st == 0 || st == 0xFF || (st & (ATA_SR_ERR | ATA_SR_DF))) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static int wait_drq(int d) { return wait_drq_n(d, 500000); }

static void ata_probe(int d) {
    u16 io = io_base[chan_of(d)];
    u16 ctrl = ctrl_base[chan_of(d)];
    u8 cl = (u8)(0xA0 | (slave_of(d) << 4));
    u16 buf[256];
    u8 lba1, lba2;
    int i;

    present[d] = 0;
    is_atapi[d] = 0;
    outb_p(ctrl, 0x04);
    for (i = 0; i < 8; i++) io_delay();
    outb_p(ctrl, 0x00);
    if (wait_bsy(d, 30000) < 0) return;

    outb_p(io + ATA_REG_DRIVE, cl);
    for (i = 0; i < 8; i++) io_delay();
    outb_p(io + ATA_REG_SECCNT, 0);
    outb_p(io + ATA_REG_LBA0, 0);
    outb_p(io + ATA_REG_LBA1, 0);
    outb_p(io + ATA_REG_LBA2, 0);
    outb_p(io + ATA_REG_CMD, 0xEC);
    io_delay();

    {
        u8 st0 = inb_p(io + ATA_REG_STATUS);
        if (st0 == 0 || st0 == 0xFF) return;
    }
    if (wait_bsy(d, 30000) < 0) return;
    lba1 = inb_p(io + ATA_REG_LBA1);
    lba2 = inb_p(io + ATA_REG_LBA2);
    if (lba1 == 0x14 && lba2 == 0xEB) {
        is_atapi[d] = 1;
        klog("[atapi] optical drive on channel %d.%d", chan_of(d), slave_of(d));
        return;
    }
    if (lba1 != 0 || lba2 != 0) return;
    if (wait_drq_n(d, 12000) < 0) return;

    for (i = 0; i < 256; i++) buf[i] = inw_p(io + ATA_REG_DATA);
    for (i = 0; i < 20; i++) {
        model[d][i * 2] = (char)(buf[27 + i] >> 8);
        model[d][i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
    }
    model[d][40] = 0;
    for (i = 39; i > 0 && model[d][i] == ' '; i--) model[d][i] = 0;
    sectors[d] = ((u32)buf[61] << 16) | buf[60];
    if (!sectors[d]) {
        u32 cyl = buf[1], heads = buf[3], spt = buf[6];
        if (cyl && heads && spt) sectors[d] = cyl * heads * spt;
    }
    present[d] = 1;
}

void ata_init(void) {
    int d;
    for (d = 0; d < 4; d++) {
        ata_probe(d);
        if (present[d])
            klog("[ata] disk %d: %s (%d MB)", d, model[d], (int)(sectors[d] / 2048));
    }
}

int ata_disk_count(void) {
    int d, n = 0;
    for (d = 0; d < 4; d++) if (present[d]) n++;
    return n;
}

int ata_disk_index(int n) {
    int d, k = 0;
    for (d = 0; d < 4; d++) {
        if (!present[d]) continue;
        if (k == n) return d;
        k++;
    }
    return -1;
}

const char *ata_disk_model(int d) { return (d >= 0 && d < 4 && present[d]) ? model[d] : ""; }
u32 ata_disk_sectors(int d) { return (d >= 0 && d < 4 && present[d]) ? sectors[d] : 0; }

int ata_rw(int d, u32 lba, u32 count, u8 *buf, int write) {
    u16 io = io_base[chan_of(d)];
    u32 i;
    if (d < 0 || d > 3 || !present[d]) return -1;
    if (lba + count > sectors[d]) return -1;
    while (count) {
        u32 chunk = count > 128 ? 128 : count;
        if (wait_bsy(d, 400000) < 0) return -1;
        outb_p(io + ATA_REG_DRIVE, (u8)(0xE0 | (slave_of(d) << 4) | ((lba >> 24) & 0x0F)));
        outb_p(io + ATA_REG_SECCNT, (u8)chunk);
        outb_p(io + ATA_REG_LBA0, (u8)(lba & 0xFF));
        outb_p(io + ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
        outb_p(io + ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
        outb_p(io + ATA_REG_CMD, write ? 0x30 : 0x20);
        for (i = 0; i < chunk; i++) {
            int w;
            if (wait_drq(d) < 0) return -1;
            if (write) {
                const u16 *p = (const u16 *)(buf + i * 512);
                for (w = 0; w < 256; w++) outw_p(io + ATA_REG_DATA, p[w]);
            } else {
                u16 *p = (u16 *)(buf + i * 512);
                for (w = 0; w < 256; w++) p[w] = inw_p(io + ATA_REG_DATA);
            }
        }
        if (write) {
            wait_bsy(d, 400000);
            outb_p(io + ATA_REG_CMD, 0xE7);
            wait_bsy(d, 400000);
        }
        buf += chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static int atapi_read2048(int d, u32 lba2048, u8 *out2048) {
    u16 io = io_base[chan_of(d)];
    u8 pkt[12];
    const u16 *pw = (const u16 *)pkt;
    u16 *dst = (u16 *)out2048;
    int i;

    if (d < 0 || d > 3 || !is_atapi[d]) return -1;
    if (wait_bsy(d, 200000) < 0) return -1;
    outb_p(io + ATA_REG_DRIVE, (u8)(0xA0 | (slave_of(d) << 4)));
    for (i = 0; i < 4; i++) io_delay();
    outb_p(io + 1, 0x00);               /* PIO mode (no DMA) */
    outb_p(io + ATA_REG_LBA1, 2048 & 0xFF);
    outb_p(io + ATA_REG_LBA2, (2048 >> 8) & 0xFF);
    outb_p(io + ATA_REG_CMD, 0xA0);     /* ATAPI PACKET */
    if (wait_drq(d) < 0) return -1;

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x28;                      /* SCSI READ(10) */
    pkt[2] = (u8)(lba2048 >> 24);
    pkt[3] = (u8)(lba2048 >> 16);
    pkt[4] = (u8)(lba2048 >> 8);
    pkt[5] = (u8)(lba2048 & 0xFF);
    pkt[8] = 1;                         /* 1 x 2048-byte sector */
    for (i = 0; i < 6; i++) outw_p(io + ATA_REG_DATA, pw[i]);

    if (wait_drq(d) < 0) return -1;
    for (i = 0; i < 1024; i++) dst[i] = inw_p(io + ATA_REG_DATA);
    wait_bsy(d, 100000);
    return 0;
}

static u8  g_cd_cache[2048];
static u32 g_cd_cache_lba = 0xFFFFFFFFU;
static int g_cd_cache_drv = -1;

static int atapi_read512_drv(int d, u32 lba512, u32 count512, u8 *buf) {
    u32 i;
    for (i = 0; i < count512; i++) {
        u32 cur = lba512 + i;
        u32 blk = cur >> 2;
        u32 off = (cur & 3U) * 512U;
        if (g_cd_cache_drv != d || g_cd_cache_lba != blk) {
            if (atapi_read2048(d, blk, g_cd_cache) != 0) return -1;
            g_cd_cache_drv = d;
            g_cd_cache_lba = blk;
        }
        memcpy(buf + i * 512U, g_cd_cache + off, 512U);
    }
    return 0;
}

int atapi_boot_media_read(u32 lba512, u32 count512, u8 *buf) {
    int d;
    u8 probe[512];
    for (d = 0; d < 4; d++) {
        if (!is_atapi[d]) continue;
        if (atapi_read512_drv(d, 36, 1, probe) == 0 && memcmp(probe, "YOROISO2", 8) == 0)
            return atapi_read512_drv(d, lba512, count512, buf);
    }
    return -1;
}
