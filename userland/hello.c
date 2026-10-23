/* YoroOS - sample Win32 console program (PE32/i386)
 *
 * This file is compiled and linked like a normal Windows program: it imports
 * GetStdHandle / WriteConsoleA / GetTickCount / MessageBoxA / ExitProcess from
 * KERNEL32.dll and USER32.dll, and its entry point is `_start`.  On YoroOS the
 * PE subsystem maps the image, resolves those imports against the kernel stubs
 * and jumps into the code below - the very same bytes Windows would run.
 */
#define API __attribute__((stdcall))

typedef unsigned int u32;

void *API GetStdHandle(u32 id);
int   API WriteConsoleA(void *h, const void *buf, u32 len, u32 *written, void *res);
u32   API GetTickCount(void);
void  API Sleep(u32 ms);
int   API MessageBoxA(void *hwnd, const char *text, const char *caption, u32 type);
void *API GetModuleHandleA(const char *name);
u32   API GetVersion(void);
void  API ExitProcess(u32 code);

#define STD_OUTPUT_HANDLE 0xFFFFFFF5u

static void *hout;

static u32 slen(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void out(const char *s) {
    u32 written = 0;
    WriteConsoleA(hout, s, slen(s), &written, 0);
}

static void out_num(u32 v) {
    char buf[16];
    int n = 0, i;
    char digits[12];
    if (!v) digits[n++] = '0';
    while (v) {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (i = n - 1; i >= 0; i--) buf[i] = digits[n - 1 - i];
    buf[n] = 0;
    out(buf);
}

static void out_hex(u32 v) {
    char buf[12];
    int i;
    for (i = 0; i < 8; i++) buf[i] = "0123456789abcdef"[(v >> ((7 - i) * 4)) & 0xF];
    buf[8] = 0;
    out("0x");
    out(buf + 2);           /* drop the two leading zeros of 0x0040.... */
}

void _start(void) {
    u32 t0, t1, sum, i;

    hout = GetStdHandle(STD_OUTPUT_HANDLE);

    out("YoroOS · подсистема Win32 (PE32)\r\n");
    out("============================================\r\n");
    out("Это настоящая программа для Windows:\r\n");
    out("  формат файла  PE32 / i386 / консоль\r\n");
    out("  импорт     KERNEL32.dll, USER32.dll\r\n");
    out("  код         выполняет ядро YoroOS\r\n\r\n");

    out("Вызовы Win32 API:\r\n");
    out("  GetStdHandle(STD_OUTPUT)  -> ");
    out(hout ? "ok" : "fail");
    out("\r\n");
    out("  WriteConsoleA             -> ok (этот текст пишет сам EXE)\r\n");

    t0 = GetTickCount();
    Sleep(20);
    t1 = GetTickCount();
    out("  GetTickCount              -> ");
    out_num(t1);
    out(" мс (пауза ");
    out_num(t1 - t0);
    out(" мс)\r\n");

    out("  GetModuleHandle(NULL)     -> ");
    out_hex((u32)GetModuleHandleA(0));
    out("\r\n  GetVersion()              -> ");
    out_num((GetVersion() >> 24) & 0xFF);
    out(".");
    out_num((GetVersion() >> 16) & 0xFF);
    out("\r\n");

    sum = 0;
    for (i = 1; i <= 1000; i++) sum += i;
    out("  цикл 1..1000               -> ");
    out_num(sum);
    out(" (считало кодом EXE)\r\n");

    MessageBoxA(0, "Привет из Win32-процесса!",
                "hello.exe", 0);
    out("\r\nГотово. Выход с кодом 0.\r\n");
    ExitProcess(0);
}
