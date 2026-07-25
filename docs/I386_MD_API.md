# i386 machine-dependent integration audit

Статус: generic physical-page allocator и публичный i386 pmap подключены;
следующий gate — generic vmspace и page-fault integration, 2026-07-25.

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

До первого полноценного generic link нужны нейтральные MD hooks:

1. `sys/include/user.h`:
   заменить публичное имя `mips_curuser` на нейтральный current-uarea hook.
2. `sys/include/systm.h` и `sys/kernel/init_main.c`:
   обобщить `mips_init_process`, `mips_user_enter`,
   `mips_uarea_guard_init`.
3. `sys/kernel/kern_fork.c` и `sys/kernel/kern_synch.c`:
   вынести MIPS u-area fork/guard operations в MD API.
4. `sys/kernel/exec_elf.c`:
   заменить жёсткий `EM_MIPS` на machine-dependent ELF validation;
   i686 принимает `EM_386`.
5. Реализовать i686 `copyin/copyout/copyinstr` с recovery из kernel-mode
   page fault.
6. Добавить machine headers (`types`, `machparam`, `vmparam`, `layout`,
   `cpu`, `fpu`, `limits`) и i386 Kconfig/file lists.

PCC не входит в этот список и остаётся нетронутым.

## Следующий integration gate

1. Подключить generic `vm_map`, `vm_object`, `vmspace` и их dependencies к
   раннему i386 image.
2. Добавить QEMU vmspace gate для anonymous fault, COW, protect/unmap и
   address-space isolation без MIPS TLB assumptions.
3. Связать i386 user page fault с `vmspace_fault` и оставить kernel-mode
   recovery hook для будущих `copyin/copyout`.
4. После этого подключать process bootstrap и syscall ABI.
