/* ATA PIO (primary master, LBA28, polling) */
#include "hw.h"
#ifdef YORO_SIM
#include <stdio.h>
#endif

#define ATA_DATA 0x1F0
#define ATA_ERR 0x1F1
#define ATA_SEC 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_CMD 0x1F7
#define ATA_CTRL 0x3F6

#define DISK_SECTORS 32768 /* 16 МБ образ */

#ifdef YORO_SIM
static u8 sim_disk[DISK_SECTORS * 512];
int ata_read(u32 lba, u32 count, void *buf) {
    if (lba + count > DISK_SECTORS) return 0;
    memcpy(buf, sim_disk + lba * 512, count * 512);
    return 1;
}
int ata_write(u32 lba, u32 count, const void *buf) {
    if (lba + count > DISK_SECTORS) return 0;
    memcpy(sim_disk + lba * 512, buf, count * 512);
    return 1;
}
void ata_sim_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fread(sim_disk, 1, sizeof(sim_disk), f); fclose(f); }
}
void ata_sim_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(sim_disk, 1, sizeof(sim_disk), f); fclose(f); }
}
#else
static int ata_wait(u32 want_drq) {
    int t = 2000000;
    while (t--) {
        u8 st = inb(ATA_STATUS);
        if (st & 0x80) continue;      /* BSY */
        if (want_drq) return (st & 0x08) ? 0 : -1;
        return (st & 1) ? -1 : 0;     /* ERR */
    }
    return -1;
}

int ata_read(u32 lba, u32 count, void *buf) {
    u8 *p = buf;
    for (u32 s = 0; s < count; s++, lba++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SEC, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_CMD, 0x20);
        if (ata_wait(1)) return 0;
        insw(ATA_DATA, p, 256);
        p += 512;
        io_wait();
    }
    return 1;
}

int ata_write(u32 lba, u32 count, const void *buf) {
    const u8 *p = buf;
    for (u32 s = 0; s < count; s++, lba++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SEC, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_CMD, 0x30);
        if (ata_wait(1)) return 0;
        outsw(ATA_DATA, p, 256);
        p += 512;
        outb(ATA_CTRL, 0x08); /* сброс кэша */
        if (ata_wait(0)) return 0;
    }
    return 1;
}
#endif
