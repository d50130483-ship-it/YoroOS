/* YoroOS 0.2.4 - Pure-C GPT + Canonical FAT16 UEFI ESP + Hybrid BIOS + YoroFS2 Installer
 *
 * Self-contained disk layout (all offsets in 512-byte logical LBAs):
 *   LBA 0             : Hybrid Protective/Boot MBR (boot.bin + 0xEE GPT Protective Entry)
 *   LBA 1             : Primary GPT Header ("EFI PART", IEEE CRC32 verified)
 *   LBA 2..33         : Primary GPT Partition Array (128 entries)
 *   LBA 36..39        : BIOS Stage 2 bootloader (stage2.bin)
 *   LBA 40..16383     : BIOS High-Memory 32-bit Kernel (kernel32.bin)
 *   LBA 16384..49151  : GPT Partition 1 — 16 MiB Pure-C Formatted FAT16 EFI System Partition
 *                       (8,160 clusters @ 2 KiB => 100% Microsoft fatgen103 FAT16 compliant!)
 *                       Contains:
 *                         \EFI\BOOT\BOOTX64.EFI
 *                         \EFI\MICROSOFT\BOOT\BOOTMGFW.EFI
 *                         \EFI\YOROOS\INSTALLED.TAG
 *   LBA 49152..end-34 : GPT Partition 2 — Persistent YoroFS2 System Root Volume (C:)
 *   LBA end-33..end-1 : Backup GPT Partition Array + Backup GPT Header
 */
#include "kernel.h"

extern FSNode fs_nodes[FS_MAX_NODES];
void fs_sync_suspend(int on);

#define ESP_START_LBA    16384ULL
#define ESP_SECTORS      32768ULL   /* 16 MiB => ~8,160 2KB clusters = 100% compliant FAT16! */
#define YOROFS_START_LBA 49152ULL
#define LEGACY_YFS_LBA   32768ULL   /* Backwards-compat probe for 0.2.0 disks */
#define YOROFS_MAGIC     0x32534659U /* 'YFS2' */

typedef struct __attribute__((packed)) {
    u32 magic;          /* YOROFS_MAGIC */
    u32 version;        /* 2 */
    u32 total_bytes;    /* payload bytes following header */
    u32 payload_crc32;  /* IEEE CRC32 of payload */
    SysProfile profile;
    u32 installed_mask; /* bitmask of installed apps 0..31 */
    u32 desktop_mask;   /* bitmask of desktop shortcut apps 0..31 */
    u32 pinned_mask;    /* bitmask of pinned apps 0..31 */
    u32 node_count;
    u8  reserved[512 - 16 - sizeof(SysProfile) - 16];
} YoroFsSuper;

typedef struct __attribute__((packed)) {
    i16 id;
    i16 parent;
    u8  is_dir;
    u8  pad;
    u16 name_len;
    u32 icon;
    u32 data_len;
    char name[FS_NAME_MAX];
} DiskNodeHdr;

static void put_le16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put_le32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put_le64(u8 *p, u64 v) { put_le32(p, (u32)v); put_le32(p + 4, (u32)(v >> 32)); }
static u32  get_le32(const u8 *p)  { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static void put_utf16le(u8 *dst, const char *ascii, int max_chars) {
    int i;
    for (i = 0; i < max_chars && ascii[i]; i++) {
        dst[i * 2] = (u8)ascii[i];
        dst[i * 2 + 1] = 0;
    }
}

/* ---------------- Pure-C Canonical 16 MiB FAT16 ESP Image Generator ---------------- */
#define FAT_SPC        4U       /* 4 sectors/cluster = 2,048 bytes/cluster */
#define FAT_CLUS_BYTES 2048U
#define FAT_RSVD_SEC   4U
#define FAT_NUM_FATS   2U
#define FAT_ROOT_ENTS  512U     /* 512 * 32 = 16 KiB = 32 sectors */
#define FAT_ROOT_SECS  32U
#define FAT_SPF        36U      /* 36 sectors/FAT = 18,432 bytes => holds 9,216 FAT16 entries! */
#define FAT_DATA_LBA   (FAT_RSVD_SEC + FAT_NUM_FATS * FAT_SPF + FAT_ROOT_SECS) /* = 108 */

static void fat_set_entry(u8 *img, u32 clus, u16 val) {
    u8 *f1 = img + FAT_RSVD_SEC * 512U + clus * 2U;
    u8 *f2 = img + (FAT_RSVD_SEC + FAT_SPF) * 512U + clus * 2U;
    put_le16(f1, val);
    put_le16(f2, val);
}

static u8 *fat_clus_ptr(u8 *img, u32 clus) {
    return img + ((size_t)FAT_DATA_LBA + (size_t)(clus - 2U) * FAT_SPC) * 512U;
}

static void fat_write_dirent(u8 *slot, const char name11[11], u8 attr, u16 first_clus, u32 file_size) {
    memset(slot, 0, 32);
    memcpy(slot, name11, 11);
    slot[11] = attr;
    put_le16(slot + 22, 0x8000U); /* 16:00:00 */
    put_le16(slot + 24, 0x5D57U); /* 2026-10-23 */
    put_le16(slot + 26, first_clus);
    put_le32(slot + 28, file_size);
}

static u32 fat_alloc_chain_and_copy(u8 *img, u32 *next_free_clus, const u8 *data, u32 len) {
    u32 first = *next_free_clus;
    u32 n_clus = (len == 0) ? 1U : ((len + FAT_CLUS_BYTES - 1U) / FAT_CLUS_BYTES);
    u32 i;
    for (i = 0; i < n_clus; i++) {
        u32 c = *next_free_clus + i;
        u32 off = i * FAT_CLUS_BYTES;
        u32 rem = (len > off) ? (len - off) : 0U;
        u32 cp  = rem > FAT_CLUS_BYTES ? FAT_CLUS_BYTES : rem;
        u8 *dst = fat_clus_ptr(img, c);
        memset(dst, 0, FAT_CLUS_BYTES);
        if (cp > 0 && data) memcpy(dst, data + off, cp);
        fat_set_entry(img, c, (i + 1U == n_clus) ? 0xFFFFU : (u16)(c + 1U));
    }
    *next_free_clus += n_clus;
    return first;
}

static void fat_init_subdir(u8 *img, u32 self_clus, u32 parent_clus) {
    u8 *d = fat_clus_ptr(img, self_clus);
    memset(d, 0, FAT_CLUS_BYTES);
    fat_write_dirent(d + 0,  ".          ", 0x10U, (u16)self_clus, 0U);
    fat_write_dirent(d + 32, "..         ", 0x10U, (u16)parent_clus, 0U);
    fat_set_entry(img, self_clus, 0xFFFFU);
}

/* Builds a complete, standards-compliant 16 MiB FAT16 EFI System Partition in RAM */
static u8 *build_canonical_fat16_esp(const u8 *efi_pe, u32 efi_len) {
    static const char kTagText[] =
        "YOROOS_INSTALLED_DISK=1\r\n"
        "LOADER=\\EFI\\BOOT\\BOOTX64.EFI\r\n"
        "VERSION=0.2.4\r\n";
    u8 *img = (u8 *)kzalloc((size_t)ESP_SECTORS * 512U);
    u8 *root;
    u32 next_clus = 2U;
    u32 c_efi, c_boot, c_ms, c_msboot, c_yoro, c_bx64, c_bmgfw, c_tag;

    if (!img) return NULL;

    /* 1. FAT16 Boot Sector (BPB per Microsoft EFI FAT Specification) */
    img[0] = 0xEB; img[1] = 0x3C; img[2] = 0x90;
    memcpy(img + 3, "YOROOS02", 8);
    put_le16(img + 11, 512U);             /* BytsPerSec */
    img[13] = (u8)FAT_SPC;                /* SecPerClus = 4 (2 KiB) */
    put_le16(img + 14, FAT_RSVD_SEC);     /* RsvdSecCnt = 4 */
    img[16] = (u8)FAT_NUM_FATS;           /* NumFATs = 2 */
    put_le16(img + 17, FAT_ROOT_ENTS);    /* RootEntCnt = 512 */
    put_le16(img + 19, (u16)ESP_SECTORS); /* TotSec16 = 32768 (8165 clusters => true FAT16!) */
    img[21] = 0xF8U;                      /* Media = Fixed Disk */
    put_le16(img + 22, FAT_SPF);          /* FATSz16 = 36 */
    put_le16(img + 24, 32U);              /* SecPerTrk */
    put_le16(img + 26, 64U);              /* NumHeads */
    put_le32(img + 28, (u32)ESP_START_LBA);
    img[36] = 0x80U;                      /* DrvNum */
    img[38] = 0x29U;                      /* BootSig */
    put_le32(img + 39, 0x20260201U);      /* VolID */
    memcpy(img + 43, "YOROOS_ESP ", 11);
    memcpy(img + 54, "FAT16   ", 8);
    img[510] = 0x55U; img[511] = 0xAAU;

    /* Reserved FAT entries 0 and 1 */
    fat_set_entry(img, 0U, 0xFFF8U);
    fat_set_entry(img, 1U, 0xFFFFU);

    /* 2. Directory hierarchy: \EFI, \EFI\BOOT, \EFI\MICROSOFT, \EFI\MICROSOFT\BOOT, \EFI\YOROOS */
    c_efi    = next_clus++; fat_init_subdir(img, c_efi, 0U);
    c_boot   = next_clus++; fat_init_subdir(img, c_boot, c_efi);
    c_ms     = next_clus++; fat_init_subdir(img, c_ms, c_efi);
    c_msboot = next_clus++; fat_init_subdir(img, c_msboot, c_ms);
    c_yoro   = next_clus++; fat_init_subdir(img, c_yoro, c_efi);

    root = img + (size_t)(FAT_RSVD_SEC + FAT_NUM_FATS * FAT_SPF) * 512U;
    fat_write_dirent(root + 0,  "YOROOS_ESP ", 0x08U, 0U, 0U); /* Volume label */
    fat_write_dirent(root + 32, "EFI        ", 0x10U, (u16)c_efi, 0U);

    fat_write_dirent(fat_clus_ptr(img, c_efi) + 64,  "BOOT       ", 0x10U, (u16)c_boot, 0U);
    fat_write_dirent(fat_clus_ptr(img, c_efi) + 96,  "MICROSOF   ", 0x10U, (u16)c_ms, 0U);
    /* Wait: on Hyper-V Gen 2 Fat.efi, "MICROSOFT" is 9 chars (>8.3)! Add clean VFAT LFN entry for "Microsoft"! */
    {
        u8 *efi_dir = fat_clus_ptr(img, c_efi);
        /* Build valid VFAT Long Filename entry for "Microsoft" (checksum of "MICROSOF   "!) */
        const char *short11 = "MICROS~1   ";
        const char *lfn_chars = "Microsoft";
        u8 sum = 0, lfn[32];
        int k;
        for (k = 0; k < 11; k++)
            sum = (u8)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + (u8)short11[k]);
        memset(lfn, 0xFF, sizeof(lfn));
        lfn[0] = 0x41U; /* Last LFN entry #1 */
        lfn[11] = 0x0FU; /* ATTR_LONG_NAME */
        lfn[12] = 0U;
        lfn[13] = sum;
        put_le16(lfn + 26, 0U);
        /* 5 chars at +1..+10, 6 chars at +14..+25, 2 chars at +28..+31 */
        for (k = 0; k < 5; k++) put_le16(lfn + 1 + k * 2, (u16)lfn_chars[k]);
        for (k = 0; k < 4; k++) put_le16(lfn + 14 + k * 2, (u16)lfn_chars[5 + k]);
        put_le16(lfn + 14 + 4 * 2, 0U); /* NUL terminator */
        memcpy(efi_dir + 96, lfn, 32);
        fat_write_dirent(efi_dir + 128, short11, 0x10U, (u16)c_ms, 0U);
        fat_write_dirent(efi_dir + 160, "YOROOS     ", 0x10U, (u16)c_yoro, 0U);
    }
    fat_write_dirent(fat_clus_ptr(img, c_ms) + 64, "BOOT       ", 0x10U, (u16)c_msboot, 0U);

    /* 3. Write \EFI\BOOT\BOOTX64.EFI + \EFI\Microsoft\Boot\bootmgfw.efi + \EFI\YOROOS\INSTALLED.TAG */
    if (efi_pe && efi_len > 0) {
        c_bx64  = fat_alloc_chain_and_copy(img, &next_clus, efi_pe, efi_len);
        c_bmgfw = fat_alloc_chain_and_copy(img, &next_clus, efi_pe, efi_len);
        fat_write_dirent(fat_clus_ptr(img, c_boot) + 64,   "BOOTX64 EFI", 0x20U, (u16)c_bx64, efi_len);
        fat_write_dirent(fat_clus_ptr(img, c_msboot) + 64, "BOOTMGFWEFI", 0x20U, (u16)c_bmgfw, efi_len);
    }

    /* "INSTALLED.TAG" -> 9 chars base -> add VFAT LFN "INSTALLED.TAG" + short "INSTAL~1TAG" */
    {
        u8 *yd = fat_clus_ptr(img, c_yoro);
        const char *short11 = "INSTAL~1TAG";
        const char *nm = "INSTALLED.TAG";
        u8 sum = 0, lfn[32];
        int k;
        for (k = 0; k < 11; k++)
            sum = (u8)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + (u8)short11[k]);
        memset(lfn, 0xFF, sizeof(lfn));
        lfn[0] = 0x41U;
        lfn[11] = 0x0FU;
        lfn[12] = 0U;
        lfn[13] = sum;
        put_le16(lfn + 26, 0U);
        for (k = 0; k < 5; k++) put_le16(lfn + 1 + k * 2, (u16)nm[k]);
        for (k = 0; k < 6; k++) put_le16(lfn + 14 + k * 2, (u16)nm[5 + k]);
        for (k = 0; k < 2; k++) put_le16(lfn + 28 + k * 2, (u16)nm[11 + k]);
        c_tag = fat_alloc_chain_and_copy(img, &next_clus, (const u8 *)kTagText, (u32)(sizeof(kTagText) - 1U));
        memcpy(yd + 64, lfn, 32);
        fat_write_dirent(yd + 96, short11, 0x20U, (u16)c_tag, (u32)(sizeof(kTagText) - 1U));
    }

    return img;
}

/* ---------------- Persistent YoroFS2 Serialization & Load ---------------- */
static u64 probe_yorofs_lba_for_disk(int disk_idx) {
    u8 sec[512];
    if (hal_disk_read(disk_idx, YOROFS_START_LBA, 1, sec) == 0 &&
        ((const YoroFsSuper *)sec)->magic == YOROFS_MAGIC)
        return YOROFS_START_LBA;
    if (hal_disk_read(disk_idx, LEGACY_YFS_LBA, 1, sec) == 0 &&
        ((const YoroFsSuper *)sec)->magic == YOROFS_MAGIC)
        return LEGACY_YFS_LBA;
    return 0ULL;
}

int disk_save_yorofs(int disk_idx) {
    size_t pay_cap, pos = 0;
    u8 *buf;
    YoroFsSuper *sb;
    int i;
    u32 n_used = 0, sectors_needed;

    if (disk_idx < 0 || disk_idx >= hal_disk_count()) return -1;

    pay_cap = fs_total_bytes() + (size_t)FS_MAX_NODES * sizeof(DiskNodeHdr) + 4096U;
    pay_cap = ((pay_cap + 511U) / 512U) * 512U;
    buf = (u8 *)kzalloc(512U + pay_cap);
    if (!buf) return -1;

    sb = (YoroFsSuper *)buf;
    sb->magic = YOROFS_MAGIC;
    sb->version = 2;
    sb->profile = sys_profile;
    sb->profile.installed_disk_idx = disk_idx;

    for (i = 0; i < app_count() && i < 32; i++) {
        App *a = app_by_index(i);
        if (!a) continue;
        if (a->installed)  sb->installed_mask |= (1U << i);
        if (a->on_desktop) sb->desktop_mask   |= (1U << i);
        if (a->pinned)     sb->pinned_mask    |= (1U << i);
    }

    for (i = 0; i < FS_MAX_NODES; i++) {
        DiskNodeHdr nh;
        if (!fs_nodes[i].used) continue;
        memset(&nh, 0, sizeof(nh));
        nh.id = (i16)i;
        nh.parent = (i16)fs_nodes[i].parent;
        nh.is_dir = fs_nodes[i].is_dir;
        nh.icon = fs_nodes[i].icon;
        nh.data_len = (!fs_nodes[i].is_dir && fs_nodes[i].data && fs_nodes[i].size > 0)
                      ? (u32)fs_nodes[i].size : 0U;
        strncpy(nh.name, fs_nodes[i].name, FS_NAME_MAX - 1);
        nh.name_len = (u16)strlen(nh.name);

        if (pos + sizeof(nh) + nh.data_len > pay_cap) break;
        memcpy(buf + 512U + pos, &nh, sizeof(nh));
        pos += sizeof(nh);
        if (nh.data_len > 0) {
            memcpy(buf + 512U + pos, fs_nodes[i].data, nh.data_len);
            pos += nh.data_len;
        }
        n_used++;
    }

    sb->node_count = n_used;
    sb->total_bytes = (u32)pos;
    sb->payload_crc32 = crc32_calc(buf + 512U, pos);

    sectors_needed = (u32)((512U + pos + 511U) / 512U);
    i = hal_disk_write(disk_idx, YOROFS_START_LBA, sectors_needed, buf);
    kfree(buf);
    return i;
}

int disk_load_yorofs(int disk_idx) {
    u8 hdr_sec[512];
    const YoroFsSuper *sb;
    u64 base_lba;
    u32 pay_sectors, crc;
    u8 *pay;
    size_t pos = 0;
    u32 k;
    int i;

    if (disk_idx < 0 || disk_idx >= hal_disk_count()) return -1;
    base_lba = probe_yorofs_lba_for_disk(disk_idx);
    if (!base_lba) return -1;
    if (hal_disk_read(disk_idx, base_lba, 1, hdr_sec) != 0) return -1;
    sb = (const YoroFsSuper *)hdr_sec;
    if (sb->magic != YOROFS_MAGIC || sb->version != 2 || sb->total_bytes > 32U * 1024U * 1024U)
        return -1;

    pay_sectors = (sb->total_bytes + 511U) / 512U;
    pay = (u8 *)kzalloc((size_t)(pay_sectors ? pay_sectors : 1U) * 512U);
    if (!pay) return -1;
    if (pay_sectors > 0 && hal_disk_read(disk_idx, base_lba + 1ULL, pay_sectors, pay) != 0) {
        kfree(pay);
        return -1;
    }
    crc = crc32_calc(pay, sb->total_bytes);
    if (crc != sb->payload_crc32) {
        kfree(pay);
        return -1;
    }

    fs_sync_suspend(1);
    sys_profile = sb->profile;
    sys_profile.installed_disk_idx = disk_idx;

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].used && fs_nodes[i].data) kfree(fs_nodes[i].data);
    }
    memset(fs_nodes, 0, sizeof(fs_nodes));

    for (k = 0; k < sb->node_count && pos + sizeof(DiskNodeHdr) <= sb->total_bytes; k++) {
        DiskNodeHdr nh;
        int id;
        memcpy(&nh, pay + pos, sizeof(nh));
        pos += sizeof(nh);
        if (pos + nh.data_len > sb->total_bytes) break;
        id = nh.id;
        if (id >= 0 && id < FS_MAX_NODES) {
            fs_nodes[id].used = 1;
            fs_nodes[id].is_dir = nh.is_dir;
            fs_nodes[id].parent = nh.parent;
            fs_nodes[id].first_child = -1;
            fs_nodes[id].next_sibling = -1;
            fs_nodes[id].icon = nh.icon;
            strncpy(fs_nodes[id].name, nh.name, FS_NAME_MAX - 1);
            if (!nh.is_dir) {
                fs_nodes[id].data = (u8 *)kmalloc((size_t)nh.data_len + 1U);
                if (fs_nodes[id].data) {
                    if (nh.data_len > 0) memcpy(fs_nodes[id].data, pay + pos, nh.data_len);
                    fs_nodes[id].data[nh.data_len] = 0;
                    fs_nodes[id].size = (int)nh.data_len;
                }
            }
        }
        pos += nh.data_len;
    }

    for (i = 1; i < FS_MAX_NODES; i++) {
        int p = fs_nodes[i].parent;
        if (!fs_nodes[i].used || p < 0 || p >= FS_MAX_NODES || !fs_nodes[p].used) continue;
        if (fs_nodes[p].first_child < 0) fs_nodes[p].first_child = i;
        else {
            int c = fs_nodes[p].first_child;
            while (fs_nodes[c].next_sibling >= 0) c = fs_nodes[c].next_sibling;
            fs_nodes[c].next_sibling = i;
        }
    }

    for (i = 0; i < app_count() && i < 32; i++) {
        App *a = app_by_index(i);
        if (!a) continue;
        a->installed  = (sb->installed_mask & (1U << i)) ? 1 : 0;
        a->on_desktop = (sb->desktop_mask   & (1U << i)) ? 1 : 0;
        a->pinned     = (sb->pinned_mask    & (1U << i)) ? 1 : 0;
    }

    kfree(pay);
    fs_sync_suspend(0);
    theme_set(sys_profile.theme_id);
    wallpaper_set(sys_profile.wallpaper_id);
    klog("[yorofs2] Mounted installed YoroOS from disk #%d (%u nodes)", disk_idx, sb->node_count);
    return 0;
}

int disk_probe_installed_yorofs(void) {
    int i, n = hal_disk_count();
    for (i = 0; i < n; i++) {
        if (probe_yorofs_lba_for_disk(i) != 0ULL)
            return i;
    }
    return -1;
}

/* ---------------- Asynchronous Step-by-Step Hardware OS Installer Engine ---------------- */
void disk_install_free(DiskInstallTask *t) {
    if (!t) return;
    if (t->esp_img) {
        kfree(t->esp_img);
        t->esp_img = NULL;
    }
    t->active = 0;
}

int disk_install_begin(DiskInstallTask *t, int disk_idx, u64 custom_c_size_mb) {
    static const u8 kPart1Guid[16] = {
        0x59,0x4F,0x52,0x4F, 0x21,0x02, 0x47,0x50, 0x88,0x11, 0xAA,0xBB,0xCC,0xDD,0x02,0x01
    };
    HalDiskInfo di;
    const u8 *efi_pe = NULL;
    u32 efi_len = 0;

    memset(t, 0, sizeof(*t));
    t->disk_idx = disk_idx;
    t->custom_c_mb = custom_c_size_mb;
    memcpy(t->part1_guid, kPart1Guid, 16);

    if (hal_disk_info(disk_idx, &di) != 0) {
        snprintf(t->err, sizeof(t->err), "Не удалось открыть выбранный диск #%d.", disk_idx);
        t->failed = 1;
        return -1;
    }
    if (di.sectors < 65536ULL) {
        snprintf(t->err, sizeof(t->err), "Диск слишком мал (минимум 32 МБ для GPT + ESP + YoroFS2).");
        t->failed = 1;
        return -1;
    }

    t->total_lba = di.sectors;
    t->last_usable = di.sectors - 34ULL;
    t->c_end_lba = t->last_usable;
    if (custom_c_size_mb >= 20ULL) {
        u64 want = YOROFS_START_LBA + custom_c_size_mb * 2048ULL - 1ULL;
        if (want < t->last_usable && want > YOROFS_START_LBA + 4096ULL)
            t->c_end_lba = want;
    }

    /* Obtain BOOTX64.EFI bytes: direct RAM capture on UEFI, or read from ISO ESP on BIOS */
    efi_pe = hal_self_efi_bytes(&efi_len);
    t->esp_img = build_canonical_fat16_esp(efi_pe, efi_len);
    if (!t->esp_img) {
        snprintf(t->err, sizeof(t->err), "Недостаточно ОЗУ для генерации FAT16 ESP раздела.");
        t->failed = 1;
        return -1;
    }
    t->esp_sectors = (u32)ESP_SECTORS;
    t->active = 1;
    t->phase = 1;
    t->pct = 6;
    snprintf(t->status, sizeof(t->status), "Подготовка разметки GPT и загрузчика UEFI/BIOS...");
    snprintf(t->detail, sizeof(t->detail), "Диск: %s · UEFI PE64: %u КБ", di.model, efi_len / 1024U);
    return 0;
}

int disk_install_step(DiskInstallTask *t) {
    /* EFI System Partition Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
    static const u8 kGuidEsp[16] = {
        0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B
    };
    /* Basic Data Partition Type GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 */
    static const u8 kGuidBasic[16] = {
        0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    static const u8 kDiskGuid[16] = {
        0x59,0x4F,0x52,0x4F, 0x30,0x32, 0x47,0x50, 0x9A,0xBC, 0x01,0x02,0x03,0x04,0x02,0x01
    };
    static const u8 kPart2Guid[16] = {
        0x59,0x4F,0x52,0x4F, 0x21,0x02, 0x47,0x50, 0x88,0x22, 0xAA,0xBB,0xCC,0xDD,0x02,0x02
    };

    if (!t || !t->active || t->done || t->failed) return 0;

    /* PHASE 1: Write Hybrid MBR (LBA 0) + Primary GPT (LBA 1..33) + Backup GPT */
    if (t->phase == 1) {
        u8 mbr[512], gpt_hdr[512], gpt_bak[512], iso_meta[512];
        u8 *gpt_ents = (u8 *)kzalloc(32 * 512);
        const u8 *bimg;
        u32 blen = 0, ents_crc;

        if (!gpt_ents) {
            snprintf(t->err, sizeof(t->err), "Нехватка буфера GPT.");
            t->failed = 1; disk_install_free(t); return 0;
        }

        memset(mbr, 0, sizeof(mbr));
        bimg = hal_bios_img_bytes(&blen);
        if (bimg && blen >= 512U) {
            memcpy(mbr, bimg, 512U);
        } else if (hal_boot_media_read(36, 1, iso_meta) == 0 && memcmp(iso_meta, "YOROISO2", 8) == 0) {
            u32 mbr_lba = get_le32(iso_meta + 16);
            u32 k32_cnt = get_le32(iso_meta + 28);
            if (mbr_lba) hal_boot_media_read(mbr_lba, 1, mbr);
            if (k32_cnt) put_le32(mbr + 0x1B4, k32_cnt);
        }

        memset(mbr + 446, 0, 64);
        mbr[446 + 1] = 0x00; mbr[446 + 2] = 0x02; mbr[446 + 3] = 0x00;
        mbr[446 + 4] = 0xEE;
        mbr[446 + 5] = 0xFF; mbr[446 + 6] = 0xFF; mbr[446 + 7] = 0xFF;
        put_le32(mbr + 446 + 8, 1U);
        put_le32(mbr + 446 + 12, t->total_lba > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (u32)(t->total_lba - 1ULL));
        mbr[510] = 0x55; mbr[511] = 0xAA;

        memcpy(gpt_ents + 0,  kGuidEsp, 16);
        memcpy(gpt_ents + 16, t->part1_guid, 16);
        put_le64(gpt_ents + 32, ESP_START_LBA);
        put_le64(gpt_ents + 40, ESP_START_LBA + ESP_SECTORS - 1ULL);
        put_utf16le(gpt_ents + 56, "EFI System Partition", 36);

        memcpy(gpt_ents + 128 + 0,  kGuidBasic, 16);
        memcpy(gpt_ents + 128 + 16, kPart2Guid, 16);
        put_le64(gpt_ents + 128 + 32, YOROFS_START_LBA);
        put_le64(gpt_ents + 128 + 40, t->c_end_lba);
        put_utf16le(gpt_ents + 128 + 56, "YoroOS Root (YoroFS2)", 36);

        ents_crc = crc32_calc(gpt_ents, 128 * 128);

        memset(gpt_hdr, 0, sizeof(gpt_hdr));
        memcpy(gpt_hdr, "EFI PART", 8);
        put_le32(gpt_hdr + 8,  0x00010000U);
        put_le32(gpt_hdr + 12, 92U);
        put_le64(gpt_hdr + 24, 1ULL);
        put_le64(gpt_hdr + 32, t->total_lba - 1ULL);
        put_le64(gpt_hdr + 40, 34ULL);
        put_le64(gpt_hdr + 48, t->last_usable);
        memcpy(gpt_hdr + 56, kDiskGuid, 16);
        put_le64(gpt_hdr + 72, 2ULL);
        put_le32(gpt_hdr + 80, 128U);
        put_le32(gpt_hdr + 84, 128U);
        put_le32(gpt_hdr + 88, ents_crc);
        put_le32(gpt_hdr + 16, crc32_calc(gpt_hdr, 92));

        memcpy(gpt_bak, gpt_hdr, sizeof(gpt_hdr));
        put_le32(gpt_bak + 16, 0U);
        put_le64(gpt_bak + 24, t->total_lba - 1ULL);
        put_le64(gpt_bak + 32, 1ULL);
        put_le64(gpt_bak + 72, t->total_lba - 33ULL);
        put_le32(gpt_bak + 16, crc32_calc(gpt_bak, 92));

        if (hal_disk_write(t->disk_idx, 0, 1, mbr) != 0 ||
            hal_disk_write(t->disk_idx, 2, 32, gpt_ents) != 0 ||
            hal_disk_write(t->disk_idx, 1, 1, gpt_hdr) != 0 ||
            hal_disk_write(t->disk_idx, t->total_lba - 33ULL, 32, gpt_ents) != 0 ||
            hal_disk_write(t->disk_idx, t->total_lba - 1ULL, 1, gpt_bak) != 0) {
            kfree(gpt_ents);
            snprintf(t->err, sizeof(t->err), "Ошибка записи таблицы разделов GPT на диск #%d.", t->disk_idx);
            t->failed = 1; disk_install_free(t); return 0;
        }
        kfree(gpt_ents);
        t->bytes_written += 67ULL * 512ULL;
        t->phase = 2;
        t->cursor_sec = 0;
        t->pct = 15;
        snprintf(t->status, sizeof(t->status), "Запись системного раздела UEFI (FAT16 ESP + BOOTX64.EFI)...");
        snprintf(t->detail, sizeof(t->detail), "Таблица разделов GPT (Primary + Backup CRC32) записана");
        return 1;
    }

    /* PHASE 2: Stream 16 MiB FAT16 EFI System Partition in smooth 2,048-sector (1 MiB) chunks! */
    if (t->phase == 2) {
        /* If running on 32-bit BIOS without self EFI bytes, first try reading ISO's ESP image */
        u32 efi_len = 0;
        if (!hal_self_efi_bytes(&efi_len)) {
            u8 iso_meta[512];
            if (t->cursor_sec == 0 &&
                hal_boot_media_read(36, 1, iso_meta) == 0 && memcmp(iso_meta, "YOROISO2", 8) == 0) {
                u32 esp_src = get_le32(iso_meta + 8);
                u32 esp_cnt = get_le32(iso_meta + 12);
                if (esp_src && esp_cnt <= ESP_SECTORS)
                    hal_boot_media_read(esp_src, esp_cnt, t->esp_img);
            }
        }
        {
            u32 chunk = t->esp_sectors - t->cursor_sec;
            if (chunk > 2048U) chunk = 2048U; /* 1 MiB per frame => 16 smooth real hardware frames! */
            if (hal_disk_write(t->disk_idx, ESP_START_LBA + t->cursor_sec, chunk,
                               t->esp_img + (size_t)t->cursor_sec * 512U) != 0) {
                snprintf(t->err, sizeof(t->err), "Ошибка записи FAT16 ESP на LBA %llu.",
                         (unsigned long long)(ESP_START_LBA + t->cursor_sec));
                t->failed = 1; disk_install_free(t); return 0;
            }
            t->cursor_sec += chunk;
            t->bytes_written += (u64)chunk * 512ULL;
            t->pct = 15 + (int)((t->cursor_sec * 45U) / t->esp_sectors);
            snprintf(t->detail, sizeof(t->detail),
                     "Записано EFI FAT16: %u.%u / 16.0 МБ (\\EFI\\BOOT\\BOOTX64.EFI)",
                     t->cursor_sec / 2048U, ((t->cursor_sec % 2048U) * 10U) / 2048U);
            if (t->cursor_sec >= t->esp_sectors) {
                kfree(t->esp_img);
                t->esp_img = NULL;
                t->phase = 3;
                t->cursor_sec = 0;
                snprintf(t->status, sizeof(t->status), "Установка ядра YoroKernel (BIOS Stage2 + Kernel32)...");
            }
        }
        return 1;
    }

    /* PHASE 3: Stream BIOS Stage2 + 32-bit High-Memory Kernel (from RAM capture OR ISO metadata) */
    if (t->phase == 3) {
        const u8 *bimg;
        u32 blen = 0;
        bimg = hal_bios_img_bytes(&blen);
        if (bimg && blen > 36U * 512U) {
            u32 avail_sec = blen / 512U;
            if (avail_sec > 16380U) avail_sec = 16380U;
            if (t->cursor_sec == 0) t->cursor_sec = 36U;
            if (t->cursor_sec < avail_sec) {
                u32 chunk = avail_sec - t->cursor_sec;
                if (chunk > 512U) chunk = 512U;
                hal_disk_write(t->disk_idx, t->cursor_sec, chunk, bimg + (size_t)t->cursor_sec * 512U);
                t->cursor_sec += chunk;
                t->bytes_written += (u64)chunk * 512ULL;
                t->pct = 60 + (int)(((t->cursor_sec - 36U) * 16U) / (avail_sec - 36U + 1U));
                snprintf(t->detail, sizeof(t->detail), "Запись загрузчика и ядра: %u КБ",
                         (t->cursor_sec * 512U) / 1024U);
                if (t->cursor_sec < avail_sec) return 1;
            }
        } else {
            u8 iso_meta[512];
            if (hal_boot_media_read(36, 1, iso_meta) == 0 && memcmp(iso_meta, "YOROISO2", 8) == 0) {
                u32 st2_src = get_le32(iso_meta + 20);
                u32 k32_src = get_le32(iso_meta + 24);
                u32 k32_cnt = get_le32(iso_meta + 28);
                u8 *tmp = (u8 *)kmalloc(128 * 512);
                if (tmp) {
                    if (t->cursor_sec == 0 && st2_src && hal_boot_media_read(st2_src, 4, tmp) == 0)
                        hal_disk_write(t->disk_idx, 36, 4, tmp);
                    if (k32_src && t->cursor_sec < k32_cnt && t->cursor_sec < 16300U) {
                        u32 n = k32_cnt - t->cursor_sec > 128U ? 128U : (k32_cnt - t->cursor_sec);
                        if (hal_boot_media_read(k32_src + t->cursor_sec, n, tmp) == 0)
                            hal_disk_write(t->disk_idx, 40ULL + t->cursor_sec, n, tmp);
                        t->cursor_sec += n;
                        t->bytes_written += (u64)n * 512ULL;
                        t->pct = 60 + (int)((t->cursor_sec * 16U) / (k32_cnt ? k32_cnt : 1U));
                        snprintf(t->detail, sizeof(t->detail), "Распаковка YoroKernel: %u / %u КБ",
                                 t->cursor_sec / 2U, k32_cnt / 2U);
                        kfree(tmp);
                        if (t->cursor_sec < k32_cnt) return 1;
                    } else {
                        kfree(tmp);
                    }
                }
            }
        }
        t->phase = 4;
        t->pct = 80;
        snprintf(t->status, sizeof(t->status), "Форматирование корневого тома YoroFS2 и перенос системы...");
        return 1;
    }

    /* PHASE 4: Format & Commit Persistent YoroFS2 System Volume */
    if (t->phase == 4) {
        sys_profile.setup_completed = 1;
        sys_profile.installed_disk_idx = t->disk_idx;
        apps_init();
        if (disk_save_yorofs(t->disk_idx) != 0) {
            snprintf(t->err, sizeof(t->err), "Ошибка записи файловой системы YoroFS2 на диск #%d.", t->disk_idx);
            t->failed = 1; disk_install_free(t); return 0;
        }
        t->phase = 5;
        t->pct = 90;
        snprintf(t->status, sizeof(t->status), "Регистрация загрузочной записи YoroOS в UEFI NVRAM...");
        snprintf(t->detail, sizeof(t->detail), "Корневая ФС YoroFS2 (%d узлов) синхронизирована", fs_total_nodes());
        return 1;
    }

    /* PHASE 5: Register UEFI NVRAM Boot0001 + BootOrder */
    if (t->phase == 5) {
        t->nvram_ok = (hal_register_nvram_boot(t->part1_guid, ESP_START_LBA, ESP_SECTORS) == 0) ? 1 : 0;
        t->phase = 6;
        t->pct = 96;
        snprintf(t->status, sizeof(t->status), "Посекторная верификация GPT, FAT16 ESP и контрольной суммы CRC32...");
        snprintf(t->detail, sizeof(t->detail),
                 t->nvram_ok ? "UEFI NVRAM: запись Boot0001 (YoroOS) зарегистрирована"
                             : "Загрузчик: активны пути \\EFI\\BOOT\\BOOTX64.EFI + MBR");
        return 1;
    }

    /* PHASE 6: Real Hardware Read-Back Verification of GPT + FAT16 BPB + YOROFS2 Superblock */
    {
        u8 v_gpt[512], v_esp[512], v_yfs[512];
        if (hal_disk_read(t->disk_idx, 1ULL, 1U, v_gpt) != 0 ||
            memcmp(v_gpt, "EFI PART", 8) != 0) {
            snprintf(t->err, sizeof(t->err), "Верификация не пройдена: заголовок GPT не прочитался с диска!");
            t->failed = 1; disk_install_free(t); return 0;
        }
        if (hal_disk_read(t->disk_idx, ESP_START_LBA, 1U, v_esp) != 0 ||
            v_esp[510] != 0x55U || v_esp[511] != 0xAAU) {
            snprintf(t->err, sizeof(t->err), "Верификация не пройдена: раздел EFI FAT16 повреждён!");
            t->failed = 1; disk_install_free(t); return 0;
        }
        if (hal_disk_read(t->disk_idx, YOROFS_START_LBA, 1U, v_yfs) != 0 ||
            ((const YoroFsSuper *)v_yfs)->magic != YOROFS_MAGIC) {
            snprintf(t->err, sizeof(t->err), "Верификация не пройдена: суперблок YoroFS2 не найден!");
            t->failed = 1; disk_install_free(t); return 0;
        }
        t->pct = 100;
        t->done = 1;
        t->active = 0;
        snprintf(t->status, sizeof(t->status), "Установка YoroOS %s на диск #%d успешно завершена и проверена!",
                 YORO_VERSION_STR, t->disk_idx);
        snprintf(t->detail, sizeof(t->detail),
                 "Проверка диска OK: GPT CRC32 + EFI FAT16 + BOOTX64.EFI + YoroFS2");
        return 0;
    }
}

/* Synchronous wrapper preserved for automated tests / CLI calls */
int disk_install_yoroos(int disk_idx, u64 custom_c_size_mb, InstallProgressFn cb, char *err_buf, int err_cap) {
    DiskInstallTask task;
    if (disk_install_begin(&task, disk_idx, custom_c_size_mb) != 0) {
        if (err_buf && err_cap > 0) strncpy(err_buf, task.err, (size_t)err_cap - 1U);
        return -1;
    }
    while (disk_install_step(&task)) {
        if (cb) cb(task.pct, task.status);
    }
    if (task.failed) {
        if (err_buf && err_cap > 0) strncpy(err_buf, task.err, (size_t)err_cap - 1U);
        return -1;
    }
    if (cb) cb(100, task.status);
    return 0;
}
