# i386 machine-dependent integration audit

Статус: generic allocator, публичный i386 pmap, vmspace fault path,
`copyin/copyout`, i386 u-area allocator, kernel context switch и fork/init
frames подключены; MIPS process symbols нейтрализованы, следующий gate —
ring-3 entry и syscall ABI, 2026-07-25.

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

1. Добавить user code/data selectors, TSS `esp0` и проверяемый ring-3 entry.
2. Добавить явный user-string primitive и перевести syscall pathname/exec
   call sites без pointer-range эвристики.
3. Затем подключить process bootstrap, `int 0x80` и exec ABI.
