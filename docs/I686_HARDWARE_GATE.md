# Hardware gate: IBM 6563-W4G

Два gate успешно выполнены 2026-07-25 на реальном IBM 6563-W4G с
VIA Apollo Pro 133, AGP VGA и IDE-CF. У машины нет floppy drive, поэтому
существующий GRUB Legacy загрузил `rebsd-i686.bzimg` с Red Hat root
partition `(hd0,2)`.

Текущий код проходит QEMU gate со встроенным read-only UFS root:
общий byte-backed romdisk расположен на block major 0 minor 0 и является
единственным root как при наличии, так и при отсутствии IDE. IDE-CF
регистрируется через общий disk major 2 как `sd0`; её Red Hat разделы не
участвуют в выборе root. На IBM это пока повторять не требуется. QEMU EHCI
USB Mass Storage уже подключён через существующие USB core/`umass`/generic
disk владельцы на major 2: с IDE он получает `sd1`, без IDE — `sd0`.
Общие OHCI и `ukbd` подключены отдельными direct/BIOS QEMU gates для
HID устройств на платах с OHCI controller. Общий UHCI HCD теперь проходит
direct/BIOS QEMU с PIIX3 boot keyboard, boot mouse и mass storage, включая
режим без IDE; host fake-I/O gate отдельно проверяет low-speed TD flags.
i8042 keyboard/mouse также проходят direct/BIOS QEMU с реальными IRQ1/IRQ12.
Следующий input gate уже требует реальный i8042 и VIA USB IBM; storage gate
остаётся отдельной read-only проверкой. Как на Ci20 и N64, writable `/var`
создаётся при каждой загрузке как UFS на общем RAM block driver; i686
подключает 1 MiB backing store как `/dev/ram0`, а `/tmp` указывает на
`/var/tmp`.
Установленный PCI Ethernet controller определён как Realtek `10ec:8169`
revision `10`; общий `sys/pci` RTL8169 driver подключает его как `re0`.
QEMU 11 не содержит модели RTL8169, поэтому аппаратно-точный automated gate
использует fake PCI/BAR/DMA/IRQ backend, а следующий сетевой gate выполняется
на IBM.
ATA-команд записи всё ещё нет: IDE
backend предоставляет только `IDENTIFY` и `READ SECTORS`, generic disk
регистрируется с `DISK_FLAG_READ_ONLY`, а оба write gates возвращают
`EROFS`.

## 1. Собрать и повторить QEMU gate

```sh
cd /Users/sash/Work/N64/rebsd-i686
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 all
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 \
    rootfs-smoke boot-smoke \
    ide-smoke ide-absent-smoke \
    usb-mass-storage-smoke \
    usb-mass-storage-ide-absent-smoke \
    ps2-input-smoke \
    ohci-keyboard-smoke ohci-mouse-smoke \
    uhci-keyboard-smoke uhci-mouse-smoke \
    uhci-mass-storage-smoke \
    uhci-mass-storage-ide-absent-smoke \
    usb-combined-smoke
```

Все перечисленные gates загружают один Linux/x86-protocol artifact
`rebsd-i686.bzimg`. На floppy-less IBM этот же файл загружает GRUB Legacy.

GRUB-compatible artifact:

```text
/Users/sash/Work/N64/rebsd-i686-build/ibm6563/obj/sys/i386/rebsd-i686.bzimg
```

## 2. GRUB Legacy gate

На Red Hat image устанавливается как обычный файл:

```sh
sudo install -m 0644 rebsd-i686.bzimg /boot/rebsd-i686.bzimg
sudo sync
```

`/boot/grub/grub.conf` получает запись в конце файла, чтобы существующий
`default=2` продолжал выбирать Windows 98:

```text
title ReBSD i686 test
	root (hd0,2)
	kernel /boot/rebsd-i686.bzimg
```

GRUB переустанавливать не требуется; `initrd`, `root=`, `ro`, `rhgb` и
`quiet` не используются. До замены image надо сохранить backup CF или
как минимум исходного `grub.conf`.

## 3. Фактический результат первого запуска

VGA log завершился:

```text
ide-lba28: ok
ide-backend-read: ok
ide-lba0: ok
ide-image: external
ide-mbr: present
ide-mbr-table: ok
ide-part0-type: 0x0000000c
ide-part0-start: 0x0000000000000800
ide-part0-sectors: 0x0000000001dcc059
ide-partition-read: external
ide-last-lba: external
ide-bounds: ok
disk-attach: read-only
disk-write-open: erofs
disk-partition: external
disk-strategy-read: external
disk-strategy-eof: ok
disk-strategy-write: erofs
disk-close: ok
pic: ok
pit: hz=100
timer-ticks: 0x0000000a
timer-ticks: ok
HALT
```

Это подтверждает BIOS/GRUB handoff, VGA console, primary IDE-CF LBA28
reads, корректный MBR, bounded partition I/O, два независимых запрета
записи, PIC и PIT на реальном chipset.

## 4. Фактический PCI baseline

Первый длинный VGA log вытеснил начальные PCI строки. Обновлённый image
повторил inventory перед `HALT`:

```text
hardware-summary: pci
hardware-pci-host: 0x11060691
hardware-pci-isa: 0x11060596
hardware-pci-ide: 0x11060571
hardware-pci-vga: 0x121a0005
hardware-pci-platform: via
HALT
```

По официальной [PCI ID database](https://pci-ids.ucw.cz/) это:

- `1106:0691` — VIA VT82C693A/694x, Apollo PRO133x host bridge;
- `1106:0596` — VIA VT82C596 ISA southbridge;
- `1106:0571` — VIA PIPC Bus Master IDE function;
- `121a:0005` — 3Dfx Interactive Voodoo3 AGP VGA.

Ранний порт использует только совместимые legacy interfaces; VIA
bus-master DMA и программирование AGP не включены. COM1 `115200 8N1`
остаётся желательным для последующих длинных logs, но точный PCI baseline
уже зафиксирован через VGA.

## 5. RTL8169 Ethernet gate

Дополнительный PCI controller на IBM определён фактическим `lspci`:

```text
00:10.0 Ethernet controller: 10ec:8169 (rev 10)
```

Ожидаемый attach содержит `re0: RTL8169`, MAC version/XID, BAR, IRQ и MAC
address. i686 устанавливает legacy PCI INTx в level-triggered режим через
ELCR до unmask и разбирает запланированный protocol input сразу на
interrupt-return boundary после EOI. PIT channel 0 используется для
microsecond interpolation между 100 Hz тиками, поэтому `ping` больше не
должен быть искусственно округлён к 10 ms. До настройки адреса надо сохранить
полный экран/serial log, затем проверить интерфейс и link:

```sh
/sbin/ifconfig re0
/usr/bin/netstat -ian
```

Для первого RX/TX gate используется свободный статический адрес из локальной
сети, указанный владельцем сети, без изменения rootfs и без записи на IDE-CF:

```sh
/sbin/ifconfig re0 inet <address> netmask <mask> broadcast <broadcast> up
/usr/bin/ping -n -c 50 <gateway-or-lan-host>
/usr/bin/netstat -ian
/bin/mount
/bin/df
```

Критерий: `re0` остаётся `RUNNING`, link поднимается, ICMP проходит в обе
стороны без секундных задержек, значения RTT не зажаты в шаг 10 ms, а
`Ierrs`, `Oerrs` и `Coll` не растут. `mount` и `df` должны показывать
`/dev/ram0` на `/var`. После этого нужен длительный сетевой прогон на реальной
IBM; QEMU 11 не имеет модели RTL8169 и не заменяет этот gate.
