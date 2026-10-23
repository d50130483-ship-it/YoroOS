YoroOS 0.1 "Astra"

Загрузочный ISO для 32-битного x86 ПК (BIOS, El Torito, без UEFI).
Своё ядро YoroKernel и свой загрузчик YoroBoot (без GRUB).

Запуск в QEMU:
  qemu-system-i386 -cdrom yoroos.iso -m 256 -drive file=disk.img,format=raw

Запуск в VirtualBox/VMware:
  создайте ВМ x86, подключите yoroos.iso как CD-ROM и жёсткий диск (IDE/ATA).

Установка на диск:
  1) загрузитесь с ISO;
  2) откройте "Установить YoroOS" на рабочем столе;
  3) выберите диск и нажмите "Установить";
  4) перезагрузите машину без ISO.

Запись ISO на USB (Linux):
  dd if=yoroos.iso of=/dev/sdX bs=4M

Горячие клавиши:
  Ctrl+Alt+T — терминал, F10 — магазин приложений, Ctrl+Alt+L — раскладка.

Требования: i386-совместимый CPU, 64 МБ ОЗУ, VESA VBE 1024x768x32
(при отсутствии — 800x600/640x480).
