/* YoroOS 0.2 - YoroFS v2 Hierarchical Filesystem + Unrestricted User Freedom + Seed Assets */
#include "kernel.h"
#include "fs_seed.h"

FSNode fs_nodes[FS_MAX_NODES];
static int g_root = 0;
static int g_sync_suppress = 0;

static void fs_auto_sync(void) {
    if (!g_sync_suppress && sys_profile.installed_disk_idx >= 0)
        disk_save_yorofs(sys_profile.installed_disk_idx);
}

void fs_sync_suspend(int on) { g_sync_suppress = on; }

static int alloc_node(void) {
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (!fs_nodes[i].used) {
            memset(&fs_nodes[i], 0, sizeof(FSNode));
            fs_nodes[i].used = 1;
            fs_nodes[i].parent = -1;
            fs_nodes[i].first_child = -1;
            fs_nodes[i].next_sibling = -1;
            return i;
        }
    }
    return -1;
}

static void link_child(int parent, int child) {
    fs_nodes[child].parent = parent;
    fs_nodes[child].next_sibling = -1;
    if (fs_nodes[parent].first_child < 0) {
        fs_nodes[parent].first_child = child;
    } else {
        int c = fs_nodes[parent].first_child;
        while (fs_nodes[c].next_sibling >= 0) c = fs_nodes[c].next_sibling;
        fs_nodes[c].next_sibling = child;
    }
}

static void unlink_child(int parent, int child) {
    int prev = -1, c;
    if (parent < 0 || !fs_nodes[parent].used) return;
    c = fs_nodes[parent].first_child;
    while (c >= 0) {
        if (c == child) {
            if (prev < 0) fs_nodes[parent].first_child = fs_nodes[c].next_sibling;
            else fs_nodes[prev].next_sibling = fs_nodes[c].next_sibling;
            fs_nodes[c].next_sibling = -1;
            fs_nodes[c].parent = -1;
            return;
        }
        prev = c;
        c = fs_nodes[c].next_sibling;
    }
}

static u32 infer_file_icon(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return ICON_TEXTFILE;
    if (!strcasecmp(dot, ".exe") || !strcasecmp(dot, ".msi") || !strcasecmp(dot, ".com")) return ICON_EXE;
    if (!strcasecmp(dot, ".zip") || !strcasecmp(dot, ".7z") || !strcasecmp(dot, ".rar"))  return ICON_ZIP;
    if (!strcasecmp(dot, ".py"))  return ICON_PYTHON;
    if (!strcasecmp(dot, ".reg")) return ICON_REGEDIT;
    if (!strcasecmp(dot, ".bat") || !strcasecmp(dot, ".cmd") || !strcasecmp(dot, ".ps1")) return ICON_TERMINAL;
    if (!strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg")) return ICON_IMAGE;
    if (!strcasecmp(dot, ".c") || !strcasecmp(dot, ".cpp") || !strcasecmp(dot, ".h") ||
        !strcasecmp(dot, ".json") || !strcasecmp(dot, ".js") || !strcasecmp(dot, ".html")) return ICON_CODEFILE;
    return ICON_TEXTFILE;
}

void fs_init(void) {
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (fs_nodes[i].used && fs_nodes[i].data) kfree(fs_nodes[i].data);
    }
    memset(fs_nodes, 0, sizeof(fs_nodes));
    g_root = alloc_node();
    strcpy(fs_nodes[g_root].name, "C:");
    fs_nodes[g_root].is_dir = 1;
    fs_nodes[g_root].icon = ICON_DRIVE;
}

int fs_root(void) { return g_root; }

int fs_lookup(int dir, const char *name) {
    int c;
    if (dir < 0 || !fs_nodes[dir].used || !fs_nodes[dir].is_dir || !name) return -1;
    if (!strcmp(name, ".")) return dir;
    if (!strcmp(name, "..")) return fs_nodes[dir].parent >= 0 ? fs_nodes[dir].parent : dir;
    c = fs_nodes[dir].first_child;
    while (c >= 0) {
        if (!strcasecmp(fs_nodes[c].name, name)) return c;
        c = fs_nodes[c].next_sibling;
    }
    return -1;
}

int fs_mkdir(int parent, const char *name) {
    int ex, n;
    if (parent < 0 || !name || !*name) return -1;
    ex = fs_lookup(parent, name);
    if (ex >= 0 && fs_nodes[ex].is_dir) return ex;
    n = alloc_node();
    if (n < 0) return -1;
    strncpy(fs_nodes[n].name, name, FS_NAME_MAX - 1);
    fs_nodes[n].is_dir = 1;
    fs_nodes[n].icon = ICON_FOLDER;
    link_child(parent, n);
    fs_auto_sync();
    return n;
}

int fs_mkdir_p(int start_dir, const char *path) {
    int cur = start_dir;
    const char *p = path;
    if (!p) return -1;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') { cur = g_root; p += 2; }
    else if (p[0] == '\\' || p[0] == '/') { cur = g_root; }
    while (*p) {
        char seg[FS_NAME_MAX];
        int k = 0;
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        while (*p && *p != '\\' && *p != '/' && k < FS_NAME_MAX - 1) seg[k++] = *p++;
        seg[k] = 0;
        cur = fs_mkdir(cur, seg);
        if (cur < 0) return -1;
    }
    return cur;
}

int fs_write(int node, const void *data, int len) {
    u8 *buf;
    if (node < 0 || !fs_nodes[node].used || fs_nodes[node].is_dir) return -1;
    if (len < 0) len = 0;
    if (len > FS_DATA_MAX) len = FS_DATA_MAX;
    buf = (u8 *)kmalloc((size_t)len + 1U);
    if (!buf) return -1;
    if (len > 0 && data) memcpy(buf, data, (size_t)len);
    buf[len] = 0;
    if (fs_nodes[node].data) kfree(fs_nodes[node].data);
    fs_nodes[node].data = buf;
    fs_nodes[node].size = len;
    fs_auto_sync();
    return 0;
}

int fs_create_bin(int parent, const char *name, const void *data, int len) {
    int n;
    if (parent < 0 || !name || !*name) return -1;
    n = fs_lookup(parent, name);
    if (n >= 0 && !fs_nodes[n].is_dir) {
        fs_write(n, data, len);
        return n;
    }
    n = alloc_node();
    if (n < 0) return -1;
    strncpy(fs_nodes[n].name, name, FS_NAME_MAX - 1);
    fs_nodes[n].is_dir = 0;
    fs_nodes[n].icon = infer_file_icon(name);
    link_child(parent, n);
    fs_write(n, data, len);
    return n;
}

int fs_create(int parent, const char *name, const char *data) {
    return fs_create_bin(parent, name, data ? data : "", data ? (int)strlen(data) : 0);
}

/* UNRESTRICTED USER FREEDOM:
 * Unlike Windows (no TrustedInstaller / Access Denied blocks!), YoroOS allows
 * the owner of the PC to delete ANY folder or file — including C:\YoroOS,
 * C:\Windows, System32, Boot, Program Files — cleanly and safely!
 */
static void free_subtree(int node) {
    int c;
    if (node <= 0 || !fs_nodes[node].used) return;
    c = fs_nodes[node].first_child;
    while (c >= 0) {
        int next = fs_nodes[c].next_sibling;
        free_subtree(c);
        c = next;
    }
    if (fs_nodes[node].data) { kfree(fs_nodes[node].data); fs_nodes[node].data = NULL; }
    fs_nodes[node].used = 0;
}

int fs_remove(int node) {
    int was_sys = 0;
    char pathbuf[192];
    if (node <= 0 || !fs_nodes[node].used) return -1; /* Only root volume C: itself stays mounted */
    fs_path(node, pathbuf, sizeof(pathbuf));
    if (!strncmp(pathbuf, "C:\\YoroOS", 9) || !strncmp(pathbuf, "C:\\Windows", 10) ||
        !strncmp(pathbuf, "C:\\Boot", 7))
        was_sys = 1;
    unlink_child(fs_nodes[node].parent, node);
    free_subtree(node);
    if (was_sys)
        toast("Системный каталог удалён без ограничений! Восстановить: Параметры -> Восстановление или sfc /restore");
    fs_auto_sync();
    desktop_rebuild();
    wm_invalidate();
    return 0;
}

int fs_trash_dir(void) {
    int t = fs_lookup(g_root, "Recycle.Bin");
    if (t < 0) {
        g_sync_suppress++;
        t = fs_mkdir(g_root, "Recycle.Bin");
        if (t >= 0) fs_nodes[t].icon = ICON_TRASH;
        g_sync_suppress--;
    }
    return t;
}

int fs_move_to_trash(int node) {
    int tr = fs_trash_dir();
    if (node <= 0 || !fs_nodes[node].used) return -1;
    if (fs_nodes[node].parent == tr || node == tr) return fs_remove(node);
    unlink_child(fs_nodes[node].parent, node);
    link_child(tr, node);
    fs_auto_sync();
    desktop_rebuild();
    wm_invalidate();
    return 0;
}

int fs_rename(int node, const char *name) {
    if (node <= 0 || !fs_nodes[node].used || !name || !*name) return -1;
    strncpy(fs_nodes[node].name, name, FS_NAME_MAX - 1);
    fs_nodes[node].name[FS_NAME_MAX - 1] = 0;
    if (!fs_nodes[node].is_dir) fs_nodes[node].icon = infer_file_icon(name);
    fs_auto_sync();
    return 0;
}

int fs_copy_node(int node, int dest_dir) {
    char cname[FS_NAME_MAX];
    if (node < 0 || dest_dir < 0 || !fs_nodes[node].used || !fs_nodes[dest_dir].is_dir) return -1;
    strncpy(cname, fs_nodes[node].name, sizeof(cname) - 1);
    cname[sizeof(cname) - 1] = 0;
    if (fs_lookup(dest_dir, cname) >= 0)
        snprintf(cname, sizeof(cname), "Copy_%s", fs_nodes[node].name);
    if (fs_nodes[node].is_dir) {
        int nd = fs_mkdir(dest_dir, cname);
        int c = fs_nodes[node].first_child;
        while (c >= 0 && nd >= 0) {
            fs_copy_node(c, nd);
            c = fs_nodes[c].next_sibling;
        }
        return nd;
    }
    return fs_create_bin(dest_dir, cname, fs_nodes[node].data, fs_nodes[node].size);
}

int fs_path(int node, char *out, int outlen) {
    int chain[32], n = 0, cur = node, pos = 0, i;
    if (node < 0 || !fs_nodes[node].used || outlen < 4) { if (outlen > 0) out[0] = 0; return -1; }
    if (node == g_root) { strncpy(out, "C:\\", (size_t)outlen); return 0; }
    while (cur > 0 && n < 32) { chain[n++] = cur; cur = fs_nodes[cur].parent; }
    pos += snprintf(out + pos, (size_t)(outlen - pos), "C:");
    for (i = n - 1; i >= 0 && pos < outlen - 2; i--)
        pos += snprintf(out + pos, (size_t)(outlen - pos), "\\%s", fs_nodes[chain[i]].name);
    return 0;
}

int fs_parse_path(int cwd, const char *path) {
    int cur = cwd >= 0 ? cwd : g_root;
    const char *p = path;
    if (!p || !*p) return cur;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') { cur = g_root; p += 2; }
    else if (p[0] == '\\' || p[0] == '/') { cur = g_root; }
    while (*p) {
        char seg[FS_NAME_MAX];
        int k = 0;
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        while (*p && *p != '\\' && *p != '/' && k < FS_NAME_MAX - 1) seg[k++] = *p++;
        seg[k] = 0;
        cur = fs_lookup(cur, seg);
        if (cur < 0) return -1;
    }
    return cur;
}

int fs_count(int dir) {
    int n = 0, c;
    if (dir < 0 || !fs_nodes[dir].used || !fs_nodes[dir].is_dir) return 0;
    c = fs_nodes[dir].first_child;
    while (c >= 0) { n++; c = fs_nodes[c].next_sibling; }
    return n;
}

int fs_child(int dir, int index) {
    int c;
    if (dir < 0 || !fs_nodes[dir].used || !fs_nodes[dir].is_dir) return -1;
    c = fs_nodes[dir].first_child;
    while (c >= 0 && index > 0) { c = fs_nodes[c].next_sibling; index--; }
    return c;
}

int fs_parent(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].parent : -1; }
const char *fs_name(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].name : ""; }
int fs_is_dir(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].is_dir : 0; }
int fs_size(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].size : 0; }
u32 fs_icon(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].icon : ICON_TEXTFILE; }
const char *fs_data(int node) { return (node >= 0 && fs_nodes[node].used && fs_nodes[node].data) ? (const char *)fs_nodes[node].data : ""; }
const u8 *fs_raw(int node) { return (node >= 0 && fs_nodes[node].used) ? fs_nodes[node].data : NULL; }

size_t fs_total_bytes(void) {
    size_t sum = 0;
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) if (fs_nodes[i].used) sum += (size_t)fs_nodes[i].size;
    return sum;
}
int fs_total_nodes(void) {
    int n = 0, i;
    for (i = 0; i < FS_MAX_NODES; i++) if (fs_nodes[i].used) n++;
    return n;
}

static int ensure_user_sub(const char *sub) {
    char p[128];
    snprintf(p, sizeof(p), "C:\\Users\\%s\\%s",
             sys_profile.username[0] ? sys_profile.username : "User", sub);
    int n = fs_parse_path(g_root, p);
    if (n < 0) {
        g_sync_suppress++;
        n = fs_mkdir_p(g_root, p);
        g_sync_suppress--;
    }
    return n;
}

int fs_desktop_dir(void)   { return ensure_user_sub("Desktop"); }
int fs_downloads_dir(void) { return ensure_user_sub("Downloads"); }
int fs_documents_dir(void) { return ensure_user_sub("Documents"); }
int fs_pictures_dir(void)  { return ensure_user_sub("Pictures"); }

int fs_windows_present(void) {
    return fs_lookup(g_root, "YoroOS") >= 0 &&
           fs_parse_path(g_root, "C:\\YoroOS\\System32") >= 0;
}

/* Expose compiled native Windows PE (.exe) & .zip installers for Browser / Store downloads! */
const u8 *fs_builtin_pkg_bytes(const char *id, int *out_len, const char **out_filename) {
    if (!strcasecmp(id, "matronix")) {
        *out_len = (int)sizeof(exe_matronix); *out_filename = "MatronixSetup.exe"; return exe_matronix;
    }
    if (!strcasecmp(id, "matronix_zip")) {
        *out_len = (int)sizeof(zip_matronix); *out_filename = "Matronix-3.0.0-win64.zip"; return zip_matronix;
    }
    if (!strcasecmp(id, "chrome")) {
        *out_len = (int)sizeof(exe_chrome); *out_filename = "ChromeSetup.exe"; return exe_chrome;
    }
    if (!strcasecmp(id, "firefox")) {
        *out_len = (int)sizeof(exe_firefox); *out_filename = "FirefoxSetup.exe"; return exe_firefox;
    }
    if (!strcasecmp(id, "steam")) {
        *out_len = (int)sizeof(exe_steam); *out_filename = "SteamSetup.exe"; return exe_steam;
    }
    if (!strcasecmp(id, "python")) {
        *out_len = (int)sizeof(exe_python); *out_filename = "PythonSetup-3.12.exe"; return exe_python;
    }
    if (!strcasecmp(id, "vscode")) {
        *out_len = (int)sizeof(exe_vscode); *out_filename = "VSCodeUserSetup-x64.exe"; return exe_vscode;
    }
    *out_len = (int)sizeof(exe_hello); *out_filename = "hello.exe"; return exe_hello;
}

static void populate_system_tree(void) {
    int sys = fs_mkdir_p(g_root, "C:\\YoroOS\\System32\\drivers");
    int s32 = fs_parse_path(g_root, "C:\\YoroOS\\System32");
    int boot = fs_mkdir_p(g_root, "C:\\Boot\\EFI\\BOOT");
    int pf = fs_mkdir_p(g_root, "C:\\Program Files\\YoroOS");
    int win_compat = fs_mkdir_p(g_root, "C:\\Windows\\System32");

    fs_create(sys, "hv_vmbus_mouse.sys",
              "[YoroOS 0.2 Native Hyper-V Gen 2 Synthetic Mouse Driver]\n"
              "Protocol=EFI_EMCL_PROTOCOL + SynthHid v2.0 (0x045E:0x0621)\n");
    fs_create(sys, "gop_video.sys",
              "[YoroOS 0.2 UEFI GOP + VBE 32bpp Compositor Driver]\n");
    fs_create_bin(s32, "hello.exe", exe_hello, (int)sizeof(exe_hello));
    fs_create_bin(s32, "sysinfo.exe", exe_sysinfo, (int)sizeof(exe_sysinfo));
    fs_create(s32, "os_version.ini",
              "[YoroOS]\nVersion=0.2.6\nCodename=Aurora\nArch=UEFI_x86_64_Gen2_and_BIOS_x86\n"
              "SystemDeleteProtection=DISABLED_BY_DESIGN\n");
    fs_create(boot, "boot.cfg",
              "default=YoroOS-0.2-Aurora\ngpt_esp=FAT16_8142_CLUSTERS\nroot=PARTLABEL=YOROFS2\n");
    fs_create(win_compat, "kernel32.dll.manifest",
              "YoroOS Native Win32/PE32+ IAT Runtime Subsystem (KERNEL32/USER32/SHELL32)\n");
    fs_create(pf, "README.txt",
              "Каталог установленных приложений YoroOS 0.2 «Aurora».\n");
}

void fs_restore_system_files(void) {
    g_sync_suppress++;
    populate_system_tree();
    g_sync_suppress--;
    fs_auto_sync();
    toast("Системные каталоги C:\\YoroOS, C:\\Windows и C:\\Boot полностью восстановлены!");
    wm_invalidate();
}

void fs_seed(void) {
    int d_dl, d_doc, d_pic;
    g_sync_suppress++;
    populate_system_tree();
    (void)fs_trash_dir();

    (void)fs_desktop_dir(); /* keep the Desktop folder present, now empty */
    d_dl   = fs_downloads_dir();
    d_doc  = fs_documents_dir();
    d_pic  = fs_pictures_dir();

    fs_create_bin(d_dl, "hello.exe", exe_hello, (int)sizeof(exe_hello));
    fs_create_bin(d_dl, "Win32_Apps_Pack.zip", zip_win32_pack, (int)sizeof(zip_win32_pack));
    fs_create(d_doc, "Добро пожаловать в YoroOS 0.2.txt",
        "Добро пожаловать в YoroOS 0.2 «Aurora» (Pre-Release)!\n"
        "============================================================\n"
        "Что нового в версии 0.2:\n"
        "  • Полная нативная поддержка Hyper-V Поколения 2 (UEFI x86-64, VMBus мышь, GOP)\n"
        "    и классических ПК / ВМ Поколения 1 (BIOS / ATA / PS2).\n"
        "  • Полноценный оконный Установщик YoroOS на виртуальный диск (GPT + FAT16 ESP + YOROFS2)\n"
        "    с реальным сохранением всех ваших файлов, обоев и программ после перезагрузки!\n"
        "  • Абсолютная свобода владельца ПК: в отличие от Windows, вы можете удалить ДАЖЕ\n"
        "    системные папки C:\\YoroOS, C:\\Windows и System32 — никаких отказов в доступе!\n"
        "    А при желании — в 1 клик восстановить их в Параметрах или командой sfc /restore.\n"
        "  • Прямой запуск Windows .EXE файлов и работа с .ZIP архивами (чтение, распаковка, сжатие).\n"
        "  • Браузер с загрузкой Google Chrome, Mozilla Firefox и Matronix Browser 3.0.0\n"
        "    + Центр установки приложений (Steam, Python 3.12, VS Code, Matronix, Discord, VLC, Git).\n");

    fs_create_bin(d_dl, "MatronixSetup.exe", exe_matronix, (int)sizeof(exe_matronix));
    fs_create_bin(d_dl, "Matronix-3.0.0-win64.zip", zip_matronix, (int)sizeof(zip_matronix));
    fs_create_bin(d_dl, "sysinfo.exe", exe_sysinfo, (int)sizeof(exe_sysinfo));

    fs_create_bin(d_doc, "DevStarterKit.zip", zip_dev_kit, (int)sizeof(zip_dev_kit));
    fs_create(d_doc, "demo_aurora.py",
        "# Пример скрипта Python 3.12 в YoroOS 0.2\n"
        "print('Привет из YoroOS 0.2 Aurora!')\n"
        "for n in [1, 2, 3, 4, 5]:\n"
        "    print('Квадрат числа', n, '=', n * n)\n");
    fs_create(d_doc, "system_check.bat",
        "@echo off\r\necho === Проверка подсистем YoroOS 0.2 ===\r\nver\r\ndir C:\\YoroOS\\System32\r\n");

    /* Small 24x24 truecolor BMP sample inside Pictures! */
    {
        u8 bmp[54 + 24 * 24 * 3];
        int x, y, off = 54;
        memset(bmp, 0, sizeof(bmp));
        bmp[0] = 'B'; bmp[1] = 'M';
        *(u32 *)(bmp + 2) = sizeof(bmp);
        *(u32 *)(bmp + 10) = 54;
        *(u32 *)(bmp + 14) = 40;
        *(u32 *)(bmp + 18) = 24;
        *(u32 *)(bmp + 22) = 24;
        *(u16 *)(bmp + 26) = 1;
        *(u16 *)(bmp + 28) = 24;
        for (y = 0; y < 24; y++)
            for (x = 0; x < 24; x++) {
                bmp[off++] = (u8)(160 + y * 3);
                bmp[off++] = (u8)(60 + x * 7);
                bmp[off++] = (u8)(20 + y * 4);
            }
        fs_create_bin(d_pic, "Aurora_Sample.bmp", bmp, sizeof(bmp));
    }
    g_sync_suppress--;
}

void fs_reset(void) {
    fs_init();
    fs_seed();
}

/* Universal Windows File Dispatcher — opens ANY file type natively! */
void fs_open_any_file(int node) {
    const char *name, *dot;
    const u8 *raw;
    int sz;
    if (node < 0 || !fs_nodes[node].used) return;
    if (fs_nodes[node].is_dir) {
        char p[192];
        fs_path(node, p, sizeof(p));
        explorer_open_path(p);
        return;
    }
    name = fs_name(node);
    dot = strrchr(name, '.');
    raw = fs_raw(node);
    sz = fs_size(node);

    if (sz >= 4 && pe_is_pe(raw, sz)) { exe_open_node(node); return; }
    if (sz >= 4 && raw[0] == 'P' && raw[1] == 'K') { zip_open_node(node); return; }
    if (dot) {
        if (!strcasecmp(dot, ".exe")) { exe_open_node(node); return; }
        if (!strcasecmp(dot, ".zip")) { zip_open_node(node); return; }
        if (!strcasecmp(dot, ".py"))  { vscode_open_file(node); return; }
        if (!strcasecmp(dot, ".bat") || !strcasecmp(dot, ".cmd")) { batch_run_node(node); return; }
        if (!strcasecmp(dot, ".reg")) { regedit_open_file(node); return; }
        if (!strcasecmp(dot, ".bmp")) { image_viewer_open_node(node); return; }
        if (!strcasecmp(dot, ".c") || !strcasecmp(dot, ".cpp") || !strcasecmp(dot, ".h") ||
            !strcasecmp(dot, ".js") || !strcasecmp(dot, ".json") || !strcasecmp(dot, ".html")) {
            App *vs = app_find("vscode");
            if (vs && vs->installed) vscode_open_file(node);
            else notepad_open_file(node);
            return;
        }
    }
    notepad_open_file(node);
}
