/* YoroOS 0.2 - Real GPT + FAT16 UEFI Gen 2 ESP + Hybrid BIOS MBR + YOROFS2 Disk Persistence */
#include "kernel.h"

extern FSNode fs_nodes[FS_MAX_NODES];
void fs_sync_suspend(int on);

/* Disk layout constants (all in 512-byte logical LBAs):
 *   LBA 0             : Hybrid Protective/Boot MBR (boot.bin + 0xEE GPT entry)
 *   LBA 1             : Primary GPT Header ("EFI PART", CRC32 verified)
 *   LBA 2..33         : Primary GPT Partition Array (128 entries)
 *   LBA 36..39        : BIOS Stage 2 bootloader (stage2.bin)
 *   LBA 40..16383     : BIOS High-Memory 32-bit Kernel (kernel32.bin)
 *   LBA 16384..32767  : GPT Partition 1 — Canonical 8 MiB FAT16 EFI System Partition
 *                       (\EFI\BOOT\BOOTX64.EFI + \EFI\MICROSOFT\BOOT\BOOTMGFW.EFI)
 *   LBA 32768..end-34 : GPT Partition 2 — Persistent YoroFS2 System Volume (C:)
 *   LBA end-33..end-1 : Backup GPT Partition Array + Backup GPT Header
 *
 * ISO metadata block at LBA 36 ("YOROISO2"):
 *   [0..7]   "YOROISO2"
 *   [8..11]  u32 esp_lba512
 *   [12..15] u32 esp_sectors512   (16384)
 *   [16..19] u32 bios_mbr_lba512
 *   [20..23] u32 stage2_lba512
 *   [24..27] u32 kernel32_lba512
 *   [28..31] u32 kernel32_sectors512
 */

#define ESP_START_LBA    16384ULL
#define ESP_SECTORS      16384ULL
#define YOROFS_START_LBA 32768ULL
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

/* Serialize entire in-memory YoroFS + SysProfile + App registry to persistent disk */
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

    /* Emit nodes in index order so parent references reconstruct identically! */
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

/* Load & verify CRC32-protected YoroFS2 image from physical disk */
int disk_load_yorofs(int disk_idx) {
    u8 hdr_sec[512];
    const YoroFsSuper *sb;
    u32 pay_sectors, crc;
    u8 *pay;
    size_t pos = 0;
    u32 k;
    int i;

    if (disk_idx < 0 || disk_idx >= hal_disk_count()) return -1;
    if (hal_disk_read(disk_idx, YOROFS_START_LBA, 1, hdr_sec) != 0) return -1;
    sb = (const YoroFsSuper *)hdr_sec;
    if (sb->magic != YOROFS_MAGIC || sb->version != 2) return -1;
    if (sb->total_bytes > 32U * 1024U * 1024U) return -1;

    pay_sectors = (sb->total_bytes + 511U) / 512U;
    pay = (u8 *)kzalloc((size_t)(pay_sectors ? pay_sectors : 1U) * 512U);
    if (!pay) return -1;
    if (pay_sectors > 0 && hal_disk_read(disk_idx, YOROFS_START_LBA + 1ULL, pay_sectors, pay) != 0) {
        kfree(pay);
        return -1;
    }
    crc = crc32_calc(pay, sb->total_bytes);
    if (crc != sb->payload_crc32) {
        klog("[yorofs2] CRC mismatch on disk %d (got 0x%x want 0x%x)", disk_idx, crc, sb->payload_crc32);
        kfree(pay);
        return -1;
    }

    fs_sync_suspend(1);
    sys_profile = sb->profile;
    sys_profile.installed_disk_idx = disk_idx;

    /* Clear & rebuild exact FSNode graph */
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

    /* Relink children lists in index order */
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
    klog("[yorofs2] Mounted installed YoroOS volume from disk %d (%u nodes)", disk_idx, sb->node_count);
    return 0;
}

int disk_probe_installed_yorofs(void) {
    int i, n = hal_disk_count();
    for (i = 0; i < n; i++) {
        u8 sec[512];
        if (hal_disk_read(i, YOROFS_START_LBA, 1, sec) == 0 &&
            ((const YoroFsSuper *)sec)->magic == YOROFS_MAGIC)
            return i;
    }
    return -1;
}

/* Full GPT + UEFI Gen 2 FAT16 ESP + Hybrid BIOS MBR + YOROFS2 Disk Formatter & OS Installer */
int disk_install_yoroos(int disk_idx, u64 custom_c_size_mb, InstallProgressFn cb, char *err_buf, int err_cap) {
    HalDiskInfo di;
    u8 iso_meta[512];
    u8 mbr[512];
    u8 gpt_hdr[512];
    u8 gpt_hdr_bak[512];
    u8 *gpt_ents;
    u8 *chunk;
    u64 total_lba, last_usable, c_end_lba;
    u32 esp_src_lba = 0, esp_src_cnt = 0, mbr_src_lba = 0, st2_src_lba = 0, k32_src_lba = 0, k32_src_cnt = 0;
    u32 ents_crc, hdr_crc;
    int has_iso_meta = 0;

    /* EFI System Partition Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
    static const u8 kGuidEsp[16] = {
        0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B
    };
    /* Microsoft Basic Data Type GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 */
    static const u8 kGuidBasic[16] = {
        0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    static const u8 kDiskGuid[16] = {
        0x59,0x4F,0x52,0x4F, 0x30,0x32, 0x47,0x50, 0x9A,0xBC, 0x01,0x02,0x03,0x04,0x05,0x26
    };
    static const u8 kPart1Guid[16] = {
        0x59,0x4F,0x52,0x4F, 0xEA,0x01, 0x47,0x50, 0x88,0x11, 0xAA,0xBB,0xCC,0xDD,0xEE,0x01
    };
    static const u8 kPart2Guid[16] = {
        0x59,0x4F,0x52,0x4F, 0xEA,0x02, 0x47,0x50, 0x88,0x22, 0xAA,0xBB,0xCC,0xDD,0xEE,0x02
    };

    if (hal_disk_info(disk_idx, &di) != 0) {
        snprintf(err_buf, (size_t)err_cap, "Не удалось получить параметры выбранного диска #%d.", disk_idx);
        return -1;
    }
    if (di.sectors < 40960ULL) {
        snprintf(err_buf, (size_t)err_cap, "Диск слишком мал (минимум 24 МБ для ESP + YoroFS2).");
        return -1;
    }

    total_lba = di.sectors;
    last_usable = total_lba - 34ULL;
    c_end_lba = last_usable;
    if (custom_c_size_mb >= 16ULL) {
        u64 want_end = YOROFS_START_LBA + custom_c_size_mb * 2048ULL - 1ULL;
        if (want_end < last_usable && want_end > YOROFS_START_LBA + 4096ULL)
            c_end_lba = want_end;
    }

    if (cb) cb(6, "Чтение загрузочного носителя YoroOS 0.2 (UEFI ESP + BIOS Boot)...");
    memset(iso_meta, 0, sizeof(iso_meta));
    if (hal_boot_media_read(36, 1, iso_meta) == 0 && memcmp(iso_meta, "YOROISO2", 8) == 0) {
        esp_src_lba = get_le32(iso_meta + 8);
        esp_src_cnt = get_le32(iso_meta + 12);
        mbr_src_lba = get_le32(iso_meta + 16);
        st2_src_lba = get_le32(iso_meta + 20);
        k32_src_lba = get_le32(iso_meta + 24);
        k32_src_cnt = get_le32(iso_meta + 28);
        has_iso_meta = 1;
    }

    gpt_ents = (u8 *)kzalloc(32 * 512);
    chunk = (u8 *)kzalloc(64 * 512);
    if (!gpt_ents || !chunk) {
        kfree(gpt_ents); kfree(chunk);
        snprintf(err_buf, (size_t)err_cap, "Недостаточно оперативной памяти для буфера разметки.");
        return -1;
    }

    /* Step 1: Write Protective / Hybrid MBR at LBA 0 */
    if (cb) cb(14, "Создание таблицы разделов GPT и гибридного сектора MBR...");
    memset(mbr, 0, sizeof(mbr));
    if (has_iso_meta && mbr_src_lba)
        hal_boot_media_read(mbr_src_lba, 1, mbr);
    if (k32_src_cnt)
        put_le32(mbr + 0x1B4, k32_src_cnt);
    memset(mbr + 446, 0, 64);
    /* Protective GPT Partition Entry (Type 0xEE covering LBA 1..end) */
    mbr[446 + 0] = 0x00;
    mbr[446 + 1] = 0x00; mbr[446 + 2] = 0x02; mbr[446 + 3] = 0x00;
    mbr[446 + 4] = 0xEE;
    mbr[446 + 5] = 0xFF; mbr[446 + 6] = 0xFF; mbr[446 + 7] = 0xFF;
    put_le32(mbr + 446 + 8, 1U);
    put_le32(mbr + 446 + 12, total_lba > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (u32)(total_lba - 1ULL));
    mbr[510] = 0x55; mbr[511] = 0xAA;
    if (hal_disk_write(disk_idx, 0, 1, mbr) != 0) {
        kfree(gpt_ents); kfree(chunk);
        snprintf(err_buf, (size_t)err_cap, "Ошибка записи сектора MBR/GPT LBA 0.");
        return -1;
    }

    /* Step 2: Build GPT Partition Entries (1 = EFI System Partition, 2 = YoroOS C:) */
    memcpy(gpt_ents + 0,  kGuidEsp, 16);
    memcpy(gpt_ents + 16, kPart1Guid, 16);
    put_le64(gpt_ents + 32, ESP_START_LBA);
    put_le64(gpt_ents + 40, ESP_START_LBA + ESP_SECTORS - 1ULL);
    put_utf16le(gpt_ents + 56, "EFI System Partition", 36);

    memcpy(gpt_ents + 128 + 0,  kGuidBasic, 16);
    memcpy(gpt_ents + 128 + 16, kPart2Guid, 16);
    put_le64(gpt_ents + 128 + 32, YOROFS_START_LBA);
    put_le64(gpt_ents + 128 + 40, c_end_lba);
    put_utf16le(gpt_ents + 128 + 56, "YoroOS 0.2 System (C:)", 36);

    ents_crc = crc32_calc(gpt_ents, 128 * 128);

    /* Primary GPT Header at LBA 1 */
    memset(gpt_hdr, 0, sizeof(gpt_hdr));
    memcpy(gpt_hdr, "EFI PART", 8);
    put_le32(gpt_hdr + 8,  0x00010000U);
    put_le32(gpt_hdr + 12, 92U);
    put_le64(gpt_hdr + 24, 1ULL);
    put_le64(gpt_hdr + 32, total_lba - 1ULL);
    put_le64(gpt_hdr + 40, 34ULL);
    put_le64(gpt_hdr + 48, last_usable);
    memcpy(gpt_hdr + 56, kDiskGuid, 16);
    put_le64(gpt_hdr + 72, 2ULL);
    put_le32(gpt_hdr + 80, 128U);
    put_le32(gpt_hdr + 84, 128U);
    put_le32(gpt_hdr + 88, ents_crc);
    hdr_crc = crc32_calc(gpt_hdr, 92);
    put_le32(gpt_hdr + 16, hdr_crc);

    /* Backup GPT Header at LBA total_lba - 1 */
    memcpy(gpt_hdr_bak, gpt_hdr, sizeof(gpt_hdr));
    put_le32(gpt_hdr_bak + 16, 0U);
    put_le64(gpt_hdr_bak + 24, total_lba - 1ULL);
    put_le64(gpt_hdr_bak + 32, 1ULL);
    put_le64(gpt_hdr_bak + 72, total_lba - 33ULL);
    put_le32(gpt_hdr_bak + 16, crc32_calc(gpt_hdr_bak, 92));

    if (hal_disk_write(disk_idx, 2, 32, gpt_ents) != 0 ||
        hal_disk_write(disk_idx, 1, 1, gpt_hdr) != 0 ||
        hal_disk_write(disk_idx, total_lba - 33ULL, 32, gpt_ents) != 0 ||
        hal_disk_write(disk_idx, total_lba - 1ULL, 1, gpt_hdr_bak) != 0) {
        kfree(gpt_ents); kfree(chunk);
        snprintf(err_buf, (size_t)err_cap, "Ошибка записи основной/резервной таблицы GPT.");
        return -1;
    }

    /* Step 3: Copy Canonical 8 MiB FAT16 UEFI System Partition (\EFI\BOOT\BOOTX64.EFI) */
    if (cb) cb(26, "Форматирование раздела EFI (FAT16) и запись BOOTX64.EFI...");
    if (has_iso_meta && esp_src_lba && esp_src_cnt == ESP_SECTORS) {
        u32 done = 0;
        while (done < esp_src_cnt) {
            u32 n = esp_src_cnt - done > 64U ? 64U : esp_src_cnt - done;
            if (hal_boot_media_read(esp_src_lba + done, n, chunk) != 0) break;
            if (hal_disk_write(disk_idx, ESP_START_LBA + done, n, chunk) != 0) break;
            done += n;
            if (cb && (done & 511U) == 0)
                cb(26 + (int)((done * 32U) / esp_src_cnt),
                   "Копирование системного раздела UEFI Поколения 2 (BOOTX64.EFI)...");
        }
    }

    /* Step 4: Copy BIOS Stage2 + 32-bit High-Memory Kernel (dual-boot Gen 1 + Gen 2!) */
    if (cb) cb(62, "Установка загрузчика Stage2 и ядра YoroKernel 0.2...");
    if (has_iso_meta && st2_src_lba && k32_src_lba && k32_src_cnt) {
        u32 done = 0;
        if (hal_boot_media_read(st2_src_lba, 4, chunk) == 0)
            hal_disk_write(disk_idx, 36, 4, chunk);
        while (done < k32_src_cnt && done < 16300U) {
            u32 n = k32_src_cnt - done > 64U ? 64U : k32_src_cnt - done;
            if (hal_boot_media_read(k32_src_lba + done, n, chunk) != 0) break;
            if (hal_disk_write(disk_idx, 40ULL + done, n, chunk) != 0) break;
            done += n;
            if (cb && (done & 255U) == 0)
                cb(62 + (int)((done * 20U) / k32_src_cnt),
                   "Распаковка системных компонентов YoroKernel 0.2...");
        }
    }

    kfree(gpt_ents);
    kfree(chunk);

    /* Step 5: Format & write YOROFS2 partition + verify CRC32 read-back! */
    if (cb) cb(88, "Создание файловой системы YoroFS2 (том C:) и профиля пользователя...");
    sys_profile.setup_completed = 1;
    sys_profile.installed_disk_idx = disk_idx;
    if (disk_save_yorofs(disk_idx) != 0) {
        snprintf(err_buf, (size_t)err_cap, "Ошибка записи файловой системы YoroFS2 на том C:.");
        return -1;
    }

    if (cb) cb(97, "Проверка контрольной суммы CRC32 установленной системы...");
    {
        u8 verify_sec[512];
        if (hal_disk_read(disk_idx, YOROFS_START_LBA, 1, verify_sec) != 0 ||
            ((const YoroFsSuper *)verify_sec)->magic != YOROFS_MAGIC) {
            snprintf(err_buf, (size_t)err_cap, "Проверка контрольной суммы тома C: не пройдена.");
            return -1;
        }
    }
    if (cb) cb(100, "Установка YoroOS 0.2 «Aurora» успешно завершена!");
    return 0;
}
