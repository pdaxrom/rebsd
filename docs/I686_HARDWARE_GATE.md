# Первый hardware gate: IBM 6563-W4G

Этот gate проверяет только legacy BIOS boot, CPU, E820, VGA/COM1, PCI,
8259A и PIT. Файловая система не монтируется. В текущем i686 image нет
ATA-команд записи: IDE backend предоставляет только `IDENTIFY` и
`READ SECTORS`, а generic disk регистрируется с `DISK_FLAG_READ_ONLY`.

## 1. Собрать и повторить QEMU gate

```sh
cd /Users/sash/Work/N64/rebsd-i686
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 all
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 \
    bios-image-smoke bios-boot-smoke bios-ide-absent-smoke
make -C sys/i386 BOARD=pc \
    O=/Users/sash/Work/N64/rebsd-i686-build/ibm6563 \
    QEMU_MACHINE=pc-i440fx-5.1 bios-boot-smoke
```

Носитель после успешного прогона:

```text
/Users/sash/Work/N64/rebsd-i686-build/ibm6563/obj/sys/i386/rebsd-i686-bios-floppy.img
```

Это raw 1.44 MB floppy image. `mkbios.py` печатает его SHA-256; после
записи носителя надо повторно сверить checksum чтением с носителя.

## 2. Подготовить первый безопасный запуск

1. Полностью выключить IBM и отсоединить питание.
2. Для самого первого запуска отсоединить питание и data cable от всех
   IDE HDD/CF/DOM. CD-ROM также можно оставить отсоединённым. Это исключает
   влияние даже возможной ошибки раннего драйвера на ценный носитель.
3. Оставить AGP VGA adapter, клавиатуру, RAM и floppy drive.
4. Подключить COM1 через настоящий null-modem cable к машине, пишущей
   полный log: `115200 8N1`, без hardware/software flow control.
5. Записать raw image на заведомо выбранную 1.44 MB дискету. Команда записи
   носителя разрушительна для выбранного device: имя `/dev/rdiskN` нельзя
   подставлять до проверки через `diskutil list`.
6. В BIOS выбрать boot с floppy первым. Не менять IDE geometry и не
   разрешать BIOS flash/update utilities.

## 3. Ожидаемый результат без IDE

VGA и COM1 должны показать последовательность, содержащую:

```text
REBSD_I686_BOOT
cpu: i686
boot: linux-x86-2.02
boot-loader: bios-int13
memory-map: ok
pci: mechanism=1
pci-platform: via
ide-primary-master: none
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Машина намеренно остановится на `HALT`; это успех, а не зависание. После
этого питание выключается вручную. Сохранить весь COM1 log и фотографию
последнего VGA screen.

Если нет banner, записать буквально последнее сообщение BIOS/boot sector.
Если есть banner, но нет `HALT`, сохранить полный COM1 log и больше ничего
к машине не подключать до разбора.

## 4. Второй gate с IDE

Этот шаг выполняется только после успешного gate без IDE и разбора его
лога. Сначала лучше подключить пустой тестовый IDE/CF носитель, а не диск
с данными. Ожидаются VIA PCI IDs, `ide-primary-master: ata` и только
read-only probe. Несовпадающая/отсутствующая ReBSD MBR partition допустима
и должна печататься как `external` или `absent`; никаких записей быть не
должно.

LILO для первого gate не нужен. Он остаётся будущим способом загрузки
`rebsd-i686.bzimg` с HDD после проверки native BIOS floppy path и VIA
hardware log.
