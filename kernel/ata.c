/* YoroOS - ATA PIO disk driver (LBA28, primary/secondary, master/slave) */
#include "kernel.h"

#define ATA_REG_DATA   0
#define ATA_REG_ERR    1
#define ATA_REG_SECCNT 2
#define ATA_REG_LBA0   3
#define ATA_REG_LBA1   4
#define ATA_REG_LBA2   5
#define ATA_REG_DRIVE  6
#define ATA_REG_STATUS 7
#define ATA_REG_CMD    7
#define ATA_REG_CTRL   0x206

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF  0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static const u16 io_base[2] = { 0x1F0, 0x170 };
static const u16 ctrl_base[2] = { 0x3F6, 0x376 };

static u8  present[4];
static char model[4][41];
static u32 sectors[4];

static int chan_of(int d) { return d / 2; }
static int slave_of(int d) { return d % 2; }

static int wait_bsy(int d, int loops) {
    u16 io = io_base[chan_of(d)];
    int t = loops;
    while (t-- > 0) {
        u8 st = inb(io + ATA_REG_STATUS);
        if (st == 0xFF) return -1;          /* floating bus: no device */
        if (!(st & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(int d) {
    u16 io = io_base[chan_of(d)];
    int t = 2000000;
    while (t-- > 0) {
        u8 st = inb(io + ATA_REG_STATUS);
        if (st == 0xFF) return -1;
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DF) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static u16 identify_word(const u16 *id, int word) { return id[word]; }

static void ata_probe(int d) {
    u16 io = io_base[chan_of(d)];
    u16 ctrl = ctrl_base[chan_of(d)];
    u8 cl = (u8)(0xA0 | (slave_of(d) << 4));
    u16 buf[256];
    int i;

    present[d] = 0;
    outb(ctrl, 0x04);                 /* soft reset */
    for (i = 0; i < 8; i++) io_wait();
    outb(ctrl, 0x00);
    if (wait_bsy(d, 200000) < 0) return;

    outb(io + ATA_REG_DRIVE, cl);
    for (i = 0; i < 8; i++) io_wait();
    outb(io + ATA_REG_SECCNT, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_CMD, 0xEC);     /* IDENTIFY */
    io_wait();

    {
        u8 st0 = inb(io + ATA_REG_STATUS);
        if (st0 == 0 || st0 == 0xFF) return;     /* nothing here */
    }
    if (wait_bsy(d, 200000) < 0) return;
    if (inb(io + ATA_REG_LBA1) != 0 || inb(io + ATA_REG_LBA2) != 0) return;  /* ATAPI */
    if (wait_drq(d) < 0) return;

    for (i = 0; i < 256; i++) buf[i] = inw(io + ATA_REG_DATA);
    /* the model string is stored as byte-swapped ASCII, numeric fields are not */
    for (i = 0; i < 20; i++) {
        model[d][i * 2] = (char)(buf[27 + i] >> 8);
        model[d][i * 2 + 1] = (char)(buf[27 + i] & 0xFF);
    }
    model[d][40] = 0;
    for (i = 39; i > 0 && model[d][i] == ' '; i--) model[d][i] = 0;
    sectors[d] = ((u32)identify_word(buf, 61) << 16) | identify_word(buf, 60);
    if (!sectors[d]) {
        /* fall back to the CHS geometry reported in words 1..3 */
        u32 cyl = identify_word(buf, 1), heads = identify_word(buf, 3), spt = identify_word(buf, 6);
        if (cyl && heads && spt) sectors[d] = cyl * heads * spt;
    }
    present[d] = 1;
    klog("[ata] id sectors=%d", (int)sectors[d]);
}

void ata_init(void) {
    int d;
    for (d = 0; d < 4; d++) {
        ata_probe(d);
        if (present[d])
            klog("[ata] disk %d: %s (%d MB)", d, model[d], (int)(sectors[d] / 2048));
    }
    if (!ata_disk_count()) klog("[ata] no ATA disks found");
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

const char *ata_disk_model(int d) {
    if (d < 0 || d > 3 || !present[d]) return "";
    return model[d];
}

u32 ata_disk_sectors(int d) {
    if (d < 0 || d > 3 || !present[d]) return 0;
    return sectors[d];
}

static int ata_rw(int d, u32 lba, u32 count, u8 *buf, int write) {
    u16 io = io_base[chan_of(d)];
    u32 i;

    if (d < 0 || d > 3 || !present[d]) return -1;
    if (lba + count > sectors[d]) return -1;

    while (count) {
        u32 chunk = count > 128 ? 128 : count;   /* max 64 KiB per DRQ block */
        if (wait_bsy(d, 400000) < 0) return -1;
        outb(io + ATA_REG_DRIVE, (u8)(0xE0 | (slave_of(d) << 4) | ((lba >> 24) & 0x0F)));
        outb(io + ATA_REG_SECCNT, (u8)chunk);
        outb(io + ATA_REG_LBA0, (u8)(lba & 0xFF));
        outb(io + ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
        outb(io + ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
        outb(io + ATA_REG_CMD, write ? 0x30 : 0x20);   /* WRITE / READ sectors */
        for (i = 0; i < chunk; i++) {
            int w;
            if (wait_drq(d) < 0) return -1;
            if (write) {
                u16 *p = (u16 *)(buf + i * 512);
                for (w = 0; w < 256; w++) outw(io + ATA_REG_DATA, p[w]);
            } else {
                u16 *p = (u16 *)(buf + i * 512);
                for (w = 0; w < 256; w++) p[w] = inw(io + ATA_REG_DATA);
            }
        }
        if (write) {
            /* flush the device write cache after each block */
            wait_bsy(d, 400000);
            outb(io + ATA_REG_CMD, 0xE7);
            wait_bsy(d, 400000);
        }
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int ata_read(int d, u32 lba, u32 count, u8 *buf) { return ata_rw(d, lba, count, buf, 0); }
int ata_write(int d, u32 lba, u32 count, const u8 *buf) { return ata_rw(d, lba, count, (u8 *)buf, 1); }

/* used by the installer: stream a memory buffer to disk */
int ata_write_buffer(int d, u32 lba, const u8 *data, u32 bytes) {
    u32 sectors_n = (bytes + 511) / 512;
    u8 *tmp = (u8 *)kmalloc(512);
    u32 i;
    if (!tmp) return -1;
    for (i = 0; i < sectors_n; i++) {
        u32 off = i * 512;
        u32 n = bytes - off < 512 ? bytes - off : 512;
        memset(tmp, 0, 512);
        memcpy(tmp, data + off, n);
        if (ata_write(d, lba + i, 1, tmp) < 0) { kfree(tmp); return -1; }
    }
    kfree(tmp);
    return 0;
}
