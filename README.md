# YoroOS 0.2 «Aurora» (Pre-Release)

> **Самостоятельная гибридная 64/32-битная операционная система в духе Windows 10/11 с полной нативной поддержкой виртуальных машин Поколения 2 (UEFI x86-64 / Hyper-V Generation 2) и Поколения 1 (Legacy BIOS / VirtualBox / QEMU / VMware).**

---

## Скачать готовый загрузочный образ `.ISO` (без `.zip` и без самостоятельной сборки!)

* **Прямая ссылка на готовый `.ISO` (`9.92 МБ`):**
  **[Скачать `YoroOS-0.2.2.iso` напрямую](https://github.com/d50130483-ship-it/YoroOS/raw/v0.2.2-pre/release/YoroOS-0.2.2.iso)**
* **Страница Pre-Release на GitHub:**
  **[GitHub Releases — `v0.2.2-pre`](https://github.com/d50130483-ship-it/YoroOS/releases/tag/v0.2.2-pre)**

> **Важно для Hyper-V Generation 2 (Поколение 2):**
> В настройках виртуальной машины Hyper-V (Поколение 2) в разделе **Безопасность (Security)** снимите галочку **Включить безопасную загрузку (Enable Secure Boot)**, подключите `YoroOS-0.2.2.iso` в DVD-привод и запустите ВМ. После установки на виртуальный диск (`.vhdx`) `.iso` можно полностью извлечь — система загружается напрямую с установленного диска!

---

## Что нового в YoroOS 0.2 «Aurora»

1. **Нативная работа на Поколении 2 (Hyper-V Generation 2 / OVMF / UEFI x86-64) + Поколении 1 (BIOS):**
   * Полноценное 64-битное ядро `\EFI\BOOT\BOOTX64.EFI` (и `\EFI\Microsoft\Boot\bootmgfw.efi` на системном разделе FAT16 ESP) + 32-битное ядро `kernel32.bin` в одном гибридном `.iso`.
   * Собственный драйвер **Hyper-V VMBus Synthetic Mouse (`045E:0621`, `hv_mouse.c`)** по протоколам `EMCL` / `SynthHID 2.0`, а также `EFI_ABSOLUTE_POINTER_PROTOCOL`, `EFI_SIMPLE_POINTER_PROTOCOL` и `PS/2 i8042 IRQ12`.
   * Графика **UEFI GOP 32-bit TrueColor** / **VESA VBE LFB** с C++ композитором Fluent Acrylic (`kernel/fx.cpp`), сглаженными шрифтами **UTF-8 (Кириллица + Латиница)** и объёмными световыми обоями Windows 10 Hero «Aurora».

2. **Серьёзный оконный установщик ОС (6 шагов, как в Windows Setup):**
   * Шаг 1: Выбор редакции (*Pro*, *Developer Workstation*, *Gaming Edition*, *Home*) и раскладки клавиатуры, либо мгновенный вход в Live-режим.
   * Шаг 2: Лицензионное соглашение и философия полного контроля владельца ПК.
   * Шаг 3: Реальная разметка диска **GPT + Hybrid MBR + EFI System Partition (FAT16) + том `C:` (`YoroFS2`)** с кнопками *Создать разделы*, *Форматировать*, *Очистить*, *Обновить*.
   * Шаг 4: Реальная запись загрузчика и системы на виртуальный диск с проверкой контрольной суммы **IEEE CRC32** (после установки система грузится с диска без `.iso`!).
   * Шаг 5–6: OOBE-настройка имени пользователя, имени компьютера, акцентного цвета и обоев.

3. **Философия абсолютной свободы файловой системы (`YoroFS2`):**
   * В отличие от Windows, YoroOS **никогда не блокирует владельца ПК** сообщениями `TrustedInstaller` / `Отказано в доступе`.
   * Вы можете свободно изменять и **удалять любые системные файлы и папки** — включая `C:\YoroOS`, `C:\YoroOS\System32` и `C:\Windows` — как через Проводник, так и через Терминал (`rd C:\YoroOS`), а при желании мгновенно восстановить их одной кнопкой в **Параметрах YoroOS** или командой `sfc /restore`.

4. **Все 5 ключевых системных приложений + запуск `.EXE` и `.ZIP`:**
   * **Проводник YoroFS (`kernel/app_explorer.c`)** — дерево разделов, хлебные крошки, создание папок/файлов, упаковка в `.ZIP`, беспрепятственное удаление любых папок (включая системные) и сохранение изменений на диск `C:`.
   * **Терминал YoroOS (`kernel/app_terminal.c`)** — команды `help`, `dir`/`ls`, `cd`, `type`/`cat`, `mkdir`, `touch`, `del`/`rm`/`rd`, `copy`, `ren`, `zip`, `unzip`, `run <file.exe>`, `python`, `winget list` / `winget install <id>`, `sfc /restore`, `diskpart`, `ver`, `systeminfo`, `reboot`, `shutdown`.
   * **Параметры YoroOS (`kernel/app_settings.c`)** — обои, цветовые темы, экран и разрешение GOP, учётная запись и раскладка `RU/EN`, восстановление системных папок и сброс до заводских настроек, сведения о системе.
   * **Браузер YoroWeb (`kernel/app_browser.c`)** — вкладки, адресная строка и страницы загрузки **Matronix Browser v3.0.0** ([`d50130483-ship-it/Matronix_Browser`](https://github.com/d50130483-ship-it/Matronix_Browser) с фирменным чёрным интерфейсом `#000000`), **Google Chrome** и **Mozilla Firefox** со скачиванием `.exe` / `.zip` в `Загрузки` и прямой установкой в систему.
   * **Центр приложений YoroOS с иконками (`kernel/app_store.c`)** — установка в 1 клик с иконками на Рабочем столе и в меню «Пуск»: **Matronix Browser 3.0.0**, **Google Chrome**, **Mozilla Firefox**, **Steam** (с встроенной игрой *Cyber Snake*), **Python 3.12 IDE** (с реальным интерпретатором Python!), **Visual Studio Code**, **Discord**, **VLC Media Player**, **Git Bash**.
   * **Нативный загрузчик Windows PE `.EXE` (`kernel/pe.c` + `kernel/pe_asm.S`) и Архиватор `YoroZIP` (`kernel/app_zip.c` + `kernel/inflate.c`)** — чтение заголовков `MZ`/`PE32`/`PE32+`, релокации, таблица импортов Win32 API (`KERNEL32.dll`, `USER32.dll`), нативное исполнение машинного кода `.exe`, а также просмотр, `DEFLATE`-распаковка и создание `.zip` архивов.
