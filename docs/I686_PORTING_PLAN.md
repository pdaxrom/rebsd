# План портирования ReBSD на i686/BIOS

Статус: два IBM 6563-W4G hardware gate сохранены как фактические результаты;
текущий QEMU path всегда использует встроенный read-only UFS root через
общий romdisk major 0 minor 0 и общие VFS/UFS/inode-exec владельцы. IDE
подключается через отдельный generic disk major 2 как `sd0`. Ошибочные
FAT-root, IDE→memory fallback и регистрация romdisk в `sdN` отменены и
удалены из текущей реализации. USB Mass Storage включён в обязательный
i686 storage-план через существующие USB/`umass`/generic disk владельцы,
2026-07-26.

Текущая веха normal boot завершена:

- `sys/i386/pc/boot_main.c` выполняет только MD hardware startup и передаёт
  управление общему `sys/kernel/init_main.c::main`;
- конфигурация `swap none` создаёт обычный `NODEV`; общий startup не
  инициализирует swap, если устройство не задано, без i686 feature-флага;
- встроенный read-only UFS содержит GCC userland: `init`, `getty`, `login`,
  `sh`, `hostname` и `stty`;
- QEMU без внешнего диска монтирует romdisk root, создаёт proc1 общим кодом,
  запускает `/sbin/init`, принимает `root` через общий console/TTY и
  выполняет команду в `/bin/sh`;
- native BIOS loader загружает полный embedded image CHS-чтениями через
  64-КиБ staging buffer и стандартный `INT 15h/AH=87` high-memory move;
- normal kernel больше не линкует i386 diagnostic `*_selftest.o`, private
  VFS bootstrap или process-bootstrap и не завершает штатную загрузку через
  marker `HALT`;
- следующий этап остаётся QEMU-only: расширение нужного userland,
  external IDE/USB mount path и PS/2/VGA.

Общий этап аппаратного времени завершён 2026-08-01 одновременно для i686 и
Ci20.  `sys/kernel/todr.c` выбирает hardware provider по приоритету, выполняет
fallback на timestamp root filesystem и записывает все доступные RTC после
успешного `settimeofday`.  i686 использует MC146818 CMOS; Ci20 использует
PCF8563 через I2C4 как primary и внутренний JZ4780 RTC как secondary.  BCD и
Gregorian conversion находятся только в общем коде; FAT также переиспользует
его вместо собственной копии.  QEMU i686 подтверждает выбор `mc146818` и
загрузку с аппаратным UTC временем; Ci20 требует только финального gate на
реальной плате для чтения PCF8563 и проверки сохранения после выключения.

## Выполнено

Первый QEMU bring-up инкремент завершён 2026-07-25:

- добавлены `sys/i386` и GCC-only `target-i386.mk`;
- toolchain gate подтверждает `i686-elf`, GCC 14.2.0 и ELF32/i386;
- собираются `rebsd-i686.elf` и Linux boot-protocol 2.02 image
  `rebsd-i686.bzimg`;
- единственный штатный install/hardware-gate artifact —
  `rebsd-i686.bzimg`; default `all` не создаёт дополнительный image format;
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

- E820 RAM нормализуется в page-aligned диапазоны ниже `0x40000000`;
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

Пятый QEMU bring-up инкремент завершён:

- generic `sys/vm/vm_param.c`, `vm_phys.c` и `vm_page.c` реально входят в
  i686 kernel, без локальной копии allocator policy;
- `vm_phys_boot_map` строится из нормализованных E820 RAM ranges;
- страницы, уже занятые bootstrap allocator, и выделенная из RAM metadata
  помечаются `VM_PAGE_RESERVED`, а остальные переходят в buddy allocator;
- identity direct map используется для zero/poison/check освобождаемых
  страниц;
- существующий generic `vm_page_bootstrap_selftest` проверяет allocation,
  constrained allocation, free и poison;
- normal/trap smoke, RAM matrix 32/64/128/256 МиБ и полный host VM/MIPS
  test suite проходят.

Шестой QEMU bring-up инкремент завершён:

- добавлен постоянный kernel direct map
  `0xC0000000 + physical_address` для первого 1 ГиБ RAM;
- текущий non-PAE bootstrap сознательно использует только E820 RAM ниже
  1 ГиБ; это покрывает QEMU и начальный IBM/VIA gate, а highmem остаётся
  отдельной будущей задачей;
- реализован i386 backend всего публичного `sys/vm/pmap.h`: lifecycle,
  prepare/enter/device/remove/protect/extract, CR3 activation, fault access
  accounting, referenced/modified state, reverse page operations, direct
  map, statistics и validation;
- page-directory/page-table pages выделяются как `VM_PAGE_WIRED` из
  generic allocator и полностью возвращаются при destroy;
- process page directories наследуют low bootstrap mappings и kernel
  direct map; user PDE получает private copy при первом использовании;
- QEMU self-test переключает реальные CR3 между двумя address spaces,
  проверяет isolation, RO protection, mapping replacement, execution,
  low-PDE inheritance и отсутствие утечки allocator pages;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ и полный host
  VM/MIPS test suite проходят.

Седьмой QEMU bring-up инкремент завершён:

- ранний i386 image реально линкует generic `vm_map`, `vm_object`,
  `vm_shm`, `vm_sysv_shm`, `vmspace` и split access/fault translation
  units;
- добавлены минимальные i386 `bzero`/`bcopy`, необходимые generic kernel
  коду до подключения общего libkern;
- на раннем однопоточном диагностическом этапе swap ещё не
  инициализировался; anonymous pages и COW работали без имитации swap;
- MD activation регистрирует текущий vmspace, а i386 `#PF` сначала
  проверяет существующий pmap и затем вызывает `vmspace_fault_context`;
- QEMU self-test проверяет anonymous fault, kernel read/write access,
  clone+COW с разными physical pages, CR3 activation, protection,
  mincore/validation, полный reclaim и настоящий non-present page fault с
  возвратом через `iret`;
- deliberate kernel write-protection `#PF` по-прежнему не поглощается
  vmspace и завершается ожидаемым diagnostic panic;
- normal/trap smoke, расширенная RAM matrix и полный host VM/MIPS suite
  проходят; `BOARD=maltael kernel-objects` также компилируется после
  добавления ранних VM capability-флагов.

Восьмой QEMU bring-up инкремент завершён:

- добавлены публичные i386 `copyin/copyout` и neutral `vmspace_current`;
- copy path проверяет границы текущего user `vm_map`, включая overflow и
  пересечение верхней границы, и возвращает `EFAULT` без прямого
  разыменования user virtual address из ring 0;
- данные передаются через generic vmspace access и kernel direct map;
  demand paging и COW используют общий `VM_FAULT_COPY` path;
- отдельный QEMU self-test вызывает именно экспортируемые ABI symbols,
  переносит значение через две страницы, проверяет invalid ranges,
  read-only protection, двухстраничный COW и полный reclaim;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, host VM suite и
  `BOARD=maltael kernel-objects` проходят.

Следующая веха: добавить i386 u-area/kernel stack и первый context switch.
User-string copy выделяется в
явный API, потому что low-linked i386 не может использовать MIPS-эвристику
по адресу указателя.
IBM HDD gate уже выполнен существующим GRUB Legacy через Linux/x86 boot
protocol; установка другого boot loader не требуется.

Первый шаг process-MD gate также завершён: generic headers, init, fork,
scheduler и exit больше не ссылаются на `mips_curuser` или
`mips_uarea_*`. Нейтральный `md_*` контракт подключён к существующей MIPS
реализации; kernel-object сборка проходит для N64, CI20, Malta, MaltaEL и
Malta64. Следующий кодовый инкремент реализует этот контракт для i386.

Девятый QEMU bring-up инкремент завершён:

- добавлен i386 `machine/elf_machdep.h` для ELF32 little-endian, `EM_386` и
  базовых `R_386_*` relocations;
- generic ELF loader больше не содержит жёсткую проверку `EM_MIPS` и
  выбирает допустимую machine ID через `ELF_MACHDEP_ID_CASES`;
- user layout закреплён как `0x00400000..0x80000000`, согласован с текущим
  generic vmspace; `exec_elf.c` компилируется i686 GCC с `-Werror`;
- реализованы i386 `md_curuser`, u-area guard и allocation/free четырёх
  contiguous wired 4-КиБ страниц через generic allocator и direct map;
- `md_uarea_fork` создаёт независимую копию `struct user`, очищает context
  labels и не выдаёт её за schedulable до появления assembly context frame;
- QEMU self-test проверяет alignment, guard, fork-copy isolation, current
  binding и отсутствие утечки страниц; normal boot выдаёт
  `uarea-selftest: ok`.

Десятый QEMU bring-up инкремент завершён:

- добавлены i386 `setjmp/longjmp`, совместимые с существующим scheduler ABI;
- context label сохраняет EBX/ESI/EDI/EBP, kernel ESP/EIP и EFLAGS;
- `longjmp` меняет `md_curuser` вместе с переходом, а выбор CR3 остаётся за
  уже существующим `vmspace_activate` перед scheduler switch;
- assembly self-test проверяет восстановление callee-saved регистров;
- второй self-test переключается на настоящий отдельный 16-КиБ u-area
  stack в kernel direct map, входит в C-функцию и возвращается в исходный
  context;
- проверяются stack bounds, current-uarea binding, оба guard и точный
  reclaim wired pages;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, host VM suite и
  `kernel-objects` для N64, CI20, Malta, MaltaEL и Malta64 проходят.

Одиннадцатый QEMU bring-up инкремент завершён:

- `md_uarea_fork` для обычного fork валидирует и копирует родительский i386
  trapframe на новый kernel stack, возвращает ребёнку `EAX=0` и готовит
  `u_ssave` для `i386_fork_trampoline`;
- trampoline использует общий interrupt restore path и завершает переход
  через настоящий `iret`, поэтому scheduler и interrupt ABI не расходятся;
- bootstrap fork получает отдельный stack и `i386_init_trampoline` для
  входа в generic `md_init_process` без копирования живого bootstrap stack;
- scheduler-подобный QEMU self-test связывает две u-area с двумя
  `struct proc` и двумя COW vmspace, меняет CR3 перед `longjmp` и проверяет
  разные значения по одному VA до и после обратного switch;
- обязательный boot marker `fork-frame: ok` подтверждает child `EAX=0`,
  `md_curuser`, `u_procp`, stack bounds, COW/CR3 isolation и полный reclaim;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, host VM suite и
  `BOARD=maltael kernel-objects` проходят.

Двенадцатый QEMU bring-up инкремент завершён:

- setup GDT получил flat DPL3 code/data descriptors и зарезервированный
  32-bit TSS descriptor;
- runtime TSS инициализирует `ss0`, отключённую I/O bitmap, аварийный kernel
  stack, загружается через `ltr` и проверяется через `str`;
- i386 `longjmp` теперь обновляет TSS `esp0` вместе с `md_curuser`, чтобы
  следующий user trap вошёл на stack выбранного процесса;
- ring-3 self-test создаёт отдельные executable и stack mappings, входит в
  CPL3 через пятисловный `iret` frame и выполняет user `int3`;
- CPU реально переключается через TSS на u-area stack, общий handler
  сохраняет user SS/ESP, возвращается в user code, а тестовый `int 0x30`
  переводит frame обратно в ring 0;
- обязательные маркеры `tss: ok` и `ring3: ok` входят в normal/page smoke;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, параллельная
  чистая сборка, host VM suite и `BOARD=maltael kernel-objects` проходят.

Тринадцатый QEMU bring-up инкремент завершён:

- IDT vector `0x80` установлен как DPL3 32-bit interrupt gate и использует
  общий полный i386 trapframe;
- register ABI задаёт syscall number в `EAX`, шесть аргументов в
  `EBX/ECX/EDX/ESI/EDI/EBP` и два результата в `EAX/EDX`;
- BSD-style error convention возвращает положительный errno в `EAX` и
  устанавливает Carry; успешный возврат очищает Carry;
- CPL3 self-test сначала вызывает неизвестный номер и проверяет
  `ENOSYS+CF`, продолжая выполнение в user mode;
- затем тот же user stream передаёт шесть аргументов зарезервированному
  self-test syscall, проверяет их сумму, второй result register, сохранность
  остальных регистров и возврат через `iret`;
- обязательный marker `syscall-int80: ok` входит в normal/page smoke;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, параллельная
  чистая сборка, host VM suite и `BOARD=maltael kernel-objects` проходят.

Четырнадцатый QEMU bring-up инкремент завершён:

- публичный `i386_syscall_set_table` отделяет register ABI от конкретной
  `struct sysent` таблицы и готов принять production `sysent`;
- dispatcher проверяет syscall number и максимум шесть аргументов, заполняет
  `u_arg`, устанавливает `u_frame` и вызывает `sy_call` под `u_qsave`;
- `u_rval/u_rval2`, positive errno/Carry, `ERESTART` и `EJUSTRETURN`
  преобразуются обратно в i386 trapframe;
- расширенный CPL3 stream проверяет успех с шестью аргументами, `EACCES`,
  настоящий повтор двухбайтного `int 0x80`, неизменённый frame и
  `u_qsave/longjmp` с `EINTR`;
- неизвестный номер даёт `ENOSYS`, а запись `sysent` с семью аргументами —
  `EINVAL`;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, параллельная
  чистая сборка, host VM suite и `BOARD=maltael kernel-objects` проходят.

Пятнадцатый QEMU bring-up инкремент завершён:

- `sys/user.h` задаёт opaque MD API для exec register setup, ptrace write,
  смены user PC и single-step;
- `exec_subr.c` и `sys_process.c` больше не знают `FRAME_*`,
  `mips_frame_*`, `ST_RP` и не включают MIPS `machine/io.h`;
- MIPS backend сохраняет прежнюю семантику как для compact MaltaEL frame,
  так и для 64-битных GPR slots N64;
- i386 backend формирует user selectors и `EIP/ESP`, передаёт
  `argc/argv/envp` в `EBX/ECX/EDX`, безопасно фильтрует EFLAGS и включает
  x86 Trap Flag для `PT_STEP`;
- QEMU u-area self-test проверяет exec/ptrace frame contract;
- i386 normal smoke, host VM suite, MaltaEL и N64 `kernel-objects` проходят.

Шестнадцатый QEMU bring-up инкремент завершён:

- зафиксирован 32-битный i386 `sigcontext` со всеми user GPR, segment
  registers, `EIP/ESP/EFLAGS`, signal mask и alternate-stack state;
- `sendsig` строит cdecl frame из return trampoline, signal number, code и
  указателя на встроенный context, поддерживая обычный и alternate stack;
- `sigreturn` принимает context pointer первым аргументом `int 0x80`,
  проверяет user selectors и VM permissions, исключает `SIGKILL/SIGSTOP`
  из маски и фильтрует опасные EFLAGS;
- QEMU self-test проверяет layout frame, восстановление registers/mask,
  rejection kernel `CS`, alternate stack и точный reclaim allocator pages;
- обязательный marker `signal-frame: ok` входит в normal/page smoke.

Семнадцатый QEMU bring-up инкремент завершён:

- общая interrupt assembly-эпилога вызывает CPL3-only `i386_user_return`
  перед восстановлением registers и `iret`;
- user-return loop устанавливает `u_frame`, с разрешёнными interrupts
  повторяет MIPS semantics `CURSIG/postsig`, `setpri`, `setrq/swtch` и
  учитывает `ru_nivcsw`;
- PIC remap/mask перенесён сразу после IDT, поэтому разрешение IF до запуска
  PIT не может принять BIOS IRQ0 на exception vector 8;
- CPL3 QEMU stream получает pending `SIGUSR1`, исполняет реальный cdecl
  handler и trampoline, вызывает `sigreturn` через `int 0x80`, возобновляет
  исходный user EIP и проходит моделируемый reschedule;
- обязательный marker `user-return: ok` входит в normal/page smoke.

Восемнадцатый QEMU bring-up инкремент завершён:

- i386 CPU exceptions отображаются в BSD signals: `SIGFPE`, `SIGTRAP`,
  `SIGILL`, `SIGBUS` и `SIGSEGV` согласно классу vector;
- terminal CPL3 page fault после неуспешного pmap/vmspace resolution
  становится `SIGSEGV`, а kernel faults сохраняют diagnostic panic;
- NMI, double fault и machine check явно исключены из process delivery;
- QEMU CPL3 stream последовательно переживает реальный `UD2→SIGILL` и
  unmapped read `#PF→SIGSEGV`; handlers проверяют EIP/CR2 в `code`, правят
  `sigcontext` и дважды возвращаются через `int 0x80 sigreturn`;
- обязательный marker `user-trap: ok` входит в normal/page smoke.

Девятнадцатый QEMU bring-up инкремент завершён:

- исторический `copystr` разделён на явные `copyinstr` для user source и
  `copykstr` для kernel source; выбор больше не зависит от численного
  значения low-linked i386 указателя;
- `nameidata` хранит `NI_USERSPACE/NI_SYSSPACE`, а все kernel pathname
  источники в `exec_script`, `core` и MIPS socket glue явно используют
  `NDINIT_KERNEL`;
- `exec` отдельно отслеживает источник interpreter name/argument,
  исходного script name, `argv` и `envp`, поэтому длина и копирование
  строки не используют MIPS address heuristic;
- MIPS backend сохраняет совместимый `copystr` как kernel-string wrapper;
  заодно исправлен общий случай исчерпания `maxlength`, который теперь
  возвращает `ENOENT`, если NUL не найден;
- i386 QEMU self-test проверяет low-linked kernel string, user string через
  границу двух страниц, truncation и unmapped fault;
- normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ, host VM suite,
  MaltaEL и N64 `kernel-objects` проходят.

Двадцатый QEMU bring-up инкремент завершён:

- ранний image получил bootstrap prefix production `sysent` с номерами
  0–20 и точными argument counts из `kernel/init_sysent.c`; ещё не
  подключённые подсистемы явно возвращают `ENOSYS`;
- syscall 20 вызывает настоящий machine-independent
  `kernel/kern_prot.c:getpid`, а linker garbage collection не удерживает
  неиспользуемые функции того же generic object;
- production table устанавливается до syscall self-test; временные таблицы
  signal/trap тестов сохраняют и восстанавливают её;
- QEMU CPL3 stream выполняет `eax=20; int 0x80` с настоящим `struct proc`,
  получает PID 386, проверяет нулевой Carry и marker
  `syscall-production: ok`;
- `kern_prot.c` очищен от двух signedness warnings для строгой i686
  `-Werror` сборки без изменения MIPS-семантики;
- clean build, normal/trap smoke, RAM matrix 32/64/128/256/768/1024 МиБ,
  host VM suite, MaltaEL и N64 `kernel-objects` проходят.

Двадцать первый QEMU bring-up инкремент завершён:

- после разрушаемых self-tests создаётся постоянный proc0-совместимый
  bootstrap process с PID/PPID 0 и состоянием `SRUN|SLOAD|SSYS`;
- процесс получает собственные guarded 16-КиБ u-area и vmspace, а `u_procp`,
  `p_uarea`, `p_addr` и `p_vmspace` связаны в согласованное состояние;
- начальные `cmask`, groups и rlimits соответствуют generic proc0
  invariants, не вовлекая пока scheduler/process table;
- vmspace процесса становится активным CR3, `md_curuser` остаётся
  установленным, а `TSS.esp0` указывает на вершину его kernel stack до
  конца ранней загрузки;
- обязательный marker `process-bootstrap: ok` проверяет все связи и guard;
  timer IRQ и deliberate kernel faults проходят уже при существующем
  current process;
- clean build, normal/trap smoke и RAM matrix
  32/64/128/256/768/1024 МиБ проходят.

Двадцать второй QEMU bring-up инкремент завершён:

- в постоянном bootstrap vmspace отображаются минимальный user text от
  `0x00400000` и верхняя user stack page;
- код сначала записывается через generic vmspace API, затем защищается
  как read/execute; stack остаётся read/write без execute permission в
  VM policy (non-PAE Pentium III аппаратно не предоставляет NX);
- тот же proc0, u-area, CR3 и `TSS.esp0` реально входят в CPL3 и выполняют
  production `eax=20; int 0x80`;
- generic `getpid` возвращает PID 0 постоянного процесса с очищенным Carry,
  после чего тестовый return vector возвращает управление через его
  настоящий u-area kernel stack;
- обязательный marker `process-user: ok` подтверждает production sysent,
  VM protection policy, CPL3 frame и сохранность bootstrap invariants;
- normal/trap smoke и RAM matrix 32/64/128/256/768/1024 МиБ проходят.

Двадцать третий QEMU bring-up инкремент завершён; последующий общий аудит
удалил временный memory-image путь:

- минимальная user-программа теперь отдельно собирается GCC/binutils
  toolchain в настоящий `ET_EXEC` ELF32/i386 и устанавливается как
  `/sbin/init` в UFS;
- общий `sys/kernel/exec_elf_loader.c` валидирует ELF
  magic/class/data/ABI, target machine из `machine/elf_machdep.h`, header
  bounds, alignment, user ranges, entry point и неперекрывающиеся
  `PT_LOAD`; inode-backed `sys/kernel/exec_elf.c` читает сегменты напрямую
  из executable inode;
- loader отклоняет interpreter/dynamic и W+X segments, явно обнуляет BSS и
  применяет финальные `PF_R/PF_W/PF_X` permissions;
- `sys/i386/common/elf_bootstrap.c` и его приватный header удалены; i386
  вызывает production `execve`, а общий loader также собирается для
  существующих MIPS-конфигураций;
- ELF содержит отдельные RX text и RW data+BSS segments; CPL3
  код проверяет initialized data, нулевой BSS и запись в него до production
  `getpid`;
- entry point берётся из ELF header, а не из kernel-константы; обязательный
  marker `elf32-user: ok` подтверждает полный build/load/execute path;
- clean build, QEMU normal/trap smoke и RAM matrix
  32/64/128/256/768/1024 МиБ проходят с прежними
  `process-bootstrap: ok` и `process-user: ok`.

Двадцать четвёртый QEMU bring-up инкремент первоначально использовал
временный i386 stack builder; после общего аудита он удалён:

- i386 bootstrap заполняет общий `struct exec_params` и вызывает
  `sys/kernel/exec_subr.c::exec_setupstack`, который формирует layout:
  reserved argument slots, `argv[]`, `envp[]`, packed strings и верхнее
  слово `argv` для `/bin/ps`, с 8-байтным stack alignment;
- все размеры, pointer arrays и границы stack mapping проверяются до записи,
  после чего страница обнуляется и заполняется только через generic
  vmspace API;
- новый `i386_user_enter_exec` передаёт `argc` в EBX, `argv` в ECX и
  `envp` в EDX, как уже требует `md_user_frame_exec`, и входит с IF=1;
- bootstrap ELF проверяет из CPL3 `argc=2`, строки `init`/`elf`,
  environment `A=i686`, NULL terminators, alignment, reserved slot и
  верхнее `argv`-слово до проверки data/BSS и production syscall;
- persistent proc получает `p_saddr/p_ssize` и u-area `u_ssize`,
  соответствующие реальному stack mapping; обязательный marker
  `user-stack: ok` подтверждает весь ABI path;
- clean build, обычный QEMU smoke, trap smoke для #DE/#GP/#PF, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Двадцать пятый QEMU bring-up инкремент завершён:

- детерминированный read-only i386 initfs упаковывает отдельно связанный
  ELF32 как именованный `/sbin/init`; формат использует фиксированный
  little-endian header и directory entries;
- ранний kernel parser проверяет magic/version, число записей, полный
  размер, path/data ranges, NUL termination, 16-byte data alignment и
  неоднозначный duplicate match до возврата файла;
- bootstrap path требует `ENOEXEC` для malformed archive, `ENOENT` для
  `/missing`, затем передаёт найденный `/sbin/init` обычному ELF32 loader;
- CPL3 image проверяет `argv[0]=/sbin/init` и `argv[1]=initfs`, так что
  `initfs: ok` подтверждает named lookup, ELF mappings, exec stack и
  production syscall одним сквозным прогоном;
- `make initfs-smoke` повторно создаёт архив и сравнивает его byte-for-byte;
- clean build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Двадцать шестой QEMU bring-up инкремент завершён:

- i386 board config предоставляет generic `proc[NPROC]` и `nproc`, а early
  kernel впервые линкует общий `kern_proc.c`;
- `pqinit` формирует штатные `allproc/freeproc/zombproc`; ранний init
  занимает `proc[1]`, получает PID 1/parent proc0, входит в `allproc` и
  `pidhash`, а следующим свободным остаётся `proc[2]`;
- process 1 владеет активными u-area, vmspace, CR3 и TSS.esp0 и, в отличие
  от зарезервированного proc0, не имеет флага `SSYS`;
- CPL3 `/sbin/init` проверяет, что production syscall 20 действительно
  вернул PID 1; kernel return path проверяет тот же EAX;
- marker `process-table: ok` валидирует list order/back-links, free/zombie
  queues, proc0 metadata, PID hash и `pfind(1)`;
- clean build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Двадцать седьмой QEMU bring-up инкремент завершён:

- proc0 получил собственные guarded u-area и kernel-only vmspace; его
  `p_uarea/p_addr/p_vmspace` теперь являются постоянными process-table
  ресурсами;
- начальный proc0 `u_qsave` запускает idle entry на отдельном u-area stack,
  после чего `setjmp` сохраняет continuation в формате штатного scheduler
  ABI;
- возврат `/sbin/init` из CPL3 происходит на process 1 kernel stack и
  выполняет два proc1→proc0→proc1 round-trip: первый через начальный
  context, второй через уже сохранённый proc0 `u_qsave`;
- на каждом переходе QEMU проверяет CR3/vmspace, `md_curuser`, `u_procp`,
  границы kernel stack, u-area guards и переключение `TSS.esp0`;
- обязательный marker `proc0-context: ok` появляется только после обоих
  переключений и повторной валидации активного process 1;
- clean build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Generic run queue/`swtch` и `newproc` ещё не входят в early image.

Двадцать восьмой QEMU bring-up инкремент завершён:

- early image впервые линкует общий `kern_synch.c`; его strong
  `setpri/setrq/swtch` заменяют ранние weak fallback symbols;
- i386 `splhigh/splx` сохраняют и восстанавливают EFLAGS.IF, а idle hook
  использует атомарный `sti; hlt` и ранний fail-stop panic;
- после возврата `/sbin/init` process 1 дважды ставится в generic `qs`;
  proc0 запускает штатный `swtch`, выбирает PID 1 по priority/SLOAD и
  возвращает его через `u_rsave`;
- первый проход строит настоящий proc0 `u_qsave` внутри generic scheduler,
  второй возобновляет тот же сохранённый context; тест проверяет, что label
  не меняется, а `qs` после выбора пуст;
- i386 `vmspace_current`, как MIPS backend, теперь сначала выводит vmspace
  из `md_curuser->u_procp`, поэтому generic `vmspace_activate` согласован с
  диагностикой CR3/current process;
- marker `scheduler-switch: ok` дополняет `proc0-context: ok` и подтверждает
  два полных generic scheduler round-trip;
- clean build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

`newproc` и fork нескольких живых процессов ещё не входят в early image.

Следующий инкремент: подключить generic `newproc`, создать process 2 через
существующие `vmspace_clone/md_uarea_fork` и выполнить parent/child scheduler
round-trip перед расширением syscall table.

Двадцать девятый QEMU bring-up инкремент завершён:

- early image линкует общий `kern_fork.c` и вызывает настоящий `newproc(0)`
  для process 1 после его первого trap из `/sbin/init`;
- `newproc` выдаёт PID 2, вставляет child в `allproc`/PID hash, клонирует
  vmspace и u-area и ставит новый process в generic `qs` с `SSWAP`;
- proc0 выбирает child, `i386_fork_trampoline` возвращает скопированный
  trapframe в CPL3 с `eax=0`, после чего child отправляет отдельный magic
  trap;
- child останавливается в сохранённом kernel continuation, ставит parent в
  run queue, а proc0 возвращает управление process 1; проверяются отдельные
  CR3/vmspace/u-area, user frame, TSS.esp0, parent links, PID lookup и
  опустошение run queue;
- marker `process-fork: ok` подтверждает полный маршрут
  proc1→proc0→proc2(CPL3)→proc0→proc1;
- clean strict build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Production syscall 2, `exit`/`wait` и повторное пробуждение остановленного
process 2 ещё не подключены.

Следующий инкремент: включить generic `fork` в production syscall table и
замкнуть минимальный child `exit`/parent `wait` lifecycle.

Тридцатый QEMU bring-up инкремент завершён:

- production syscall table entry 2 теперь вызывает generic `fork`, а
  installation gate проверяет одновременно `fork` и `getpid`;
- `/sbin/init` вызывает `fork` через настоящий `int 0x80`: parent получает
  PID 2, child возобновляет клонированный syscall trapframe с `eax=0`;
- прежний внутренний вызов `newproc` из process probe удалён: создание PID 2
  теперь происходит исключительно через production syscall dispatch;
- marker `syscall-fork: ok` отделяет проверку syscall ABI от
  `process-fork: ok`, проверяющего scheduler/CR3/u-area round-trip;
- early weak `log` выводит диагностическое сообщение на COM1/VGA до
  подключения общего kernel log;
- clean strict build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, host VM tests и объектные сборки MaltaEL/N64
  проходят.

Generic `kern_exit.c` пока не входит в early image: полный handler требует
VFS descriptor/inode teardown, resource accounting и signal delivery.

Следующий инкремент: отделить нейтральное освобождение
vmspace/u-area/proc-table от VFS teardown и на этой основе включить child
`exit` и parent `wait4`.

Тридцать первый QEMU bring-up инкремент завершён:

- filesystem-independent `proc_zombify` удаляет завершившийся process из
  PID hash/`allproc`, сохраняет wait status и переносит его в `zombproc`;
- `proc_reap` на стороне родителя освобождает отдельные child vmspace и
  u-area, удаляет zombie и возвращает полностью очищенный `proc[2]` в
  `freeproc`;
- production syscall entries 1 и 7 вызывают ранние i386 `exit` и `wait4`;
  ограниченный `wait4(-1, status, 0, 0)` усыпляет proc1 и возобновляется
  после пробуждения ребёнком;
- `/sbin/init` после production `fork` сообщает kernel о готовом parent,
  вызывает `wait4`, а child выполняет `exit(42)`; parent получает PID 2 и
  encoded status `42 << 8`;
- полный маршрут проходит через generic run queue и `swtch`:
  proc1(wait)→proc0→proc2(exit)→proc0→proc1(reap), без тестовой остановки
  или внутреннего вызова `newproc`;
- markers `syscall-exit: ok`, `syscall-wait4: ok` и `process-reap: ok`
  подтверждают syscall ABI, wakeup и возврат всех child VM/process
  ресурсов;
- clean strict build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, deterministic initfs, host VM tests и
  объектные сборки MaltaEL/N64 проходят.

Полный `kern_exit.c` всё ещё не подключён: VFS teardown, file descriptors,
resource accounting, orphan reparenting, signals и расширенные варианты
`wait4` остаются следующей интеграционной ступенью.

Следующий инкремент: обобщить ранний lifecycle до полного generic
exit/wait semantics либо подключить минимальный VFS/file layer, сохранив
чистую границу с i386 MD-кодом.

Тридцать второй QEMU bring-up инкремент завершён:

- ранний production `wait4` принимает `WAIT_ANY` или конкретный
  положительный PID и поддерживает `WNOHANG`, по-прежнему отклоняя
  неподдержанные options и ненулевой `rusage`;
- двухфазные `proc_waitable`/`proc_reap` отделяют поиск zombie от
  освобождения ресурсов; status копируется в parent vmspace до изменения
  очередей, поэтому `EFAULT` оставляет ребёнка доступным повторному wait;
- первый child PID 2 сначала остаётся runnable, пока
  `wait4(2, status, WNOHANG)` возвращает 0 и не меняет status, затем
  blocking `wait4` усыпляет proc1 и получает `exit(42)`;
- освобождённый `proc[2]` повторно используется вторым fork уже как PID 3;
  контролируемый scheduler handoff даёт ребёнку выполнить `exit(43)` до
  parent wait, проверяя отдельный путь готового zombie;
- parent сначала получает ожидаемый `EFAULT` с неверным status pointer,
  затем успешно повторяет `wait4(3, status, 0)` и возвращает `proc[2]` в
  начало `freeproc`;
- markers `wait4-nohang: ok`, `wait4-zombie: ok` и `wait4-efault: ok`
  делают три режима обязательными для normal/page QEMU smoke;
- clean strict build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ, deterministic initfs, host VM tests и
  объектные сборки MaltaEL/N64 проходят.

Следующий инкремент: добавить stop/continue semantics и resource accounting
к wait lifecycle либо перейти к минимальному file/VFS слою, необходимому
для production exec и дискового root.

Тридцать третий QEMU bring-up инкремент завершён:

- i386 port-I/O API дополнен 32-битными `inl/outl`, а раннее ядро проверяет
  legacy PCI configuration mechanism #1 через `0xCF8/0xCFC`;
- read-only enumerator сканирует все 256 buses, 32 devices и
  multifunction functions, читает vendor/product, class/subclass и
  programming interface без ACPI или BIOS services;
- inventory находит host bridge, ISA bridge, IDE controller и VGA по
  стандартным class codes, поэтому тот же код применим к QEMU PIIX и
  VIA Apollo Pro 133/596B, не привязывая IDE path к одному product ID;
- QEMU `pc-i440fx-9.2` обнаруживает Intel `8086:1237` host,
  `8086:7000` ISA и `8086:7010` IDE, а также VGA `1234:1111`;
- VIA vendor `0x1106` получает отдельную platform classification для
  будущего serial hardware log IBM 6563-W4G; AGP/VGA devices на secondary
  bus также попадают в полный bus scan;
- markers `pci: mechanism=1`, `pci-host`, `pci-isa`, `pci-ide` и
  `pci-platform: intel` обязательны для QEMU normal/page smoke;
- clean strict build, normal/trap QEMU smoke, RAM matrix
  32/64/128/256/768/1024 МиБ и deterministic initfs проходят.

Следующий инкремент: построить поверх найденного IDE function безопасный
legacy compatibility-mode PIO IDENTIFY/read-only probe, сначала в QEMU,
затем проверить PCI IDs и register mode по serial log IBM/VIA.

Тридцать четвёртый QEMU bring-up инкремент завершён:

- i386 port-I/O API дополнен 16-битным `inw`, а новый ранний ATA backend
  работает через primary compatibility ports `0x1F0`/`0x3F6`;
- probe явно выбирает primary master до первой проверки status: QEMU trace
  подтвердил реальный BIOS-сценарий, где SeaBIOS оставляет выбранным
  отсутствующий slave;
- backend посылает только `IDENTIFY DEVICE` и односекторный
  `READ SECTORS` для LBA0; команд записи, DMA и включения IRQ в нём нет;
- `IDENTIFY` печатает model и LBA28 capacity, проверяет LBA capability,
  отличает ATA от ATAPI и имеет ограниченные polling timeouts вместо
  бесконечного ожидания;
- детерминированный raw image имеет 4096 секторов, marker `REBSDIDE` и MBR
  signature; QEMU подключает его через временный `snapshot=on`, не меняя
  базовый image, а smoke требует
  `ide-primary-master: ata`, `ide-sectors: 0x00001000`, успешное чтение
  LBA0, marker и `0x55AA`;
- отдельный no-disk smoke подтверждает, что отсутствие primary master
  диагностируется и не мешает ядру дойти до timer gate и `HALT`;
- normal/page smoke теперь проверяют IDE путь вместе с PCI inventory;
  image generator имеет отдельную deterministic comparison; gate проходит
  на QEMU `pc-i440fx-9.2` и самом старом доступном профиле `5.1`.

Следующий инкремент: отделить reusable read-only PATA transfer API от
bootstrap probe и подключить MBR parser/дисковый backend без записи на
носитель. Проверка на IBM 6563-W4G пока не требуется: hardware gate будет
запрошен после завершения QEMU-only разборки partition table и
контролируемого чтения нескольких LBA.

Тридцать пятый QEMU bring-up инкремент завершён:

- PATA transport реализует точный generic `struct disk_backend_ops`
  read contract; `dbo_write` и `dbo_flush` оставлены `NULL`, а capacity и
  present state доступны будущему `disk_attach`;
- backend принимает 64-битный generic LBA, проверяет его против LBA28
  capacity, ограничивает один запрос 128 секторами и разбивает его на
  безопасные односекторные polling-команды;
- i686 image теперь линкует существующий machine-independent
  `sys/disk/disk_subr.c`, поэтому MBR signature, media bounds, partition
  table и minor-to-region contract не дублируются в MD-коде;
- deterministic image содержит активный MBR partition типа `0xB7`:
  start LBA 64, length 4032; отдельные markers лежат в первых двух и
  последнем секторах partition;
- QEMU gate читает LBA0, два последовательных partition sectors и последний
  LBA, подтверждает таблицу через generic parser и отдельно проверяет отказ
  для LBA за концом media, слишком большого/нулевого request и `NULL` buffer;
- обязательные markers фиксируют generic backend read, MBR type/start/size,
  multi-LBA partition read, last-LBA read и transport bounds.

Следующий инкремент: подключить read-only ATA ops к generic `disk_attach`
и провести запросы whole-disk/partition через block-layer region contract.
ATA-команды записи всё ещё не добавляются. Реальный IBM hardware gate будет
нужен после этого QEMU-этапа и подготовки serial-only test image.

Тридцать шестой QEMU bring-up инкремент завершён:

- полный generic `sys/disk/disk.c` теперь входит в i686 image; primary ATA
  master регистрируется через `disk_attach` с `DISK_FLAG_READ_ONLY`;
- ранние synchronous strategy completion и quiet logging adapters локальны
  только для i686 bootstrap object через переименование symbols при сборке,
  не выдавая их за готовые общесистемные `biodone`/`printf`;
- `disk_bdev_open` на partition разрешает `FREAD`, но возвращает `EROFS`
  для `FWRITE`; write `struct buf` также завершается `B_ERROR/EROFS` до
  обращения к отсутствующему `dbo_write`;
- настоящий `disk_bdev_strategy` переводит partition-relative `B_PHYS`
  request в ATA LBA, читает два sector markers, корректно возвращает EOF
  на точной границе partition и закрывает read-only device;
- generic disk cache dimensions получили build-time overrides при
  неизменных defaults; ранний i686 профиль использует один 16-sector read
  slot и один 32-sector write slot, снизив kernel BSS с 855964 до
  224988 байт; write-back для ATA device остаётся выключен;
- обязательные QEMU markers фиксируют attach, partition metadata,
  strategy read/EOF, оба `EROFS` guard и close; no-disk boot по-прежнему
  пропускает attach и доходит до `HALT`.

Следующий инкремент: подготовить serial-only BIOS boot test artifact и
инструкцию безопасного первого hardware gate. До появления воспроизводимого
BIOS-носителя IBM 6563-W4G включать не требуется.

Тридцать седьмой QEMU bring-up инкремент завершён:

- первый сектор `rebsd-i686.bzimg` теперь является настоящим legacy BIOS
  boot sector, не нарушая Linux/x86 protocol entry в offset `0x200`;
- BIOS path переносит boot params/setup в `0x90000`, через CHS `INT 13h`
  дочитывает kernel в ограниченный staging range `0x10000..0x7ffff`,
  затем в protected mode копирует payload на `0x00100000`;
- LILO для первого hardware gate больше не нужен: детерминированный raw
  `rebsd-i686-bios-floppy.img` имеет ровно 1.44 MB, проверяемые boot
  signature/`HdrS`/size limits и SHA-256;
- kernel различает `boot-loader: linux-protocol` и
  `boot-loader: bios-int13`, поэтому smoke доказывает фактический путь
  загрузки, а не только одинаковый поздний banner;
- native BIOS boot прошёл на QEMU `pc-i440fx-9.2` и `pc-i440fx-5.1`, с
  отдельным read-only IDE smoke disk и вообще без IDE-диска; оба варианта
  доходят до `HALT`;
- безопасный первый IBM gate описан в `docs/I686_HARDWARE_GATE.md`:
  сначала floppy boot при физически отсоединённых IDE drives, COM1
  `115200 8N1`, полный serial log и ожидаемый `pci-platform: via`.

Запланированный floppy gate был заменён загрузкой `rebsd-i686.bzimg` через
уже установленный на IDE-CF GRUB Legacy: у конкретного IBM нет floppy
drive. Перед gate сохранена рекомендация иметь резервную копию CF.

Тридцать восьмой bring-up инкремент завершён на реальном IBM 6563-W4G:

- существующий GRUB Legacy с CF загрузил `rebsd-i686.bzimg` через
  Linux/x86 boot protocol без LILO и без floppy drive;
- VGA text console стабильно показала полный поздний boot и штатный
  `HALT`; PIC и PIT дали десять timer ticks при `HZ=100`;
- primary IDE-CF прошёл `IDENTIFY`, LBA28 и bounded read-only backend
  checks; MBR корректен, partition 0 имеет type `0x0c`, start LBA `0x800`
  и `0x1dcc059` sectors;
- данные на CF классифицированы как `external`; partition-relative read,
  last-LBA и exact-EOF checks прошли;
- generic disk attach подтвердил `read-only`, а open/write strategy
  независимо вернули `EROFS`; аппаратный gate не выполнил ATA write;
- поскольку начальные PCI/VIA строки ушли за верх VGA screen, следующий
  image повторяет host/ISA/IDE/VGA IDs и platform непосредственно перед
  `HALT` как компактный `hardware-summary`.

Тридцать девятый bring-up инкремент завершён вторым IBM gate:

- финальный VGA `hardware-summary` подтвердил VIA host `1106:0691`,
  ISA southbridge `1106:0596` и IDE function `1106:0571`;
- AGP VGA идентифицирован как 3Dfx Voodoo3 `121a:0005`;
- официальная [PCI ID database](https://pci-ids.ucw.cz/) сопоставляет chipset с
  VT82C693A/694x Apollo PRO133x, VT82C596 и VIA PIPC Bus Master IDE;
- повторный реальный boot сохранил все read-only/`EROFS`, PIC/PIT и
  `HALT` результаты первого gate.

Точный IBM hardware baseline зафиксирован. Дальнейшая разработка остаётся
QEMU-first и не добавляет ATA writes.

### Отменённая ветка инкрементов 40–45

Ниже сохранён только журнал ошибочной FAT-root реализации. Инкременты 40–45
отменены: они не задают текущую архитектуру, требования или тестовые gates.
Созданные тогда i386 FAT image generator, FAT-root выбор, whole-file
`/sbin/init` buffer, локальные descriptor/process части и связанные QEMU
targets удалены. Возвращать любой из этих путей без предварительного
явного согласования запрещено.

Сороковой QEMU bring-up инкремент завершён:

- deterministic IDE image проверяет FAT16/FAT32 через существующий общий
  block/FAT/VFS стек, без отдельного i386 filesystem reader;
- deterministic IDE image увеличен до 8192 секторов и теперь содержит
  настоящий MBR partition type `0x06`: start LBA 64, length 8128, FAT16,
  каталог `/BOOT` и 700-байтный `/BOOT/ROOT.TXT` через два кластера;
- FAT sectors читаются через common buffer cache и
  `disk_bdev_strategy` на partition minor; mount, lookup и file read
  проходят через generic disk bounds до legacy PIO ATA backend;
- QEMU требует успешный read-only `fat_vfsops` mount, `namei` lookup и
  чтение через общий VFS;
- host FAT tests продолжают проверять общий FAT-код на FAT16 и FAT32;
- ATA write commands по-прежнему отсутствуют; `FWRITE` open и block write
  strategy всё ещё независимо возвращают `EROFS`;
- direct Linux-protocol boot, native BIOS boot и оба no-IDE варианта
  проходят QEMU до `HALT`.

Сорок первый QEMU bring-up инкремент завершён:

- `mkide.py` теперь независимо и детерминированно строит FAT16 и FAT32
  images; новый FAT32 disk имеет 131072 секторов, MBR type `0x0c`, start
  LBA `0x800` и length `0x1f800`, как у обнаруженного IBM partition;
- FAT32 volume содержит BPB, FSInfo, backup boot/FSInfo, две одинаковые
  FAT copies, root cluster, `/BOOT` и тот же двухкластерный
  `/BOOT/ROOT.TXT`;
- локальный `/sbin/fsck_msdos -n` проходит обе извлечённые partitions без
  исправлений, orphan clusters или directory warnings;
- IDE и generic disk diagnostics различают FAT16/FAT32 без привязки
  filesystem reader к QEMU PIIX PCI ID;
- `fat32-boot-smoke` и `bios-fat32-boot-smoke` проходят соответственно
  Linux protocol и native BIOS handoff, требуют настоящий FAT32 mount,
  lookup, cross-cluster read, EOF/`ENOENT`/`EISDIR`, оба `EROFS` gate,
  timer и `HALT`;
- весь тест выполняется через legacy PIO `READ SECTORS`; ATA writes, DMA и
  IRQ mode не включались.

Сорок второй QEMU bring-up инкремент завершён:

- оба deterministic FAT image теперь получают отдельно связанный
  `bootstrap-user.elf` как короткое имя `/SBIN/INIT`; 8656-байтный ELF
  занимает длинную цепочку кластеров, а `mkide.py --file
  /sbin/init=bootstrap-user.elf` остаётся byte-for-byte воспроизводимым;
- root bootstrap ищет `/sbin/init` через общий `namei`, проверяет тип и
  ненулевой размер, ограничивает ранний read-only буфер 64 КиБ и читает
  файл через `fat_vfsops` → common buffer cache →
  partition-relative `disk_bdev_strategy` → PIO ATA `READ SECTORS`;
- PCI/IDE/disk bootstrap выполняется после создания process 1, но до его
  первого exec; найденный FAT image передаётся без отдельного упрощённого
  parser в уже существующий ELF32 loader и действительно выполняется в
  CPL3, включая production `getpid`, два `fork`, `exit` и `wait4`;
- QEMU требует последовательность `fat-init-lookup: ok`,
  `fat-init-read: ok`, `process-image: fat` и затем все прежние
  `process-user`/fork/scheduler markers;
- direct и native-BIOS no-disk gates требуют
  `process-image: initfs`, подтверждая безопасный fallback на встроенный
  `/sbin/init`;
- системный `fsck_msdos -n` принимает обе извлечённые partitions с
  `/BOOT/ROOT.TXT` и `/SBIN/INIT` без исправлений, orphan clusters или
  directory warnings;
- прошли host FAT/disk tests, direct и BIOS FAT16/FAT32 boots, no-disk
  fallback, три exception gate, RAM matrix 32–1024 МиБ и старый
  `pc-i440fx-5.1`.

Следующий инкремент также остаётся QEMU-only: подключить read-only FAT root
к обычному VFS/namei пути, чтобы `/sbin/init` открывался через системный
filesystem interface, сохранив initfs fallback. Повторный IBM запуск пока
не требуется; ATA writes, DMA и IRQ mode по-прежнему не включены.

Сорок третий QEMU bring-up инкремент завершён:

- подтверждено, что Ci20 USB mass storage и i686 IDE уже сходятся в одном
  интерфейсе: оба регистрируют generic disk как block major 2, а FAT не
  зависит от нижнего транспорта;
- i686 подключил общий buffer cache, inode/name cache, `fat_vfsops` и
  `namei`; IDE backend остался прежним синхронным read-only PIO backend;
- FAT partition монтируется как постоянный read-only root. Диагностический
  open закрывается, а отдельная VFS-ссылка сохраняет block device открытым;
- `/sbin/init` открывается общим `namei` и читается через
  `fat_vfsops.vfs_rwip`; отдельной ранней FAT-реализации нет;
- QEMU требует `vfs-root: fat,read-only`, `vfs-namei-init: ok`,
  `vfs-read-init: ok` и `process-image: fat-vfs`;
- прошли direct Linux-protocol и native BIOS boots с FAT16/FAT32, а также
  оба no-IDE fallback gate с `process-image: initfs`;
- отдельные direct/BIOS FAT32 gates монтируют исправный раздел без ReBSD
  `/sbin/init` и также требуют initfs fallback; это покрывает существующий
  IBM CF без необходимости немедленного hardware retest.

Следующий инкремент остаётся QEMU-only: расширить root path от bootstrap
чтения init к обычным file-descriptor syscalls, сохраняя read-only policy.
Повторный IBM запуск пока не требуется; ATA writes, DMA и IRQ mode не
включены.

Инвариант дальнейшего порта: `sys/i386` содержит только hardware/ABI glue.
Общие disk, buffer-cache, VFS, filesystem и descriptor подсистемы
переиспользуются из ReBSD/Ci20; локальные копии этих подсистем запрещены.

Сорок четвёртый QEMU bring-up инкремент завершён:

- production syscall prefix теперь обслуживает стандартные номера
  `read(3)`, `open(5)`, `close(6)` и `lseek(19)`; оставшиеся файловые
  syscalls пока возвращают `ENOSYS`;
- i686 получил минимальную совместимую `file[NFILE]`/descriptor
  реализацию. Пользовательский pathname проходит через `copyinstr` и общий
  `namei`, а чтение regular inode — через `fat_vfsops.vfs_rwip` и
  `copyout` активного process vmspace;
- read-only policy проверяется на syscall-границе: запись, создание и
  truncation возвращают `EROFS`, directory read возвращает `EISDIR`, а
  неверный или уже закрытый descriptor — `EBADF`;
- выполняемый в CPL3 `/sbin/init` открывает собственный FAT-файл, читает
  и проверяет `0x7fELF`, делает `lseek` на offset 1, повторно читает
  `ELF`, закрывает descriptor и проверяет `EBADF`/`EROFS`;
- после checkpoint kernel требует пустые process descriptor slots и
  нулевые `file` reference counts, затем печатает `syscall-open: ok`,
  `syscall-read: ok`, `syscall-lseek: ok`, `syscall-close: ok` и
  `fd-fat-vfs: ok`;
- direct/native-BIOS FAT16 и FAT32 gates прошли. No-IDE и смонтированный
  FAT32 без `/sbin/init` корректно выполняют initfs fallback и пропускают
  только storage-backed fd probe.

Следующий QEMU-only инкремент: проверить разделяемый file offset/reference
count через `fork`, закрывать унаследованные descriptors в раннем `exit` и
после этого сближать bootstrap fd-код с общими
`kern_descrip`/`sys_generic`/`sys_inode`. Повторный IBM запуск пока не
требуется; ATA writes, DMA и IRQ mode не включены.

Сорок пятый QEMU bring-up инкремент завершён:

- FAT descriptor остаётся открытым во время первого production `fork`;
  generic `newproc` копирует descriptor table и повышает reference count
  общего `struct file` с 1 до 2;
- kernel checkpoint требует, чтобы parent и child u-area ссылались на один
  file object с offset 4 и refcount 2;
- child читает следующие четыре ELF identification bytes через унаследованный
  fd 0, проверяет `01 01 01 00` и сдвигает общий file offset до 8;
- ранний `rexit` теперь закрывает все descriptors текущего process до
  `proc_zombify`; child снимает свою ссылку, не освобождая используемый
  parent inode;
- после `wait4` parent получает offset 8 через `lseek(..., L_INCR)`, закрывает
  последнюю ссылку и проходит прежние `EBADF`/`EROFS` проверки. Kernel
  требует пустые descriptor/file tables после reap;
- QEMU требует `fd-fork-shared-offset: ok` и `fd-exit-close: ok`; direct,
  native BIOS, FAT16/FAT32, no-IDE, no-init fallback, exception gates и
  старая `pc-i440fx-5.1` прошли.

Следующий QEMU-only инкремент: добавить `dup`/`dup2` и close-on-exec
семантику либо заменить соответствующие части bootstrap adapter общими
`kern_descrip`/`sys_generic`/`sys_inode`, не включая запись на диск.
Повторный IBM запуск пока не требуется.

На этом отменённая ветка заканчивается.

Сорок шестой cleanup-инкремент начал возврат к общим владельцам:

- приватные `common/initfs.c`, `include/initfs.h` и `tools/mkinitfs.py`
  удалены;
- существующий `tools/fsutil` создаёт детерминированный little-endian UFS с
  `/sbin/init`, а общий `sys/disk/romdisk` предоставляет image как read-only
  block major 0 minor 0;
- UFS использует общие disk, buffer cache, `vfs_mountroot`, inode/name cache,
  `namei`, `rdwri` и descriptor paths;
- локальная подмена `biodone` удалена. Она не освобождала common read-ahead
  buffer и зависала при чтении UFS; теперь используется `ufs_bio.c::biodone`;
- compile-time подмены `printf`/`log` на пустые i386 adapters удалены.
  Подключены общие `subr_prf`, TTY и clist owners; после общего console
  cleanup i386 реализует только COM1/VGA poll/getc/putc/winsize hooks и MD
  halt, а console cdev/TTY path принадлежит `sys/kernel/cons.c`;
- `rootfs-smoke` проверяет UFS через `fsutil --check` и повторную
  byte-for-byte сборку. QEMU markers унифицированы как `process-image: vfs`,
  `fd-vfs: ok` и `rootfs: ok`.

Ссылки на initfs и FAT-root в отменённом журнале выше описывают удалённое
историческое состояние и не являются текущей архитектурой порта.

Сорок седьмой cleanup-инкремент начал замену ручного process startup:

- общий `kern_proc.c::proc0_bootstrap` теперь единолично создаёт proc0
  vmspace, связывает u-area, задаёт rlimits и signal state и инициализирует
  process queues;
- обычный `init_main.c` для MIPS/N64/Ci20 и ранний i386 diagnostic path
  вызывают один и тот же owner;
- ручное создание proc1 и weak fail-stop `md_init_process` пока остаются
  открытыми пунктами аудита и не считаются завершёнными.

Сорок восьмой cleanup-инкремент удалил точечный режим syscall-совместимости:

- `REBSD_SYSCALL_BOOTSTRAP` и условный syscall prefix удалены; i686 собирает
  полную общую таблицу `sys/kernel/init_sysent.c` 0–177;
- в i686 image подключены настоящие общие exec, VM, SysV SHM и sysctl
  обработчики, требуемые полной таблицей, без alias, подавления предупреждений
  и архитектурных копий;
- `ct_ticks`, `pipedev`, VM accounting storage и генератор `vers.c` перенесены
  к общим архитектурно-нейтральным владельцам; MIPS-платы используют те же
  владельцы;
- строгая i686 GCC-сборка, полная QEMU-матрица, host disk/VM tests и
  `kernel-objects` для Ci20/N64 с GCC прошли. PCC не запускался и не менялся.

Сорок девятый cleanup-инкремент завершил удаление FAT-root самодеятельности
и общего ELF-дублирования:

- `sys/i386/tools/mkide.py`, i386 FAT objects/flags, FAT-root selection,
  filesystem-content parsing в ATA/disk bootstrap и FAT-specific QEMU targets
  удалены;
- один и тот же `rootfs.img`, созданный существующим `tools/fsutil`,
  встраивается как raw read-only UFS romdisk независимо от наличия IDE;
- `/sbin/init` разрешается общим `namei` и выполняется production `execve`;
  `sys/kernel/exec_elf.c` читает сегменты прямо из inode, без i386 whole-file
  buffer;
- ELF32 validation/mapping принадлежит общему
  `sys/kernel/exec_elf_loader.c`; MIPS, N64 и i686 используют этот owner.
  MIPS/N64 linker scripts формируют отдельные RX/RW `PT_LOAD`, поэтому общий
  loader не требует RWX;
- i386 page-fault path получает активный vmspace через общий
  `vmspace_current()`, устраняя устаревшее локальное состояние после
  `exec`/scheduler switch;
- прошли строгая i686 GCC-сборка, direct и BIOS QEMU boot, оба no-IDE gate,
  rootfs/bios-image smoke, exception gates, RAM matrix 32–1024 МиБ, host
  disk/VM tests и GCC `kernel-objects` для Ci20/N64. PCC не запускался и не
  менялся.

Следующий QEMU-only cleanup на этом историческом этапе был заменой ручного
proc1/startup path на владельца в common `init_main` без добавления i386
process policy. Реальное IBM-тестирование для этого не требовалось.

Пятидесятый cleanup-инкремент завершил замену ручного proc1 startup:

- process-1 trampoline вынесен из `init_main.c` в общий
  `sys/kernel/init_process.c`; MIPS, N64, Ci20 и i686 линкуют один owner;
- i686 больше не снимает proc1 с `freeproc`, не правит PID hash, не создаёт
  для него u-area/vmspace вручную и не вызывает `execve` из MD C-кода;
  proc1 создаётся общим `newproc`;
- общий `init_process` отображает стандартный `icode`, а i386 icode вызывает
  production syscall 11 `execv("/sbin/init", {"init", "-", NULL})`;
- weak fail-stop `md_init_process`, локальный proc1 allocator и
  cross-process `longjmp` удалены; i386 оставляет только scheduler-format
  trampoline, `md_user_enter` и переход на настоящий proc0 u-area stack;
- строгая i686 GCC-сборка, direct и BIOS QEMU boot, оба no-IDE gate,
  rootfs/bios-image smoke, exception gates, RAM matrix 32–1024 МиБ, host
  disk/VM tests и GCC `kernel-objects` для Ci20/N64 прошли. PCC не запускался
  и не менялся.

Оставшийся `pc/boot_main.c` оркестрирует разрушаемые i386 bring-up tests.
Следующий cleanup должен отделить diagnostic image от обычного startup,
переиспользуя общий boot path и не добавляя i386 subsystem policy. Реальное
IBM-тестирование пока не требуется.

Пятьдесят первый cleanup-инкремент удалил private i386 VM bootstrap:

- `sys/i386/pc/vm_bootstrap.c` и его private header удалены;
- `sys/i386/pc/vm_phys_board.c` реализует только установленный общий MD-hook
  `vm_phys_board_register`, direct-map и poison operations;
- metadata reserve, финализация physical map, запуск `vm_page_allocator` и
  его self-test выполняются существующими общими
  `vm_phys_bootstrap`/`vm_page_bootstrap_selftest`;
- чистая строгая i686 GCC-сборка, direct/BIOS QEMU с IDE и без IDE,
  rootfs/bios-image smoke, exception gates и RAM matrix 32–1024 МиБ прошли;
- host disk/VM tests и GCC `kernel-objects` для Ci20/N64 прошли. PCC не
  запускался и не менялся.

Следующим остаётся отделение diagnostic/preflight image по уже существующему
N64 build pattern и подключение штатного i686 image к common `init_main`.
До определения всех существующих common startup dependencies отдельная
i386 boot policy не добавляется. Реальное IBM-тестирование пока не требуется.

Пятьдесят второй cleanup-инкремент удаляет бессодержательные VM wrappers:

- `i386_pmap_bootstrap_init` и его private header удалены; boot path вызывает
  общий `pmap_system_init` напрямую;
- `i386_vmspace_bootstrap_init` удалён; boot path вызывает общий
  `vmspace_system_init` напрямую;
- i386 pmap hardware backend, `vmspace_current`/activate/deactivate/fault
  adapters и QEMU selftests не меняют своих контрактов;
- строгая i686 GCC-сборка, direct/BIOS QEMU с IDE и без IDE,
  rootfs/bios-image smoke, exception gates и RAM matrix 32–1024 МиБ прошли;
- host disk/VM tests и GCC `kernel-objects` для Ci20/N64 прошли. PCC не
  запускался и не менялся.

Пятьдесят третий cleanup-инкремент подключает i386 к common kernel clock:

- стандартный MD-hook `clkstart` программирует 8254 на `HZ` и открывает
  IRQ0 в 8259A;
- i386 IRQ frame передаёт interrupted PC, CPL и saved IF общему
  `hardclock`; отдельной i386 clock policy нет;
- прежний `i386_pit_init` стал внутренней hardware-функцией, а
  `timer-ticks` остаётся только наблюдаемым QEMU-счётчиком;
- существующий QEMU timer gate теперь исполняет общий `hardclock`, а не
  отдельный diagnostic-only IRQ путь и требует `hardclock-ticks: ok`;
- `ct_ticks` имеет единственный storage owner в `kern_clock.c`; оставшееся
  MIPS definition и локальные `extern` declarations удалены в пользу общего
  `systm.h`.
- clean i686 suite прошёл для direct/BIOS boot, IDE/no-IDE, `#DE/#GP/#PF`
  и RAM 32/64/128/256/768/1024 МиБ;
- host disk/VM tests и GCC `kernel-objects` для Ci20/N64 прошли. PCC не
  запускался и не менялся.

Пятьдесят четвёртый cleanup-инкремент полностью удаляет ранний
`VM_SINGLE_THREADED` и его условную ветку из common VM:

- i686 уже линкует общий `kern_synch`, scheduler и clock, поэтому busy
  anonymous pages используют существующий `tsleep`/`wakeup` contract;
- отдельного i386 locking path и замены ожидания на `EBUSY` больше нет;
- специальный no-swap build path этим инкрементом не добавляется:
  normal common startup должен получить состояние swap из существующей
  конфигурации.
- clean i686 suite прошёл для direct/BIOS boot, IDE/no-IDE, `#DE/#GP/#PF`
  и RAM 32/64/128/256/768/1024 МиБ; symbol audit подтверждает ссылки
  `vm_object.o` на общие `tsleep` и `wakeup`;
- host disk/VM tests и GCC `kernel-objects` для Ci20/N64 прошли. PCC не
  запускался и не менялся.

Пятьдесят пятый cleanup-инкремент зафиксировал утверждённую debug root
policy:

- встроенный UFS предоставляется общим `sys/disk/romdisk` на block major 0
  minor 0 и является единственным read-only root device;
- Ci20, Malta и i686 используют один byte-backed romdisk owner; платформы
  задают только linker bounds image;
- IDE регистрируется через отдельный generic disk major 2 как `sd0`;
  наличие и содержимое IDE больше не участвуют в выборе root;
- удалены IDE→memory fallback и отдельная проверка `/sbin/init` при выборе
  root; `/sbin/init` проверяется штатным общим `namei`/`exec` путём;
- direct и BIOS QEMU boots с IDE и без IDE требуют
  `vfs-root: ufs,romdisk,read-only`; romdisk не занимает `sdN`;
- clean i686 build, rootfs/bios-image smoke, `#DE/#GP/#PF`, RAM matrix
  32–1024 МиБ, host disk/VM tests и `kernel-objects` для Ci20, MaltaEL и
  N64 прошли;
- полные Ci20 и MaltaEL kernel/rootfs сборки прошли последовательно и
  слинковали общий `romdisk.o` с соответствующим `romdisk_machdep.o`;
- полный MIPS-профиль штатно проверил уже существующий MIPS PCC runtime.
  PCC source/config не менялись, а PCC для i686 не подключался;
- межархитектурная проверка обнаружила предыдущую ошибку общей
  `VM_PAGE_BYTES`: C-суффикс `u` попадал в MIPS linker script. Константа
  исправлена в общем заголовке, а правило генерации теперь зависит от
  включаемых `layout.h` и `vm_constants.h`, поэтому Ci20/Malta/N64 scripts
  корректно пересоздаются при изменении общих констант.

Исторический `pc/vfs_bootstrap.c` обслуживал только diagnostic image и не
содержал выбора root; он удалён вместе с private process bootstrap и старым
тестовым init. Normal image входит в common `init_main`; `swap none`
проходит через общий config как `NODEV`.

## Обязательный i686 USB Mass Storage этап

USB не является отдельной i686 storage-подсистемой. Реализация обязана
переиспользовать уже работающую на Ci20 цепочку:

```text
общий USB core/hub -> общий umass BOT/SCSI -> общий sys/disk major 2 -> sdN
```

QEMU EHCI/OHCI/UHCI-инкремент выполнен:

- i686 напрямую линкует существующие `usb_core`, service/task queue,
  `uhub`, `ehci`, `ohci`, `uhci`, `ukbd`, `umass` BOT/SCSI, общий DMA
  allocator и `sys/disk`;
- `sys/i386` добавляет только PCI class/progif discovery, uncached MMIO
  для OHCI/EHCI, I/O-port callbacks для UHCI, coherent DMA-pool attachment
  и 8259 IRQ adapter;
- direct и BIOS QEMU с `usb-ehci`/`usb-storage` проходят enumeration,
  SCSI inquiry/capacity и общий `disk_attach`; отдельные direct/BIOS QEMU
  gates с `pci-ohci`/`usb-kbd` и `usb-mouse` проходят общие HID
  boot-keyboard/boot-mouse drivers;
- direct и BIOS QEMU с PIIX3 UHCI проходят общий control/bulk/interrupt
  HCD: отдельные gates покрывают boot keyboard, boot mouse и mass storage
  как `sd1` с IDE и `sd0` без IDE; тот же storage gate проходит на
  `pc-i440fx-5.1`;
- общий `sys/input` владеет keyboard mapping, PS/2 decoding и mouse event
  device; i386 i8042-код ограничен transport setup и IRQ1/IRQ12;
- при наличии IDE он остаётся `sd0`, USB получает `sd1`; без IDE USB
  получает `sd0`. Romdisk остаётся root `(0,0)` и не занимает `sdN`;
- все QEMU пути требуют `vfs-root: ufs,romdisk,read-only`.

Оставшийся hardware-порядок:

1. на IBM 6563-W4G проверить PCI ID USB function VIA, затем тот же
   enumeration/read-only gate на реальной флешке;
2. проверить PS/2 keyboard/mouse и USB boot keyboard/mouse на реальном IBM;
   QEMU HID devices являются full-speed, а low-speed TD flag отдельно
   покрыт host fake-I/O тестом;
3. IDE и USB получают `sdN` по общему attach order. Номер `sdN` не задаёт
   root policy: debug root остаётся romdisk `(0,0)`.

Запрещены отдельные i386 USB core, `umass`, SCSI transport, partition parser,
filesystem path или собственный namespace устройств.

## Общий PCI IDE PIO/DMA этап

Следующий storage-инкремент завершён после стабильного PIO baseline:

- ATA transport перенесён из `sys/i386` в общий `sys/pci/pciide`; он владеет
  IDENTIFY/LBA28, PIO read/write, cache flush, PCI bus-master MWDMA,
  controller timings, PRDT, IRQ completion, reset и error recovery;
- i386 оставляет только BIOS compatibility ports, edge-triggered IRQ14 и
  adapter к общему scheduler wait/wakeup contract;
- поддержаны QEMU PIIX3 `8086:7010` и фактическая IBM VIA IDE `1106:0571`
  при VIA 596B ISA bridge `1106:0596`; timing fields сверены с реализациями
  NetBSD для PIIX и Apollo/VIA IDE;
- `ata=pio`, `ata=dma` и `ata=auto` выбирают режим одного общего driver.
  Automatic mode выбирает максимальный общий MWDMA0--2 и остаётся в PIO,
  если controller/device/DMA resources не поддерживают DMA;
- DMA завершается по IRQ14. Error или timeout завершает текущий request с
  `EIO`, останавливает и сбрасывает channel и навсегда переводит следующие
  операции этого attachment в PIO;
- host fake-hardware gate проверяет PIO/DMA read, write, cache flush,
  PIIX/VIA timing registers, capability fallback и injected DMA error/timeout.
  QEMU отдельно проходит
  forced PIO, forced DMA и automatic selection;
- общий ATA backend содержит полный write/flush contract, но i686 пока
  публикует внешний IDE-CF с `DISK_FLAG_READ_ONLY`. Поэтому реальный gate не
  выполняет ATA write и не меняет Red Hat/GRUB CF; embedded read-only UFS
  остаётся root `(0,0)`.

Следующий аппаратный gate: на IBM сначала `ata=pio`, затем `ata=dma` и
`ata=auto`, каждый раз с read-only `sd0` и сохранением полного mode/error
log. Разрешение записи на настоящий CF является отдельным изменением disk
policy и без явного разрешения не выполняется.

## Общий PCI Ethernet этап

PCI Ethernet для IBM реализуется как общий, переносимый subsystem, поскольку
PCI не принадлежит архитектуре i686:

- `sys/pci` владеет enumeration, config/resource API, BAR probing и общим
  RTL8169/RTL8110 hardware driver; архитектура предоставляет только механизм
  конфигурации, отображение I/O/MMIO, INTx и delay;
- BSD `re0` front-end использует общий `ifnet`, ARP и IPv4. Повторявшиеся в
  Malta NE2000, Ci20 DM9000 и MIPS USB Ethernet сборка/разбор Ethernet frames
  перенесены в `sys/netinet/if_ether.c` и удалены из драйверов;
- первый точный controller — фактически установленный `10ec:8169` revision
  `10`; поддерживаются исходные MAC versions 2--6, DMA descriptor rings,
  copper MII autonegotiation и shared legacy INTx;
- fake-hardware host gate проверяет PCI status preservation, I/O/32/64-bit
  BAR, отклонение неизвестного XID, RX/TX rings, link IRQ и system-error
  recovery. QEMU 11 не имеет RTL8169 model, поэтому реальный RX/TX gate
  остаётся за IBM;
- строгие i686, Ci20/N64 с native PCC и обе Malta endian kernel-сборки
  являются обязательной межархитектурной регрессией этого этапа.

Запрещены i386-private Ethernet frame/ARP path, копия PCI enumeration или
подмена фактического `10ec:8169` другим QEMU-only контроллером.

## 1. Цель и границы первого порта

Цель — получить отдельный 32-битный little-endian порт ReBSD для старых
IBM PC-совместимых компьютеров с legacy BIOS и процессором класса i686.

Первая поддерживаемая платформа:

- uniprocessor i686, protected mode, paging с 4 КиБ страницами;
- legacy BIOS; ранний boot contract совместим с Linux/x86 boot protocol
  2.02, чтобы один image загружался QEMU `-kernel` и GRUB Legacy `kernel`;
- QEMU `pc-i440fx` как референсная машина;
- VGA text console и COM1;
- 8259A PIC и 8253/8254 PIT;
- PS/2-клавиатура и мышь;
- PATA/IDE: общий PIO fallback и PCI bus-master MWDMA;
- MBR и существующая файловая система ReBSD;
- статические ELF32 i386 executables;
- GCC/binutils из `/Users/sash/Library/i686-toolchain`;
- kernel и userland собираются GCC; PCC не меняется и не входит в i686-порт.

В завершённый первый boot-этап исторически не входили: UEFI, SMP/APIC, ACPI
resource/routing tables, USB, SATA/AHCI, DMA для IDE, графический
framebuffer, динамическая линковка, PCC и поддержка 386/486/586.
USB и IDE DMA добавлены последующими общими этапами, описанными выше.
USB Mass Storage теперь является обязательным последующим этапом по плану
выше, а не исключённой возможностью порта.

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
`pc-i440fx` и `pentium3`. Один image поддерживает прямой QEMU `-kernel`,
существующий GRUB Legacy на IBM и native legacy-BIOS boot sector; для
последнего собирается raw 1.44 MB floppy artifact. Установка LILO не
требуется.

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
подтверждены двумя hardware gates. Реальный PCI inventory: VIA
VT82C693A/694x host `1106:0691`, VT82C596 ISA `1106:0596`, VIA IDE
`1106:0571` и 3Dfx Voodoo3 AGP VGA `121a:0005`. Точный объём RAM, CPU
stepping и дополнительные PCI-карты ещё надо снять с полного serial log.

Документированный общий planar семейства 6563:

- Intel Pentium III, 100/133 MHz FSB;
- VIA Apollo Pro 133 family: VT82C694X north bridge;
- VIA VT82C596B south bridge;
- AGP video adapter с VGA-compatible text mode;
- PCI-to-ISA bridge и классические legacy IRQ;
- два serial ports, PS/2 keyboard/mouse и RTC/CMOS;
- PCI-to-IDE controller, PIO modes 0–4, primary IRQ 14, secondary IRQ 15;
- PC100/PC133 SDRAM, BIOS memory autoconfiguration.

Первый hardware boot намеренно использовал только общие PC-интерфейсы,
одинаковые для QEMU и IBM: 8259A, PIT, PS/2, COM1, VGA text buffer и legacy
IDE PIO ports. После стабильного PIO gate общий PCI IDE driver добавил PIIX
и VIA bus-master MWDMA, сохранив primary `0x1F0`/IRQ14 compatibility mode и
PIO fallback. AGP configuration по-прежнему не включён.

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

### Этап 2. Linux boot protocol, GRUB Legacy и ранняя консоль

1. Собирать `rebsd-i686.elf` только для symbols/debug и единственный
   устанавливаемый artifact `rebsd-i686.bzimg` в формате, совместимом с
   Linux/x86 boot protocol 2.02.
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

5. BIOS/HDD gate использует уже установленный GRUB Legacy и загружает
   единственный install artifact `rebsd-i686.bzimg` через:

   ```text
   title ReBSD i686
       root (hd0,2)
       kernel /boot/rebsd-i686.bzimg
   ```

6. Создать GDT с kernel/user code/data descriptors и TSS.
7. Реализовать ранний COM1 polling и VGA text output.
8. Добавить `run`, `run-serial`, `debug` и документированный GRUB hardware
   gate.

Критерий готовности: в QEMU стабильно печатаются banner, нормализованная
BIOS E820 memory map и результат self-check GDT; panic также виден через
COM1. Native BIOS floppy path проходит QEMU, а GRUB Legacy HDD boot уже
подтверждён на IBM 6563-W4G.

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

Bootstrap-часть этапа, generic physical-page allocator, публичный i386
`pmap` выполнены: E820 normalization, ранний monotonic allocator, передача
свободной памяти в `vm_phys_map`/`vm_page_allocator`, CR3 switch,
4-КиБ identity mappings, permanent kernel direct map, supervisor-only PTE
и writable protection. Low-level mapper умеет `map/unmap/protect/extract`,
USER mappings и `invlpg`; `pmap` создаёт отдельные process directories,
активирует их через CR3 и освобождает page-table pages. Generic `vmspace`,
anonymous faults и COW подключены к i386 page-fault handler. Process
context switch и безопасные `copyin/copyout/copyinstr`, работающие через
vmspace access без прямого разыменования user VA из ring 0, выполнены.

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
2. i8042 transport; общие PS/2 keyboard/mouse decoders.
3. RTC CMOS для wall clock.
4. PCI configuration mechanism #1 только как инфраструктура обнаружения.
5. Общий PIIX/VIA PATA/IDE: PIO fallback и IRQ-driven bus-master MWDMA.
6. Подключение IDE к существующему `sys/disk` backend contract.
7. MBR partition discovery и root filesystem с IDE-диска.
8. NE2000/RTL8139 — после стабильного storage и VM.

Критерий готовности: одна и та же HDD image грузится в QEMU и на BIOS-PC,
root монтируется read/write, reboot/sync не повреждают файловую систему.

### Этап 8. Реальное старое железо и устойчивость

Проверочная матрица:

```text
QEMU pc-i440fx:  32/64/128/256/768/1024 MiB, serial + VGA, IDE
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
4. **Различия boot paths.** После handoff kernel не должен вызывать BIOS в
   protected mode. QEMU direct boot, native BIOS setup и GRUB Legacy обязаны
   сходиться в одном `i386_bootinfo`; отсутствующие boot-protocol поля setup
   дополняет через BIOS до переключения в protected mode.
5. **FPU context corruption.** До полноценного save/restore пользовательский
   x87 выключен; lazy-FPU можно добавлять позже.
6. **Старое железо без serial.** VGA panic console обязательна, но serial
   остаётся основным машинно-читаемым test channel.

7. **Различие QEMU и IBM chipset.** QEMU PIIX и IBM VIA 596B имеют разные
   PCI IDs. Общий IDE backend сохраняет legacy compatibility task-file ports,
   но программирует отдельные PIIX/VIA timing registers и общий PCI
   bus-master interface; оба режима обязаны сохранять PIO fallback.

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

Definition of Done пятого инкремента:

```text
vm-bootstrap-reserved: ok
vm-page-selftest: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Definition of Done шестого инкремента:

```text
pmap-public: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Definition of Done седьмого инкремента:

```text
vmspace-selftest: ok
vmspace-page-fault: ok
pic: ok
pit: hz=100
timer-ticks: ok
HALT
```

Следующий инкремент — нейтральные process MD hooks, i386 u-area/context
switch и `copyin/copyout`, а не storage, userland или PCC.
