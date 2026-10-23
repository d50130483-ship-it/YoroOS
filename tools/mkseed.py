#!/usr/bin/env python3
"""Build genuine Windows PE32 (x86) + PE32+ (x86_64) executables & PKZIP archives for YoroOS 0.2."""
import io
import os
import subprocess
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "kernel", "fs_seed.h")

WIN_HEADER = r'''
#ifdef __x86_64__
#define API
#else
#define API __attribute__((stdcall))
#endif
typedef unsigned int u32;
typedef void *HANDLE;

__declspec(dllimport) HANDLE API GetStdHandle(u32 id);
__declspec(dllimport) int    API WriteConsoleA(HANDLE h, const void *buf, u32 len, u32 *written, void *res);
__declspec(dllimport) HANDLE API CreateFileA(const char *path, u32 access, u32 share, void *sec, u32 disp, u32 flags, HANDLE tpl);
__declspec(dllimport) int    API WriteFile(HANDLE h, const void *buf, u32 len, u32 *written, void *ov);
__declspec(dllimport) int    API CloseHandle(HANDLE h);
__declspec(dllimport) u32    API GetTickCount(void);
__declspec(dllimport) void   API Sleep(u32 ms);
__declspec(dllimport) u32    API GetVersion(void);
__declspec(dllimport) HANDLE API GetModuleHandleA(const char *name);
__declspec(dllimport) int    API GetSystemMetrics(int idx);
__declspec(dllimport) int    API MessageBoxA(HANDLE hwnd, const char *text, const char *caption, u32 type);
__declspec(dllimport) HANDLE API ShellExecuteA(HANDLE hwnd, const char *op, const char *file, const char *params, const char *dir, int show);
__declspec(dllimport) void   API ExitProcess(u32 code);

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void con_print(HANDLE h, const char *s) { u32 w = 0; WriteConsoleA(h, s, slen(s), &w, 0); }
static void con_u32(HANDLE h, u32 v) {
    char d[12], b[16]; int n = 0, i;
    if (!v) d[n++] = '0';
    while (v) { d[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = 0; i < n; i++) b[i] = d[n - 1 - i];
    b[n] = 0;
    con_print(h, b);
}
'''

HELLO_SRC = WIN_HEADER + r'''
void _start(void) {
    HANDLE h = GetStdHandle(0xFFFFFFF5u);
    u32 t0 = GetTickCount();
    u32 sum = 0, i;
    for (i = 1; i <= 50000u; i++) sum += (i ^ (i >> 3));
    Sleep(10);
    u32 t1 = GetTickCount();

    con_print(h, "YoroOS 0.2 Win32/PE Native Execution Engine\r\n");
    con_print(h, "==================================================\r\n");
    con_print(h, "  Формат образа   : Microsoft Portable Executable (.EXE)\r\n");
    con_print(h, "  Импорт DLL      : KERNEL32.dll, USER32.dll\r\n");
    con_print(h, "  Разрешение экрана: ");
    con_u32(h, (u32)GetSystemMetrics(0));
    con_print(h, " x ");
    con_u32(h, (u32)GetSystemMetrics(1));
    con_print(h, "\r\n  Версия подсистемы: ");
    con_u32(h, (GetVersion() >> 24) & 0xFF);
    con_print(h, ".");
    con_u32(h, (GetVersion() >> 16) & 0xFF);
    con_print(h, "\r\n  Тест CPU 50 000 итераций -> контрольная сумма = ");
    con_u32(h, sum);
    con_print(h, "\r\n  Время выполнения : ");
    con_u32(h, t1 - t0);
    con_print(h, " мс\r\n\r\n[OK] Нативный машинный код Windows .EXE успешно выполнен ядром YoroOS!\r\n");

    MessageBoxA(0, "Привет из настоящего скомпилированного Windows .EXE внутри YoroOS 0.2!", "hello.exe — Win32 Subsystem", 0);
    ExitProcess(0);
}
'''

SYSINFO_SRC = WIN_HEADER + r'''
void _start(void) {
    HANDLE h = GetStdHandle(0xFFFFFFF5u);
    u32 w = (u32)GetSystemMetrics(0), hh = (u32)GetSystemMetrics(1);
    u32 up = GetTickCount() / 1000u;
    HANDLE f;
    u32 wr = 0;
    const char *report =
        "=== Отчёт sysinfo.exe (YoroOS 0.2 Win32 API) ===\r\n"
        "Статус ядра: Активно (UEFI Gen 2 / BIOS Hybrid)\r\n"
        "Защита пользователя: Полная свобода управления файлами без блокировок\r\n";

    con_print(h, "[sysinfo.exe] Диагностика среды Windows PE в YoroOS 0.2\r\n");
    con_print(h, "--------------------------------------------------------\r\n");
    con_print(h, "  Видеорежим GOP/VBE : "); con_u32(h, w); con_print(h, "x"); con_u32(h, hh); con_print(h, " 32bpp\r\n");
    con_print(h, "  Время работы ОС    : "); con_u32(h, up); con_print(h, " сек\r\n");

    f = CreateFileA("C:\\Users\\User\\Documents\\sysreport.txt", 0x40000000u, 0, 0, 2, 0x80, 0);
    if (f) {
        WriteFile(f, report, slen(report), &wr, 0);
        CloseHandle(f);
        con_print(h, "  Записан отчёт      : C:\\Users\\User\\Documents\\sysreport.txt (");
        con_u32(h, wr);
        con_print(h, " байт)\r\n");
    }
    MessageBoxA(0, "Диагностика завершена! Файл отчёта сохранён в Documents\\sysreport.txt.", "sysinfo.exe", 0);
    ExitProcess(0);
}
'''

def make_installer_src(pkg_id: str, title: str, exe_name: str, ver: str) -> str:
    return WIN_HEADER + f'''
void _start(void) {{
    HANDLE h = GetStdHandle(0xFFFFFFF5u);
    con_print(h, "Установщик {title} v{ver} ({exe_name})\\r\\n");
    con_print(h, "========================================================\\r\\n");
    con_print(h, "  [1/3] Проверка целостности PE-пакета... OK\\r\\n");
    con_print(h, "  [2/3] Распаковка файлов в C:\\\\Program Files\\\\{pkg_id}... OK\\r\\n");
    ShellExecuteA(0, "install", "{pkg_id}", "", "C:\\\\Program Files", 1);
    con_print(h, "  [3/3] Создание ярлыков на Рабочем столе и в меню «Пуск»... OK\\r\\n\\r\\n");
    con_print(h, "Приложение «{title}» успешно установлено и запущено!\\r\\n");
    MessageBoxA(0, "Приложение «{title}» (v{ver}) установлено на Рабочий стол, в меню «Пуск» и в C:\\\\Program Files!", "{exe_name}", 0);
    ExitProcess(0);
}}
'''

PROGRAMS = [
    ("exe_hello",    HELLO_SRC),
    ("exe_sysinfo",  SYSINFO_SRC),
    ("exe_matronix", make_installer_src("matronix", "Matronix Browser", "MatronixSetup.exe", "3.0.0")),
    ("exe_chrome",   make_installer_src("chrome",   "Google Chrome",    "ChromeSetup.exe",   "131.0")),
    ("exe_firefox",  make_installer_src("firefox",  "Mozilla Firefox",  "FirefoxSetup.exe",  "133.0")),
    ("exe_steam",    make_installer_src("steam",    "Steam Client",     "SteamSetup.exe",    "2.10")),
    ("exe_python",   make_installer_src("python",   "Python 3.12 IDE",  "PythonSetup.exe",   "3.12.7")),
    ("exe_vscode",   make_installer_src("vscode",   "Visual Studio Code", "VSCodeSetup.exe", "1.95")),
]

K32_DEF = """LIBRARY KERNEL32.dll
EXPORTS
GetStdHandle@4
WriteConsoleA@20
CreateFileA@28
WriteFile@20
CloseHandle@4
GetTickCount@0
Sleep@4
GetVersion@0
GetModuleHandleA@4
ExitProcess@4
"""

U32_DEF = """LIBRARY USER32.dll
EXPORTS
GetSystemMetrics@4
MessageBoxA@16
"""

S32_DEF = """LIBRARY SHELL32.dll
EXPORTS
ShellExecuteA@24
"""

def build_arch(arch: str, tmpdir: str) -> dict:
    is64 = arch.startswith("x86_64")
    mach = "x64" if is64 else "x86"
    out = {}
    for sym, code in PROGRAMS:
        cfile = os.path.join(tmpdir, f"{sym}_{mach}.c")
        efile = os.path.join(tmpdir, f"{sym}_{mach}.exe")
        open(cfile, "w").write(code)
        entry = "_start"
        subprocess.run([
            "python3", "-m", "ziglang", "cc", "-target", arch,
            "-Os", "-fno-builtin", "-fno-stack-protector", "-nostdlib",
            f"-Wl,-e,{entry}", "-Wl,--dynamicbase",
            cfile, "-lkernel32", "-luser32", "-lshell32", "-o", efile
        ], check=True)
        out[sym] = open(efile, "rb").read()

    # Build 3 real Deflate-compressed PKZIP (.zip) archives containing the native EXEs!
    z1 = io.BytesIO()
    with zipfile.ZipFile(z1, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.writestr("hello.exe", out["exe_hello"])
        zf.writestr("sysinfo.exe", out["exe_sysinfo"])
        zf.writestr("MatronixSetup.exe", out["exe_matronix"])
        zf.writestr("README_RU.txt",
            "Набор нативных Windows PE (.exe) программ для YoroOS 0.2 «Aurora»!\r\n"
            "Нажмите «Распаковать всё» в Архиваторе YoroZIP или дважды кликните по любому .exe!\r\n".encode("utf-8"))
    out["zip_win32_pack"] = z1.getvalue()

    z2 = io.BytesIO()
    with zipfile.ZipFile(z2, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.writestr("MatronixSetup.exe", out["exe_matronix"])
        zf.writestr("matronix.cfg", b"[Matronix]\r\nversion=3.0.0\r\ntheme=#000000\r\nsearch=Google\r\nvault=AES256\r\n")
        zf.writestr("CHANGELOG_3.0.0.txt",
            "Matronix Browser v3.0.0 (https://github.com/d50130483-ship-it/Matronix_Browser)\r\n"
            "- Минималистичный угольно-чёрный интерфейс (#000000)\r\n"
            "- Вкладки, быстрый выбор поисковика (Google / DuckDuckGo / Bing / Яндекс)\r\n"
            "- Встроенные страницы matronix:settings, history, downloads, passwords\r\n"
            "Запустите MatronixSetup.exe для автоматической установки в YoroOS!\r\n".encode("utf-8"))
    out["zip_matronix"] = z2.getvalue()

    z3 = io.BytesIO()
    with zipfile.ZipFile(z3, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.writestr("hello_aurora.py",
            "# Python 3.12 демо-скрипт в YoroOS 0.2\n"
            "for i in range(1, 6):\n"
            "    print(f'YoroOS 0.2 Aurora -> шаг {i}: квадрат = {i * i}')\n".encode("utf-8"))
        zf.writestr("quick_setup.bat",
            "@echo off\r\necho Запуск пакетного сценария Windows (.bat) в YoroOS 0.2!\r\nver\r\nsysinfo\r\n".encode("utf-8"))
        zf.writestr("accent_aurora.reg",
            "Windows Registry Editor Version 5.00\r\n\r\n"
            "[HKEY_CURRENT_USER\\Software\\YoroOS\\Personalize]\r\n"
            "\"Theme\"=\"Aurora\"\r\n\"AllowSystemDelete\"=dword:00000001\r\n".encode("utf-8"))
    out["zip_dev_kit"] = z3.getvalue()
    return out

def fmt_arr(name: str, data: bytes) -> str:
    cols = [",".join(str(b) for b in data[i:i+24]) for i in range(0, len(data), 24)]
    body = ",\n".join(cols)
    return f"static const u8 {name}[{len(data)}] = {{\n{body}\n}};\n"

def main():
    with tempfile.TemporaryDirectory() as d:
        b64 = build_arch("x86_64-windows-gnu", d)
        b32 = build_arch("x86-windows-gnu", d)
    lines = ["/* Auto-generated by tools/mkseed.py — genuine PE32+/PE32 Windows .exe & Deflate .zip assets */\n"]
    lines.append("#ifdef __x86_64__\n")
    for k, v in b64.items():
        lines.append(fmt_arr(k, v))
    lines.append("#else\n")
    for k, v in b32.items():
        lines.append(fmt_arr(k, v))
    lines.append("#endif\n")
    open(OUT_H, "w").write("".join(lines))
    print("Generated", OUT_H, "size=", os.path.getsize(OUT_H),
          "hello64=", len(b64["exe_hello"]), "zip_matronix=", len(b64["zip_matronix"]))

if __name__ == "__main__":
    main()
