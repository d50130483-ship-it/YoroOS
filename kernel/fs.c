/* YoroOS - in-memory filesystem (everything is deletable, including "system") */
#include "kernel.h"
#include "hello_exe.h"

static FSNode nodes[FS_MAX_NODES];
static int node_used = 0;
static int root = -1;

static int alloc_node(void) {
    int i;
    for (i = 0; i < FS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            memset(&nodes[i], 0, sizeof(FSNode));
            nodes[i].used = 1;
            nodes[i].parent = -1;
            nodes[i].first_child = -1;
            nodes[i].next_sibling = -1;
            if (i >= node_used) node_used = i + 1;
            return i;
        }
    }
    return -1;
}

void fs_init(void) {
    memset(nodes, 0, sizeof(nodes));
    node_used = 0;
    root = -1;
    fs_reset();
}

void fs_reset(void) {
    memset(nodes, 0, sizeof(nodes));
    node_used = 0;
    root = alloc_node();
    if (root >= 0) {
        strcpy(nodes[root].name, "C:");
        nodes[root].is_dir = 1;
        nodes[root].icon = 4;
    }
}

int fs_root(void) { return root; }

int fs_lookup(int dir, const char *name) {
    int c;
    if (dir < 0 || dir >= FS_MAX_NODES || !nodes[dir].used) return -1;
    for (c = nodes[dir].first_child; c != -1; c = nodes[c].next_sibling)
        if (!strcmp(nodes[c].name, name)) return c;
    return -1;
}

static void attach(int parent, int node) {
    nodes[node].parent = parent;
    nodes[node].next_sibling = nodes[parent].first_child;
    nodes[parent].first_child = node;
}

int fs_mkdir(int parent, const char *name) {
    int n;
    if (parent < 0 || fs_lookup(parent, name) >= 0) return -1;
    n = alloc_node();
    if (n < 0) return -1;
    strncpy(nodes[n].name, name, FS_NAME_MAX - 1);
    nodes[n].name[FS_NAME_MAX - 1] = 0;
    nodes[n].is_dir = 1;
    nodes[n].icon = 4;
    attach(parent, n);
    return n;
}

int fs_create(int parent, const char *name, const char *data) {
    int n = fs_lookup(parent, name);
    if (n >= 0) {
        if (nodes[n].is_dir) return -1;
        fs_write(n, data ? data : "", data ? (int)strlen(data) : 0);
        return n;
    }
    n = alloc_node();
    if (n < 0) return -1;
    strncpy(nodes[n].name, name, FS_NAME_MAX - 1);
    nodes[n].name[FS_NAME_MAX - 1] = 0;
    nodes[n].is_dir = 0;
    nodes[n].icon = 1;
    attach(parent, n);
    if (data && *data) fs_write(n, data, (int)strlen(data));
    return n;
}

int fs_create_bin(int parent, const char *name, const char *data, int len) {
    int n = fs_lookup(parent, name);
    if (n < 0) {
        n = alloc_node();
        if (n < 0) return -1;
        strncpy(nodes[n].name, name, FS_NAME_MAX - 1);
        nodes[n].name[FS_NAME_MAX - 1] = 0;
        nodes[n].is_dir = 0;
        nodes[n].icon = 1;
        attach(parent, n);
    }
    if (nodes[n].is_dir) return -1;
    return fs_write(n, data, len);
}

int fs_write(int node, const char *data, int len) {
    u8 *buf;
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) return -1;
    if (nodes[node].is_dir) return -1;
    if (len > FS_DATA_MAX) len = FS_DATA_MAX;
    buf = (u8 *)kmalloc((size_t)len + 1);
    if (!buf) return -1;
    if (len) memcpy(buf, data, (size_t)len);
    buf[len] = 0;
    if (nodes[node].data) kfree(nodes[node].data);
    nodes[node].data = buf;
    nodes[node].size = len;
    return len;
}

int fs_remove(int node) {
    int c, next;
    if (node <= 0 || node >= FS_MAX_NODES || !nodes[node].used) return -1;
    for (c = nodes[node].first_child; c != -1; c = next) {
        next = nodes[c].next_sibling;
        fs_remove(c);
    }
    /* unlink from parent */
    {
        int p = nodes[node].parent;
        if (p >= 0) {
            if (nodes[p].first_child == node) {
                nodes[p].first_child = nodes[node].next_sibling;
            } else {
                int it;
                for (it = nodes[p].first_child; it != -1; it = nodes[it].next_sibling)
                    if (nodes[it].next_sibling == node) {
                        nodes[it].next_sibling = nodes[node].next_sibling;
                        break;
                    }
            }
        }
    }
    if (nodes[node].data) kfree(nodes[node].data);
    memset(&nodes[node], 0, sizeof(FSNode));
    return 0;
}

int fs_rename(int node, const char *name) {
    if (node <= 0 || !nodes[node].used) return -1;
    strncpy(nodes[node].name, name, FS_NAME_MAX - 1);
    nodes[node].name[FS_NAME_MAX - 1] = 0;
    return 0;
}

int fs_path(int node, char *out, int outlen) {
    char parts[16][FS_NAME_MAX];
    int depth = 0, i, n = 0;
    out[0] = 0;
    if (node < 0 || !nodes[node].used) return 0;
    while (node > 0 && depth < 16) {
        strncpy(parts[depth], nodes[node].name, FS_NAME_MAX - 1);
        parts[depth][FS_NAME_MAX - 1] = 0;
        depth++;
        node = nodes[node].parent;
    }
    strcpy(out, "C:\\");
    n = 3;
    for (i = depth - 1; i >= 0; i--) {
        int l = (int)strlen(parts[i]);
        if (n + l + 2 >= outlen) break;
        strcpy(out + n, parts[i]);
        n += l;
        if (i > 0) out[n++] = '\\';
    }
    out[n] = 0;
    return n;
}

int fs_parse_path(int cwd, const char *path) {
    int cur = (path[0] == 'C' || path[0] == 'c') && path[1] == ':' ? root : cwd;
    const char *p = path;
    char comp[FS_NAME_MAX];
    if (cur < 0) cur = root;
    if (*p == 'C' || *p == 'c') { if (p[1] == ':') p += 2; }
    while (*p) {
        int n = 0;
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        while (*p && *p != '\\' && *p != '/') {
            if (n < FS_NAME_MAX - 1) comp[n++] = *p;
            p++;
        }
        comp[n] = 0;
        if (!n) break;
        if (!strcmp(comp, ".")) continue;
        if (!strcmp(comp, "..")) { if (nodes[cur].parent >= 0) cur = nodes[cur].parent; continue; }
        {
            int nx = fs_lookup(cur, comp);
            if (nx < 0) return -1;
            cur = nx;
        }
    }
    return cur;
}

int fs_count(int dir) {
    int c, n = 0;
    if (dir < 0) return 0;
    for (c = nodes[dir].first_child; c != -1; c = nodes[c].next_sibling) n++;
    return n;
}

int fs_child(int dir, int index) {
    int c;
    if (dir < 0) return -1;
    for (c = nodes[dir].first_child; c != -1; c = nodes[c].next_sibling) {
        if (index-- == 0) return c;
    }
    return -1;
}

const char *fs_name(int node) { return (node >= 0 && nodes[node].used) ? nodes[node].name : ""; }
int fs_is_dir(int node) { return node >= 0 && nodes[node].used && nodes[node].is_dir; }
int fs_size(int node) { return (node >= 0 && nodes[node].used) ? nodes[node].size : 0; }
const char *fs_data(int node) {
    if (node < 0 || !nodes[node].used || !nodes[node].data) return "";
    return (const char *)nodes[node].data;
}

static const unsigned char demo_zip[] = {
80,75,3,4,20,0,0,0,0,0,0,114,87,93,154,163,164,96,72,0,
0,0,72,0,0,0,9,0,0,0,104,101,108,108,111,46,116,120,116,208,
159,209,128,208,184,208,178,208,181,209,130,32,208,184,208,183,32,90,73,80,
33,32,89,111,114,111,79,83,32,209,128,208,176,209,129,208,191,208,176,208,
186,208,190,208,178,209,139,208,178,208,176,208,181,209,130,32,208,176,209,128,
209,133,208,184,208,178,209,139,46,13,10,80,75,3,4,20,0,0,0,0,
0,0,114,87,93,62,180,165,43,125,0,0,0,125,0,0,0,15,0,0,
0,100,111,99,115,47,114,101,97,100,109,101,46,116,120,116,208,173,209,130,
208,190,209,130,32,209,132,208,176,208,185,208,187,32,208,187,208,181,208,182,
208,176,208,187,32,208,178,32,208,191,208,176,208,191,208,186,208,181,32,208,
178,208,189,209,131,209,130,209,128,208,184,32,208,176,209,128,209,133,208,184,
208,178,208,176,46,13,10,89,111,114,111,79,83,32,209,128,208,176,209,129,
208,191,208,187,209,142,209,137,208,184,209,130,32,208,181,208,179,208,190,32,
208,178,32,208,186,208,190,209,128,208,181,208,189,209,140,32,67,58,46,13,
10,80,75,3,4,20,0,0,0,0,0,0,114,87,93,75,23,224,41,11,
0,0,0,11,0,0,0,11,0,0,0,110,117,109,98,101,114,115,46,116,
120,116,49,32,50,32,51,32,52,32,53,13,10,80,75,1,2,20,3,20,
0,0,0,0,0,0,114,87,93,154,163,164,96,72,0,0,0,72,0,0,
0,9,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,104,
101,108,108,111,46,116,120,116,80,75,1,2,20,3,20,0,0,0,0,0,
0,114,87,93,62,180,165,43,125,0,0,0,125,0,0,0,15,0,0,0,
0,0,0,0,0,0,0,0,128,1,111,0,0,0,100,111,99,115,47,114,
101,97,100,109,101,46,116,120,116,80,75,1,2,20,3,20,0,0,0,0,
0,0,114,87,93,75,23,224,41,11,0,0,0,11,0,0,0,11,0,0,
0,0,0,0,0,0,0,0,0,128,1,25,1,0,0,110,117,109,98,101,
114,115,46,116,120,116,80,75,5,6,0,0,0,0,3,0,3,0,173,0,
0,0,77,1,0,0,0,0,
};
static const int demo_zip_len = 528;



static const char *readme_text =
"YoroOS 0.1 \"Astra\"\r\n"
"=====================\r\n\r\n"
"Добро пожаловать в YoroOS!\r\n\r\n"
"Это ваша система. Здесь нет \"защищённых\" папок:\r\n"
"любой файл и любую папку можно удалить, переименовать\r\n"
"или создать заново. Даже папку Windows.\r\n\r\n"
"Горячие клавиши:\r\n"
"  Ctrl+Alt+T   - терминал\r\n"
"  Alt+Tab      - переключение окон\r\n"
"  Alt+F4       - закрыть окно\r\n"
"  Win          - меню Пуск\r\n"
"  Ctrl+Alt+L   - смена раскладки RU/EN\r\n\r\n"
"В терминале наберите help, чтобы увидеть команды.\r\n";

static const char *notes_text =
"Мои заметки\r\n"
"-----------\r\n"
"1. Сделать YoroOS красивой.\r\n"
"2. Научить систему открывать все файлы Windows.\r\n"
"3. Выпустить первый релиз.\r\n";

void fs_seed(void) {
    int users, yoro, desk, docs, dl, pics, prog, win, sys32, games = -1, todo, i;
    if (root < 0) return;

    users = fs_mkdir(root, "Users");
    yoro = fs_mkdir(users, "Yoro");
    desk = fs_mkdir(yoro, "Desktop");
    docs = fs_mkdir(yoro, "Documents");
    dl = fs_mkdir(yoro, "Downloads");
    pics = fs_mkdir(yoro, "Pictures");
    prog = fs_mkdir(root, "Program Files");
    win = fs_mkdir(root, "Windows");
    sys32 = fs_mkdir(win, "System32");
    fs_mkdir(root, "Games");

    fs_create(docs, "welcome.txt", readme_text);
    fs_create(docs, "notes.txt", notes_text);
    fs_create(desk, "welcome.txt", "YoroOS - рабочий стол\r\nЗдесь могут лежать файлы (.txt, .png, .exe).\r\n");
    fs_create(dl, "chrome_setup.exe", "MZ ... (установщик Chrome для YoroOS 0.2)\r\n");
    fs_create(dl, "matronix_setup.exe", "MZ ... (симуляция установщика Matronix Browser)\r\n");
    fs_create(pics, "wallpaper.bmp", "BM ... (изображение обоев YoroOS)\r\n");
    fs_create(sys32, "kernel.sys", "YoroOS kernel image placeholder\r\n");
    fs_create(sys32, "ntoskrnl.exe", "Это НЕ Windows. Это YoroOS :) Удаляйте смело.\r\n");
    fs_create(win, "win.ini", "[fonts]\r\n[YoroOS]\r\neditable=yes\r\n");
    fs_create(root, "README.TXT", readme_text);
    fs_create_bin(dl, "demo.zip", (const char *)demo_zip, demo_zip_len);
    /* a genuine PE32 program linked into the image: real MZ/PE headers,
     * real section table, real import table for KERNEL32.dll / USER32.dll */
    fs_create_bin(dl, "hello.exe", (const char *)hello_exe, hello_exe_len);
    fs_create_bin(root, "hello.exe", (const char *)hello_exe, hello_exe_len);
    fs_create(prog, "readme.txt", "Установленные программы YoroOS.\r\nНе бойтесь удалять - система переживёт.\r\n");
    UNUSED(games); UNUSED(todo); UNUSED(i);
}
