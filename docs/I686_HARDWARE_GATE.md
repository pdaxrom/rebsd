# Hardware gate: IBM 6563-W4G

## Current acceptance status

The IBM 6563-W4G hardware gate was completed on 2026-08-04 with the normal
`rebsd-i686.bzimg` artifact.  That result is the historical baseline; the
`finish_legacy_port` image still requires the repeat recorded in
`docs/I686_LEGACY_PORT_TODO.md`.  The verified configuration uses the embedded
read-only UFS root, writable IDE `wd0`/`rwd0`, USB mass-storage `sd0`/`rsd0`,
writable `/var` on `/dev/ram0`, and `lo0` independently of physical Ethernet.

The physical machine confirmed:

- automatic VIA VT82C596B UDMA4, direct scatter/gather DMA, forced-PIO
  fallback, ATA writes and cache-flush persistence across reboot;
- read-write FAT mount and mutation followed by sync, unmount, remount and a
  clean `fsck.fat -n`; the FAT32 repair utility also persists approved raw
  repairs without treating a merely stale FSInfo next-free hint as damage;
- PS/2 keyboard/mouse, VIA UHCI keyboard/mouse and USB mass storage while IDE
  remains `wd0` and USB remains `sd0`;
- shared VT100 Backspace/readline redraw and correct VGA cursor placement;
- MC146818 set/read persistence after power removal;
- RTL8169 attach, level-triggered INTx, link, IPv4/ARP/ICMP and sustained
  traffic concurrently with IDE DMA, USB and PS/2 without growing interface
  error counters.

The chronological logs below retain the device names and read-only policy
printed by the older images that produced them.  Those historical `sd0`
references describe the IDE disk before disk-class naming was corrected;
they are not the current ABI.

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
Общие OHCI и `ukbd` подключены Linux-protocol QEMU gates для HID устройств
на платах с OHCI controller. Общий UHCI HCD проходит QEMU с PIIX3 boot
keyboard, boot mouse и mass storage, включая режим без IDE; host fake-I/O
gate отдельно проверяет low-speed TD flags. i8042 keyboard/mouse также
проходят QEMU с реальными IRQ1/IRQ12.
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
Общий `sys/pci/pciide` transport теперь реализует IDENTIFY, PIO,
PCI bus-master MWDMA/UDMA, IRQ completion, reset, write и cache flush. Запись
проверяется только host fake-hardware gate. i686 по-прежнему регистрирует
IDE-CF с `DISK_FLAG_READ_ONLY`, поэтому обычный kernel path не посылает
команды записи реальному CF, а write-open/strategy возвращают `EROFS`.

2026-08-02 на том же IBM отдельно проверены оба storage режима одного
общего driver: принудительный PIO и PCI bus-master DMA. В обоих режимах
`dd if=/dev/sd0 of=/dev/null bs=32768 count=128` прочитал 4 MiB, а внешний
IDE-CF остался read-only. Более поздняя DMA-загрузка обнаружила плавающую
гонку VIA: ATA IRQ14 мог прийти до защёлкивания bus-master interrupt status,
а общий handler ошибочно отбрасывал такой IRQ и через пять секунд снимал
`sd0` после timeout. Handler теперь завершает compatibility-mode DMA по
снятому ATA BSY, как требует ATA IRQ boundary, и host gate воспроизводит
ранний ATA IRQ отдельно. Следующий bootlog уточнил диагноз: bus-master
остался active (`command=09`, `status=21`), ATA остался busy (`status=80`),
а interrupt/error не был установлен. Значит конкретный timeout не является
потерянным завершившим IRQ.

Следующий bootlog и Linux 2.6.9 на той же машине показали точную причину:
BIOS оставлял VT82C596B primary master в UDMA66 (`udma=e0080000`), а ReBSD
переводил устройство в MWDMA2 без смены протокола в контроллере. Получалось
рассогласование controller/device, поэтому bus master и ATA оставались
active/busy.

Общий driver теперь выбирает UDMA0--4 на VT82C596B revision 0x12 и новее,
учитывает BIOS cable state как штатный VIA driver и программирует UDMA timing
по таблицам Apollo. Если устройство предоставляет только MWDMA, UDMA enable
primary master сначала очищается, а MWDMA ограничивается PIO-возможностями
IDENTIFY. После DMA
error/timeout он сбрасывает channel и повторяет незавершённый запрос через
PIO, поэтому `sd0` не исчезает. Повторный реальный `ata=dma` gate обязателен;
проверка `ata=auto` выполняется только после него.

Отдельная i386-регрессия успешного DMA была в proc0 wait adapter: ожидание
через `sti; hlt; cli` всегда возвращалось с очищенным IF, хотя до входа
прерывания могли быть разрешены. После дисковой инициализации это прекращало
IRQ1/IRQ12 и внешне выглядело как поломка PS/2. Adapter теперь сохраняет и
восстанавливает исходный interrupt state. `ide-dma-ps2-input-smoke` проверяет
PS/2 login, Backspace, mouse IRQ и повторный ввод с клавиатуры после полного
DMA/network smoke, чтобы IDE больше не мог незаметно выключить input IRQ.

Последовательное чтение `/dev/sd0` использует общий disk read-ahead размером
256 секторов, равным 128-KiB DMA buffer PCI IDE. Одна ATA-команда описывается
двумя стандартными 64-KiB PRD. В `dmesg` должна присутствовать строка
`sd0: read-ahead=256 sectors (128 KB)`. Это объединяет 1-KiB запросы
буферного блочного устройства в одну ATA DMA-команду; PIO остаётся штатным
fallback того же backend. Общий BSD raw-disk path доступен как `/dev/rsd0` и
обходит buffer cache; отдельного i386 raw driver нет. Для `B_PHYS` запросов
PCI IDE теперь использует общий scatter/gather DMA map: VM закрепляет
пользовательские страницы на время операции, i386 переводит их в физические
сегменты, а общий `pciide` строит PRDT непосредственно по этим сегментам.
Копирование полного raw-запроса через coherent bounce buffer устранено.
Buffered I/O и запросы, которые не удовлетворяют аппаратным ограничениям
PRDT, сохраняют прежний coherent-buffer path; ошибка самой DMA-передачи
по-прежнему переводит устройство в PIO и повторяет запрос.

Первое прямое raw DMA чтение один раз добавляет в `dmesg`:

```text
ata0: direct scatter/gather DMA active
```

Отсутствие этой строки после чтения `/dev/rsd0` означает, что проверялся не
прямой scatter/gather path.

Сравнение buffered и raw путей на одном и том же объёме (128 MiB):

```sh
/usr/bin/time /bin/dd if=/dev/sd0 of=/dev/null bs=65536 count=2048
/usr/bin/time /bin/dd if=/dev/rsd0 of=/dev/null bs=1048576 count=128
```

Размер блока и смещение raw-запроса должны быть кратны 512 байтам.

Аудит после аппаратного повтора обнаружил ещё одну независимую регрессию:
изменение сетевого
IRQ latency в `299d2696` снова разрешило выполнять уже ожидающий `netisr`
после любого hardware IRQ, включая PS/2 IRQ1/IRQ12. Это отменяло прежний
фикс `5dc16c3f` именно в конфигурации с активным RTL8169/UHCI, чего нет в QEMU PC
gate. Interrupt dispatcher теперь немедленно обслуживает только сетевую
работу, которую поставили handlers текущего IRQ. Ранее ожидающая работа
остаётся за безопасными system-call/timer boundaries, поэтому PS/2 не входит
в network stack, а сетевой IRQ по-прежнему не получает 10-ms задержку.

## 1. Собрать и повторить QEMU gate

```sh
cd /Users/sash/Work/N64/rebsd-i686
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 all
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 \
    rootfs-smoke boot-smoke \
    ide-pio-smoke ide-dma-smoke ide-auto-smoke ide-absent-smoke \
    ide-dma-ps2-input-smoke \
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
	kernel /boot/rebsd-i686.bzimg ata=pio
```

GRUB переустанавливать не требуется; `initrd`, `root=`, `ro`, `rhgb` и
`quiet` не используются. До замены image надо сохранить backup CF или
как минимум исходного `grub.conf`.

Принудительный `ata=pio` проверен на реальном VIA. Первый `ata=dma` прогон
прошёл, но последующий запуск обнаружил описанную выше гонку раннего IRQ;
исправленный DMA handler требует повторного аппаратного gate. После него
следующий storage запуск использует `ata=auto`. Во всех трёх случаях IDE-CF
остаётся read-only на generic disk уровне. Mode markers:

```text
ata0: mode=pio policy=forced
ata0: mode=udmaN bus-master irq=14 pio-timing=P identify-mwdma=X identify-udma=Y identify-pio=Z
ata0: mode=mwdmaN bus-master irq=14 pio-timing=P identify-mwdma=X identify-udma=Y identify-pio=Z
```

Если DMA transfer завершится ошибкой или timeout, controller останавливается
и сбрасывается, незавершённая операция повторяется через PIO, а все
последующие операции также идут через PIO. Это тот же общий driver path,
отдельного i386 fallback-driver нет.

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

ATA data registers остаются в BIOS compatibility mode, а общий PCI IDE
driver использует bus-master BAR, UDMA/MWDMA и IRQ14. AGP programming не включён.
COM1 `115200 8N1` остаётся желательным для последующих длинных logs, но
точный PCI baseline уже зафиксирован через VGA.

## 5. VGA terminal gate

PS/2 Backspace дошёл через i8042 и общий TTY/readline, но старый i386 VGA
backend печатал управляющие последовательности redraw буквально: на экране
появлялись `ESC[0K`, `ESC[nC` и символы очищаемого хвоста строки. Исправление
не добавляет отдельный i386 parser. VT100 state machine и cell buffer вынесены
из существующих N64/Ci20 реализаций в общий `sys/console/vtconsole.c`; i686,
N64 и Ci20 оставляют только аппаратный renderer и cursor adapter.

Host regression воспроизводит точную последовательность readline после
Backspace (`CR`, сокращённая строка, `CSI 0 K`, `CR`, `CSI n C`). QEMU
`ps2-input-smoke` дополнительно вводит ошибочный login и shell command и
исправляет их настоящей PS/2 клавишей Backspace. На IBM остаётся визуально
подтвердить, что хвост строки очищается, а VGA cursor возвращается в конец
отредактированной команды.

## 6. RTL8169 Ethernet gate

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
