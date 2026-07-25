# План портирования ReBSD на i686/BIOS

Статус: двадцать девять QEMU bring-up инкрементов выполнены, 2026-07-25.

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
- для раннего однопоточного режима явно заданы `VM_SINGLE_THREADED` и
  `VM_PAGER_NO_SWAP`; anonymous pages и COW работают, swap не имитируется;
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
LILO HDD gate выполняется после появления Linux-среды для установщика.

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

Двадцать третий QEMU bring-up инкремент завершён:

- минимальная user-программа теперь отдельно собирается GCC/binutils
  toolchain в настоящий `ET_EXEC` ELF32/i386, а затем встраивается в
  read-only секцию kernel image;
- ранний in-memory loader валидирует ELF magic/class/data/ABI, `EM_386`,
  header bounds, alignment, user ranges, entry point и до восьми
  неперекрывающихся `PT_LOAD`;
- loader отклоняет interpreter/неизвестные program headers и W+X segments,
  загружает файлы через generic vmspace, явно обнуляет BSS, применяет
  финальные `PF_R/PF_W/PF_X` permissions и откатывает mappings при ошибке;
- встроенный ELF содержит отдельные RX text и RW data+BSS segments; CPL3
  код проверяет initialized data, нулевой BSS и запись в него до production
  `getpid`;
- entry point берётся из ELF header, а не из kernel-константы; обязательный
  marker `elf32-user: ok` подтверждает полный build/load/execute path;
- clean build, QEMU normal/trap smoke и RAM matrix
  32/64/128/256/768/1024 МиБ проходят с прежними
  `process-bootstrap: ok` и `process-user: ok`.

Двадцать четвёртый QEMU bring-up инкремент завершён:

- ранний i386 stack builder повторяет layout generic `exec_setupstack`:
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
