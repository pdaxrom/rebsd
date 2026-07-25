# План портирования ReBSD на i686/BIOS

Статус: четыре QEMU bring-up инкремента выполнены, 2026-07-25.

## Выполнено

Первый QEMU bring-up инкремент завершён 2026-07-25:

- добавлены `sys/i386` и GCC-only `target-i386.mk`;
- toolchain gate подтверждает `i686-elf`, GCC 14.2.0 и ELF32/i386;
- собираются `rebsd-i686.elf` и Linux boot-protocol 2.02 image
  `rebsd-i686.bzimg`;
- real-mode setup получает BIOS E820, включает A20 и переходит через flat
  GDT в 32-битный payload по адресу 1 МиБ;
- работают ранние COM1 и VGA text consoles;
- `boot-smoke-matrix` прошёл на QEMU 11.0.1 с `pc-i440fx-9.2`,
  `pentium3` и 32/64/128/256 МиБ RAM;
- host `file` распознаёт image как Linux x86 bzImage с версией
  `ReBSD i686 early`.

Второй QEMU bring-up инкремент также завершён:

- установлена 256-entry IDT: отдельные stubs для 32 CPU exceptions и
  IRQ0–IRQ15, остальные vectors ведут в диагностический default handler;
- единый `struct i386_trapframe` сохраняет GPR, segment registers,
  vector/error, EIP/CS/EFLAGS и место для будущих user ESP/SS;
- recoverable `INT3` self-test проверяет dispatch и возврат через `iret`;
- negative `trap-smoke` проверяет `#DE` без error code и `#GP` с
  аппаратным error code;
- dual 8259A remapped на `0x20`/`0x28`, все линии кроме IRQ0 маскированы,
  реализованы EOI и spurious IRQ7/IRQ15;
- PIT channel 0 работает в rate-generator mode с `HZ=100`; normal smoke
  получает десять IRQ0 через `sti; hlt`;
- normal smoke снова прошёл на QEMU с 32/64/128/256 МиБ RAM.

Третий QEMU bring-up инкремент завершён:

- E820 RAM нормализуется в page-aligned диапазоны ниже `0xC0000000`;
  вычитаются non-usable entries, low memory и загруженный kernel;
- работает zeroing monotonic allocator физических страниц по 4 КиБ;
- строятся обычные non-PAE page directory/page tables и identity mappings
  для bootstrap low memory, ядра и доступных RAM ranges;
- linker разделяет RO и RW части ядра по границе 4 КиБ; после загрузки CR3
  включены `CR0.PG` и `CR0.WP`;
- deliberate write в RO probe даёт ожидаемый `#PF`: vector 14,
  error code `0x03` и корректный CR2;
- paging + timer normal smoke прошёл с 32/64/128/256 МиБ RAM, а
  `trap-smoke` проходит для `#DE`, `#GP` и `#PF`.

Четвёртый QEMU bring-up инкремент завершён:

- проведён аудит публичного `sys/vm/pmap.h`, MIPS pmap implementation и
  прямых MIPS-зависимостей generic kernel; результат записан в
  `docs/I386_MD_API.md`;
- paging backend получил reusable `map`, `unmap`, `protect`, `query` и
  `extract` для произвольных virtual/physical pages;
- поддерживаются supervisor/user и read/write PTE/PDE permissions;
- targeted TLB invalidation выполняется через `invlpg`;
- self-test проверяет mapping с offset extraction, RO protection,
  USER mapping, removal и замену resident translation другой physical page;
- normal/trap smoke и RAM matrix продолжают проходить.

Следующая веха: поднять существующие `vm_phys_map`/`vm_page_allocator` над
E820 и реализовать полный публичный i386 `pmap` contract с reclaim
page-table pages.
LILO HDD gate выполняется после появления Linux-среды для установщика.

## 1. Цель и границы первого порта

Цель — получить отдельный 32-битный little-endian порт ReBSD для старых
IBM PC-совместимых компьютеров с legacy BIOS и процессором класса i686.

Первая поддерживаемая платформа:

- uniprocessor i686, protected mode, paging с 4 КиБ страницами;
- legacy BIOS; ранний boot contract совместим с Linux/x86 boot protocol
  2.02, чтобы один image загружался QEMU `-kernel` и LILO `image=`;
- QEMU `pc-i440fx` как референсная машина;
- VGA text console и COM1;
- 8259A PIC и 8253/8254 PIT;
- PS/2-клавиатура;
- PATA/IDE в PIO-режиме;
- MBR и существующая файловая система ReBSD;
- статические ELF32 i386 executables;
- GCC/binutils из `/Users/sash/Library/i686-toolchain`;
- kernel и userland собираются GCC; PCC не меняется и не входит в i686-порт.

На первом этапе не входят: UEFI, SMP/APIC, ACPI, PCI autodetection, USB,
SATA/AHCI, DMA для IDE, графический framebuffer, динамическая линковка,
PCC и поддержка 386/486/586.

Минимальная ISA: i686 без обязательных SSE/SSE2. Ядро и базовый userland
собираются с `-march=i686 -mno-sse -mno-sse2`; использование x87 в ядре
запрещается. Сохранение пользовательского x87-контекста добавляется до
разрешения floating-point userland.

## 2. Проверенный toolchain

В наличии:

```text
/Users/sash/Library/i686-toolchain/bin/i686-elf-gcc
  target: i686-elf
  version: 14.2.0

/Users/sash/Library/i686-toolchain/bin/i686-elf-ld
  GNU binutils 2.44
```

Compile smoke с указанными ниже freestanding-флагами уже выполнен:
получен little-endian `ELF32` object с machine `Intel 80386`. В системе также
есть `/opt/homebrew/bin/qemu-system-i386` версии 11.0.1 с моделями
`pc-i440fx` и `pentium3`. Утилита `lilo` на macOS host сейчас не найдена:
первый QEMU smoke использует прямой `-kernel`, а установка LILO в HDD image
потребует Linux VM/container или запуска установщика на реальной машине.

Базовые переменные сборки:

```make
I686_TOOLCHAIN ?= /Users/sash/Library/i686-toolchain
I686_PREFIX    ?= $(I686_TOOLCHAIN)/bin/i686-elf-
CC             = $(I686_PREFIX)gcc
AS             = $(I686_PREFIX)gcc
LD             = $(I686_PREFIX)ld
AR             = $(I686_PREFIX)ar
RANLIB         = $(I686_PREFIX)ranlib
OBJCOPY        = $(I686_PREFIX)objcopy
OBJDUMP        = $(I686_PREFIX)objdump
READELF        = $(I686_PREFIX)readelf
```

Начальные freestanding-флаги:

```text
-m32 -march=i686 -mtune=generic
-ffreestanding -fno-builtin -fno-stack-protector
-fno-pic -fno-pie -mno-red-zone
-mno-sse -mno-sse2
```

Фактический набор флагов надо закрепить постоянным smoke-тестом toolchain.
Для 32-битного x86 `-mno-red-zone` семантически не нужен, хотя имеющийся GCC
его принимает; его можно убрать для ясности после проверки generated
assembly. На ранней стадии также запрещаем неявные вызовы runtime helpers,
которых ещё нет в `libkern`.

## 3. Предлагаемая структура дерева

```text
sys/i386/
  Makefile                 архитектурная точка входа и out-of-tree dispatch
  Makefile.kconf
  files.kconf
  devices.kconf
  include/machine/         types, machparam, layout, io, cpu, fpu, debug
  common/                  traps, syscall, context switch, pmap, copyin/out
  pc/                      BIOS-PC board: PIC, PIT, VGA, COM1, PS/2, IDE
  boot/                    Linux/x86 boot-protocol setup и bootinfo adapter
  user/                    crt0, syscall stubs, linker script
  tools/                   QEMU smoke scripts and image checks

src/libc/i386/
  gen/                     setjmp/longjmp and byte-order primitives
  sys/                     syscall stubs and signal trampoline

src/startup-i386/          target crt0
target-i386.mk             common GCC/binutils policy
```

Внешний интерфейс остаётся `machine/*`, чтобы machine-independent код не
получал новые прямые зависимости от x86.

## 4. Целевая IBM PC 300GL 6563-W4G

Физический hardware baseline — IBM PC 300GL desktop, machine type/model
`6563-W4G`. Наличие AGP и chipset VIA Apollo Pro 133 на конкретной плате
подтверждены владельцем. Точный объём RAM, CPU stepping, AGP adapter и
дополнительные PCI-карты надо снять с самой машины; код `W4G` не найден в
доступной редакции IBM model tables, поэтому эти параметры не предполагаются.

Документированный общий planar семейства 6563:

- Intel Pentium III, 100/133 MHz FSB;
- VIA Apollo Pro 133 family: VT82C694X north bridge;
- VIA VT82C596B south bridge;
- AGP video adapter с VGA-compatible text mode;
- PCI-to-ISA bridge и классические legacy IRQ;
- два serial ports, PS/2 keyboard/mouse и RTC/CMOS;
- PCI-to-IDE controller, PIO modes 0–4, primary IRQ 14, secondary IRQ 15;
- PC100/PC133 SDRAM, BIOS memory autoconfiguration.

Первый порт намеренно использует только общие PC-интерфейсы, одинаковые для
QEMU и IBM: 8259A, PIT, PS/2, COM1, VGA text buffer и legacy IDE PIO ports.
AGP configuration и vendor-specific VIA bus-master DMA не нужны для boot и
добавляются только после стабильной загрузки с IDE. На первом hardware boot
IDE используется в BIOS compatibility mode через primary `0x1F0`/IRQ14 и
secondary `0x170`/IRQ15.

Stock QEMU не эмулирует точный VIA 694X/596B planar. Референсный профиль
`pc-i440fx-9.2,pentium3` проверяет общий legacy PC слой; отдельный
`IBM6563` hardware gate проверяет VIA PCI IDs и реальные BIOS quirks.

## 5. Последовательность работ

### Этап 0. Зафиксировать baseline и контракты

1. Записать успешный baseline существующего `BOARD=maltael` GCC build/test.
   Это ближайший 32-битный little-endian эталон.
2. Выписать MIPS-зависимости, которые сейчас находятся в generic-коде:
   `mips_curuser`, `mips_init_process`, `mips_user_enter`, frame helpers,
   FPU state, `mips_microtime`, ROMFS naming и `EM_MIPS` в ELF loader.
3. Описать минимальный machine-dependent API для:
   trapframe, user entry, context switch, u-area, pmap/TLB, interrupt levels,
   timecounter, FPU и executable validation.
4. Все артефакты сразу направлять в `O=...`; исходное дерево должно
   оставаться чистым.

Критерий готовности: существующие MIPS targets собираются без регрессий,
а список обязательных MD hooks документирован.

### Этап 1. Skeleton `sys/i386` и проверка toolchain

1. Добавить `sys/i386/Makefile`, kconfig-файлы и `BOARD=pc`.
2. Добавить `target-i386.mk` с единственным режимом `gcc`.
3. Сделать compile/link smoke:
   freestanding C, assembler-with-cpp, ELF32 linker script, `objcopy`,
   отсутствие host headers/libs и нежелательных relocations.
4. Добавить команды:

   ```sh
   make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc toolchain-check
   make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc kernel
   ```

Критерий готовности: минимальный ELF32 kernel корректно определяется
`readelf` как `ELF32`, `Intel 80386`, little-endian, без undefined symbols.

### Этап 2. Linux boot protocol, LILO и ранняя консоль

1. Собирать два связанных артефакта:
   `rebsd-i686.elf` для symbols/debug и `rebsd-i686.bzimg` в формате,
   совместимом с Linux/x86 boot protocol 2.02.
2. Добавить real-mode boot sector/setup с обязательными полями:
   `0xAA55`, `HdrS`, `setup_sects`, protocol `0x0202`,
   `LOAD_HIGH` и 32-bit entry. Protected-mode payload грузится с 1 МиБ.
3. Setup-код нормализует переданные boot parameters, при необходимости
   получает BIOS E820 map, включает A20, ставит bootstrap GDT/stack,
   очищает `.bss` и передаёт общий `i386_bootinfo` в C.
4. Первый QEMU smoke запускает тот же image напрямую:

   ```sh
   qemu-system-i386 -machine pc-i440fx-9.2 -cpu pentium3 \
       -m 64M -kernel rebsd-i686.bzimg -serial stdio -display none
   ```

5. BIOS/HDD gate устанавливает LILO и загружает тот же image через:

   ```text
   image=/boot/rebsd-i686.bzimg
       label=rebsd
       read-only
   ```

   LILO `other=` не является основным путём: он только chainloads boot
   sector и потребовал бы собственного filesystem-aware stage2 уже сейчас.
6. Создать GDT с kernel/user code/data descriptors и TSS.
7. Реализовать ранний COM1 polling и VGA text output.
8. Добавить `run`, `run-serial`, `debug` и позднее `run-lilo-disk`.

Критерий готовности: в QEMU стабильно печатаются banner, нормализованная
BIOS E820 memory map и результат self-check GDT; panic также виден через
COM1. После этого тот же `rebsd-i686.bzimg` проходит LILO boot с HDD image.

### Этап 3. Exceptions, IRQ и время

Низкоуровневый bring-up этого этапа выполнен. Подключение generic
`hardclock` и полноценного `spl*` остаётся до момента, когда i386 войдёт в
machine-independent kernel; page-fault handler уже выводит CR2, но
deliberate `#PF` test возможен только после включения paging на этапе 4.

1. Полная IDT для CPU exceptions и hardware IRQ.
2. Единый `struct i386_trapframe`, одинаково пригодный для trap, syscall,
   signal и debugger paths.
3. Remap/маскирование dual 8259A; корректный EOI и spurious IRQ7/IRQ15.
4. PIT channel 0 на `HZ=100`; подключить `hardclock`.
5. Реализовать `spl*`, interrupt enable/restore и `idle` через `sti; hlt`.
6. Добавить обработку page fault с выводом CR2/error code.

Критерий готовности: таймер тикает длительно без потерь, deliberate
divide-by-zero/page-fault дают диагностируемый panic, IRQ nesting запрещён
или строго контролируется.

### Этап 4. Physical memory и paging

Bootstrap-часть этапа выполнена: E820 normalization, monotonic physical
allocator, CR3 switch, 4-КиБ identity mappings, supervisor-only PTE и
writable protection. Low-level mapper уже умеет `map/unmap/protect/extract`,
USER mappings и `invlpg`. Ещё не выполнены per-process address spaces,
полный публичный `pmap` API, reclaim page-table pages и fault recovery для
`copyin/copyout`.

1. Нормализовать BIOS/boot-protocol memory map, исключая low memory, ROM,
   kernel image, modules и MMIO holes.
2. Ввести 4 КиБ VM pages; не смешивать их с историческими `NBPG`/disk units.
3. Начальный layout:

   ```text
   0x00001000..0xBFFFFFFF  user virtual space
   0xC0000000..0xFFFFFFFF  kernel virtual space
   ```

   Точные границы фиксируются после аудита signed pointer и exec limits.
4. Реализовать page directory/page tables, CR3 switch, `invlpg`,
   supervisor/user и writable protection.
5. Реализовать i386 backend для существующего `sys/vm/pmap.h`:
   create/destroy/activate/map/unmap/protect/extract/copy/zero.
6. Добавить безопасные `copyin`, `copyout`, `copyinstr` с recovery для
   faults из kernel mode.
7. Включить CR0.WP. NX на non-PAE i686 отсутствует; это ограничение
   документируется отдельно.

Критерий готовности: VM unit tests и page-fault tests проходят, user pages
недоступны другому процессу, read-only mappings действительно защищены.

### Этап 5. Processes, syscall ABI, signals

1. Вынести MIPS-имена из generic kernel paths в нейтральные MD hooks.
2. Реализовать kernel stack/u-area allocation и guard page.
3. Реализовать `swtch`, fork child return, exec user entry и scheduler idle.
4. Зафиксировать syscall ABI:
   номер в `EAX`, аргументы через пользовательский stack по cdecl-compatible
   соглашению, результат в `EAX`, errno через carry flag либо единый
   документированный libc convention.
5. Начать с `int 0x80`; `sysenter` не нужен для совместимости.
6. Реализовать signal frame/trampoline, `sigreturn`, ptrace register access.
7. Добавить x87 `FSAVE/FRSTOR` или `FXSAVE/FXRSTOR` только после CPUID
   проверки и выравнивания; до этого user FPU запрещён.

Критерий готовности: kernel запускает минимальный статический init; работают
`fork`, `exec`, `wait`, сигналы, sleep/wakeup и несколько процессов с
раздельными address spaces.

### Этап 6. i386 userland и ELF32

1. Обобщить `exec_elf.c`: machine validation становится MD hook, добавить
   `EM_386`, сохранить MIPS.
2. Добавить i386 user linker script, `crt0`, syscall stubs,
   `setjmp/longjmp`, signal trampoline, endian/string primitives.
3. Аудировать типы ILP32, `long double`, alignment, `va_list`, `jmp_buf`,
   `stat` и сетевые структуры.
4. Сначала собрать минимальный rootfs:
   `init`, `getty`, `login`, `sh`, `mount`, `fsck`, `ls`, `cat`, `echo`.
5. Затем расширять до полного GCC userland; архитектурные программы и
   inline assembly включать только после аудита.

Критерий готовности: login shell в QEMU, базовые filesystem/process tests,
чистый full GCC rootfs build. Никакие i686 rules не заходят в `src/dev/pcc`.

### Этап 7. PC devices и загрузка с диска

Порядок драйверов:

1. VGA text console + COM1 tty.
2. i8042/PS/2 keyboard.
3. RTC CMOS для wall clock.
4. PCI configuration mechanism #1 только как инфраструктура обнаружения.
5. PIIX PATA/IDE, сначала PIO polling, затем IRQ mode.
6. Подключение IDE к существующему `sys/disk` backend contract.
7. MBR partition discovery и root filesystem с IDE-диска.
8. NE2000/RTL8139 — после стабильного storage и VM.

Критерий готовности: одна и та же HDD image грузится в QEMU и на BIOS-PC,
root монтируется read/write, reboot/sync не повреждают файловую систему.

### Этап 8. Реальное старое железо и устойчивость

Проверочная матрица:

```text
QEMU pc-i440fx:  32/64/128/256 MiB, serial + VGA, IDE
Pentium Pro/II:  если доступен
Pentium III:     обязательный реальный baseline
Pentium M:       дополнительный baseline
```

Для каждой машины сохранять:

- CPU/CPUID и объём/карту RAM;
- способ BIOS disk geometry/LBA;
- serial/VGA boot log;
- результаты VM, fork/exec/signal, filesystem stress;
- cold boot, warm reboot и power-loss recovery test.

Критерий релиза: 24-часовой QEMU stress, повторяемая загрузка на двух
разных физических BIOS-PC, отсутствие corruption после filesystem tests.

## 6. Автоматические проверки

Каждая веха должна добавлять короткий gate:

- `toolchain-check`: target, ELF class/machine/endian, relocations;
- `boot-smoke`: ждёт уникальные serial markers;
- `trap-smoke`: exceptions, page fault и IRQ accounting;
- `vm-smoke`: map/protect/fork/address-space isolation;
- `rootfs-smoke`: init/login/shell и filesystem checks;
- `disk-smoke`: MBR, read/write, sync, remount, fsck;
- `hardware-log-check`: обязательные banner/toolchain/buildinfo fields.

QEMU запускается с детерминированными параметрами и без GUI:

```sh
qemu-system-i386 \
  -machine pc-i440fx-9.2 \
  -cpu pentium3 \
  -m 64M \
  -serial stdio \
  -display none \
  -no-reboot \
  -drive file=rebsd-i686-hdd.img,format=raw,if=ide
```

Конкретную модель `-cpu` надо сверить с установленной версией QEMU; smoke
script должен иметь безопасный fallback на `qemu32`.

## 7. Основные риски

1. **MIPS leakage в generic kernel.** Это главный объём ранней работы:
   сейчас ELF validation, u-area, process bootstrap, trapframe и часть VM
   используют MIPS-имена напрямую. Исправлять через узкий MD API, сохраняя
   MIPS build green после каждого шага.
2. **4 КиБ hardware pages против исторического `NBPG=1024`.** Не менять
   старые accounting/disk units вслепую; VM page size должен иметь отдельные
   типы и константы.
3. **GCC 14 optimizations/runtime helpers.** Проверять generated assembly и
   undefined symbols; kernel не должен случайно зависеть от host libgcc.
   Нужные целочисленные helpers либо линкуются из target libgcc осознанно,
   либо реализуются в `libkern`.
4. **BIOS/LILO-различия.** После handoff kernel не должен вызывать BIOS в
   protected mode. QEMU direct boot и LILO обязаны сходиться в одном
   `i386_bootinfo`; отсутствующие boot-protocol поля setup дополняет через
   BIOS до переключения в protected mode.
5. **FPU context corruption.** До полноценного save/restore пользовательский
   x87 выключен; lazy-FPU можно добавлять позже.
6. **Старое железо без serial.** VGA panic console обязательна, но serial
   остаётся основным машинно-читаемым test channel.

7. **Различие QEMU и IBM chipset.** QEMU PIIX и IBM VIA 596B имеют разные
   PCI IDs, поэтому первый IDE backend работает через legacy compatibility
   ports, а PCI bus-master DMA остаётся отдельной поздней задачей.

## 8. Выполненные и ближайший исполнимый инкременты

Первый инкремент содержит:

1. `sys/i386` skeleton и `BOARD=pc`;
2. GCC/binutils toolchain preflight;
3. linker script + Linux boot protocol 2.02 setup + `_start`;
4. COM1/VGA early console;
5. bootable QEMU image и serial `boot-smoke`;
6. документацию сборки.

Definition of Done первого инкремента:

```text
REBSD_I686_BOOT
cpu: i686
boot: linux-x86-2.02
memory-map: ok
gdt: ok
console: com1,vga
HALT
```

Definition of Done второго инкремента:

```text
idt: ok
exception-int3: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Дополнительный `trap-smoke` обязан получить диагностический panic для
`#DE` (vector 0), `#GP` (vector 13 с error code) и write-protection `#PF`
(vector 14, error code 3, CR2).

Definition of Done третьего инкремента:

```text
memory-normalized: ok
physical-allocator: ok
paging: on
cr0.wp: on
kernel-text-ro: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Definition of Done четвёртого инкремента:

```text
pmap-primitives: ok
tlb-invlpg: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Следующий инкремент — generic `vm_page_allocator` bootstrap и полный i386
backend публичного `sys/vm/pmap.h`, а не userland или PCC.
