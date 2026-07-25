# i386 machine-dependent integration audit

Статус: первичный аудит перед подключением machine-independent kernel,
2026-07-25.

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

`sys/i386/pc/paging.c` предоставляет основу будущего pmap:

- non-PAE 1024-entry page directory и page tables;
- supervisor/user и read/write PTE/PDE bits;
- `map`, `unmap`, `protect`, `extract`, `query`;
- targeted TLB invalidation через `invlpg`;
- bootstrap identity map и `CR0.PG|CR0.WP`;
- QEMU self-test resident replacement, RO protection, USER bit и removal.

Bootstrap allocator пока monotonic. Перед полным `pmap_create/destroy`
page-table pages должны перейти под `vm_page_allocator`, чтобы их можно
было корректно wire/free и учитывать в общей статистике.

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

1. Поднять `vm_phys_map`/`vm_page_allocator` из нормализованного E820.
2. Перевести allocation page directory/page tables на `vm_page`.
3. Реализовать публичный i386 `pmap_create/enter/remove/protect/extract`.
4. Адаптировать существующий `sys/tests/vm/pmap_test.c` для i386 host/QEMU
   backend без MIPS TLB assumptions.
5. Только после этого подключать process bootstrap и syscall ABI.
