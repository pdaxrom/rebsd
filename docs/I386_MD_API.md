# i386 machine-dependent integration audit

Статус: generic allocator, публичный i386 pmap, vmspace fault path,
`copyin/copyout`, i386 u-area allocator, kernel context switch, fork/init
frames, проверяемый ring-3 entry и `int 0x80` register ABI подключены; MIPS
process symbols нейтрализованы, i386 `sysent` adapter, signal frame и
user-return signal/reschedule path проверены; следующий gate — перевод
user CPU faults в pending signals, 2026-07-25.

## Существующий нейтральный VM контракт

`sys/vm/pmap.h` уже задаёт architecture-neutral API:

- lifecycle: `pmap_system_init`, `pmap_create`, `pmap_destroy`;
- mappings: `pmap_prepare`, `pmap_enter`, `pmap_enter_device`,
  `pmap_remove`, `pmap_protect`, `pmap_extract`;
- activation/faults: `pmap_activate`, `pmap_deactivate`, `pmap_fault`;
- page state, cache synchronization, statistics and validation.

Размер hardware VM page уже отделён от исторического `NBPG`:
`sys/vm/vm_param.h` задаёт `VM_PAGE_SIZE=4096`. Типы virtual/physical
addresses в `sys/vm/vm_types.h` являются 32-битными и подходят i686.

## Почему MIPS pmap нельзя использовать на i686 напрямую

`sys/mips/common/pmap.c` содержит полезную policy-часть, но сейчас связан с
MIPS hardware моделью:

- ASID allocation и generation rollover;
- paired-entry software TLB refill;
- `mips_pmap_fast_directory` и refill diagnostics;
- MIPS EntryHi/EntryLo encoding;
- direct map через KSEG0/KSEG1;
- explicit D/I-cache maintenance.

i686 использует hardware page-table walk через CR3, не имеет MIPS ASID в
целевом non-PAE режиме и инвалидирует translations через `invlpg` либо
reload CR3. Поэтому первый i386 backend реализуется отдельно, сохраняя
публичный API `sys/vm/pmap.h`; общий policy-код можно выделить позже без
риска сломать MIPS.

## Уже реализованный i386 hardware слой

`sys/i386/pc/paging.c` предоставляет hardware основу pmap:

- non-PAE 1024-entry page directory и page tables;
- supervisor/user и read/write PTE/PDE bits;
- `map`, `unmap`, `protect`, `extract`, `query`;
- targeted TLB invalidation через `invlpg`;
- bootstrap identity map, постоянный direct map первого 1 ГиБ RAM по
  `0xC0000000 + physical_address` и `CR0.PG|CR0.WP`;
- QEMU self-test resident replacement, RO protection, USER bit и removal.

Ранний allocator остаётся monotonic только до bootstrap generic VM. Затем
нормализованные E820 ranges передаются в `vm_phys_map` и
`vm_page_allocator`: уже использованный prefix и allocator metadata
помечаются reserved, остальные страницы доступны buddy allocator.
`vm_page_bootstrap_selftest` проверяет обычное и constrained allocation,
free и poison через kernel direct map.

Страницы существующего bootstrap page directory/page tables уже
зарезервированы и не могут попасть в free lists. Публичный
`sys/i386/common/pmap.c` выделяет свои directory и table pages как
`VM_PAGE_WIRED`, а `pmap_destroy` освобождает их обратно в allocator.
Address spaces наследуют необходимые low bootstrap mappings и kernel
direct map; user PDE получает private copy при первом `pmap_prepare`.

QEMU self-test переключает реальные CR3 между двумя pmap, проверяет
различные physical pages по одному VA, read-only protection, resident
replacement, executable mapping, сохранность low-linked kernel mappings и
полный reclaim allocator pages. Аппаратные Accessed/Dirty bits согласуются
с generic reference/dirty counters; non-PAE i686 не может аппаратно
различать read и execute и не предоставляет NX.

Начальный direct map ограничивает используемую RAM первым 1 ГиБ. Это
покрывает текущую QEMU-матрицу и первый IBM/VIA bring-up; PAE/highmem не
входит в ранний порт. Ядро всё ещё исполняется по low-linked адресу 1 МиБ,
поэтому pmap отклоняет user mapping, пересекающий загруженный kernel image.

## Generic kernel blockers

До первого полноценного generic link остаются следующие MD blockers:

1. Разделить исторический `copystr` на однозначные kernel-string и
   user-string операции; low-linked i386 пока не может безопасно определять
   тип указателя по одному virtual address.
2. Добавить оставшиеся machine headers (`types`, `vmparam`, `cpu`, `fpu`,
   `limits`) и i386 Kconfig/file lists. Минимальные `layout.h` и
   `elf_machdep.h` уже задают ELF32, little-endian, `EM_386` и `R_386_*`;
   generic ELF loader использует `ELF_MACHDEP_ID_CASES` вместо `EM_MIPS`.

PCC не входит в этот список и остаётся нетронутым.

## Следующий integration gate

Generic `vm_map`/`vm_object`/`vmspace` уже входят в ранний image. MD
activation регистрирует active vmspace, а `#PF` использует
`pmap_fault_active` и `vmspace_fault_context`; QEMU проверяет настоящий
non-present fault, COW и address-space isolation. Capability-флаги раннего
i386 режима не меняют обычную MIPS ветку: `BOARD=maltael kernel-objects`
компилируется тем же GCC baseline.

Публичные i386 `copyin/copyout` используют текущий vmspace и
`vmspace_read_context`/`vmspace_write_context`. User virtual address не
разыменовывается из ring 0: данные копируются через physical-page direct
map, поэтому bad user pointer возвращает `EFAULT`, а не требует recovery из
kernel-mode `#PF`. Bounds берутся из текущего `vm_map`, включая overflow и
переход через user limit. QEMU self-test переносит слово через границу двух
страниц, demand-fault'ит обе страницы, проверяет COW isolation сразу на двух
physical pages, read-only rejection и полный allocator reclaim. В раннем
однопоточном image copy path помечен `VM_FAULT_CAN_SLEEP`; IRQ-safe вариант
будет выбран после появления trap/process context accounting.

Generic process код теперь использует нейтральные `md_curuser`,
`md_uarea_alloc/fork/free`, `md_uarea_guard_init/check`, `md_init_process` и
`md_user_enter`. MIPS сохранил прежнюю реализацию и только предоставляет её
через новый контракт; внутренних `mips_fork_trampoline` и
`mips_init_trampoline` это не касается. `kernel-objects` проходят для N64,
CI20, Malta big/little endian и Malta64, а object symbol audit подтверждает
парные definition/reference для всех новых точек входа.

I386 реализация выделяет u-area размером `USIZE=16 КиБ` как четыре
contiguous `VM_PAGE_WIRED` страницы и адресует их через kernel direct map.
Реализованы guard, allocation/free, `md_curuser` и структурный fork-copy с
очисткой context labels. QEMU проверяет alignment, copy isolation, current
binding и точное восстановление free-page counter.

Assembly `setjmp/longjmp` сохраняют и восстанавливают i386 callee-saved
регистры, kernel ESP/EIP и EFLAGS по существующему generic scheduler ABI.
`longjmp` атомарно с переходом меняет `md_curuser`; CR3 выбирается scheduler
через уже существующий `vmspace_activate`. QEMU self-test сначала проверяет
регистры, затем переходит на настоящий отдельный high direct-map u-area
stack, выполняет C entry point и возвращается в исходный context. Оба guard
и точный reclaim четырёхстраничных u-area после перехода также проверяются.

`md_uarea_fork` теперь строит schedulable frame. Обычный fork проверяет
родительский `u_frame`, копирует i386 trapframe на вершину нового u-area,
задаёт дочерний `EAX=0` и входит в общий interrupt restore/`iret` через
`i386_fork_trampoline`. Bootstrap path получает отдельный kernel stack и
`i386_init_trampoline`, вызывающий сильную generic реализацию
`md_init_process`; ранний image предоставляет только слабый fail-stop stub.

QEMU выполняет обычный fork frame до конца через ring-0 `iret`. Перед
переходом активируется дочерний vmspace/CR3, дочерний C entry проверяет своё
COW-значение по общему VA, `md_curuser`, `u_procp` и границы kernel stack,
после чего активирует родительский vmspace и возвращается. Родитель видит
своё исходное значение, а уничтожение обоих vmspace и u-area точно
восстанавливает allocator counter.

GDT содержит flat DPL3 code/data descriptors и runtime-заполняемый 32-bit
TSS descriptor. После `ltr` каждый i386 `longjmp` синхронно ставит `esp0`
на вершину нового u-area; вне процесса TSS использует отдельный fail-safe
stack. Ring-3 self-test активирует собственный vmspace, входит через `iret`,
выполняет пользовательские `int3` и `int 0x30`, проверяет аппаратный
privilege stack switch и полный user SS/ESP trapframe, затем возвращает
исполнение в ring 0. Тестовый vector `0x30` не является syscall ABI.

IDT vector `0x80` является DPL3 interrupt gate. Зафиксирован i386 ABI:
`EAX` содержит syscall number, `EBX/ECX/EDX/ESI/EDI/EBP` — до шести
32-битных аргументов, `EAX/EDX` — два результата. Успех очищает Carry;
ошибка возвращает положительный errno в `EAX` и устанавливает Carry.
QEMU user stream сначала получает настоящий `ENOSYS+CF`, затем проходит
установленную через `i386_syscall_set_table` локальную `struct sysent`
таблицу. Адаптер заполняет `u_arg`, вызывает `sy_call` под `u_qsave`,
возвращает `u_rval/u_rval2` и реализует positive errno, `ERESTART` с
повтором двухбайтного `int 0x80`, а также `EJUSTRETURN` без изменения
trapframe. CPL3-тест проверяет все эти ветви и `longjmp` из обработчика.
Вызов с более чем шестью аргументами отклоняется как `EINVAL` согласно
зафиксированному ABI.

`exec_subr.c` и `sys_process.c` больше не включают MIPS `machine/io.h` и не
используют `FRAME_*`. Общий `sys/user.h` задаёт opaque MD-операции для
exec register setup, ptrace write, PC и single-step. MIPS реализация
сохраняет прежние compact/MIPS3-wide frame semantics; i386 реализация
формирует user selectors, `EIP/ESP`, начальные `argc/argv/envp` registers,
фильтрует опасные EFLAGS при ptrace и использует x86 Trap Flag. QEMU
u-area self-test проверяет этот контракт, а MaltaEL и N64 `kernel-objects`
подтверждают отсутствие MIPS-регрессии.

I386 signal ABI использует обычный cdecl stack: return address
`u_sigtramp`, signal number, code и указатель на встроенный `sigcontext`.
`sendsig` сохраняет все user GPR, segment registers, `EIP/ESP/EFLAGS`,
маску и состояние alternate stack, а затем переводит frame на обработчик.
`sigreturn` принимает указатель первым аргументом `int 0x80`, проверяет
user selectors, executable `EIP` и writable stack, исключает
`SIGKILL/SIGSTOP` из восстановленной маски и не позволяет вернуть
kernel selectors или опасные EFLAGS. QEMU self-test проверяет обычный и
alternate signal stack, формат frame, восстановление контекста, rejection
поддельного `CS` и точный reclaim VM/u-area.

Общая assembly-эпилога trap/IRQ/syscall вызывает `i386_user_return`
непосредственно перед восстановлением регистров. Для ring-0 frame это
no-op; для CPL3 она устанавливает `u_frame`, разрешает прерывания,
повторяет MIPS-порядок `CURSIG/postsig`, `setpri`, при `runrun` выполняет
`setrq/swtch`, затем запрещает прерывания перед `iret`. Early image
использует test operations вместо ещё не подключённых generic objects;
их сильные `issignal/postsig/setpri/setrq/swtch` автоматически заменяют
слабые ранние fallback symbols при production link.

QEMU CPL3-тест начинает с pending `SIGUSR1`, проходит общую эпилогу,
исполняет реальный user handler, проверяющий cdecl signum/code/context,
изменяет `sc_eax`, возвращается через user trampoline и вызывает
`sigreturn` настоящим `int 0x80`. После восстановления исходного `EIP`
проверяются изменённый `EAX`, signal mask, `ru_nsignals`, моделируемый
`setrq/swtch`, `ru_nivcsw` и точный reclaim. PIC теперь remap/mask сразу
после IDT, до первого кода, который разрешает IF; timer unmask остаётся
перед PIT.

Полный `kernel/init_sysent.c` ещё не входит в early image: его обработчики
тянут остальные подсистемы. Публичный setter уже позволяет установить
production `sysent`, но user exceptions пока должны быть переведены из
раннего panic path в `psignal` перед запуском непривилегированного userland.

1. Преобразовать user CPU exceptions и terminal page faults в `psignal`,
   оставив kernel faults диагностическими panic.
2. Добавить явный user-string primitive и перевести syscall pathname/exec
   call sites без pointer-range эвристики.
3. Затем включить production `init_sysent`, process bootstrap и exec ABI.
