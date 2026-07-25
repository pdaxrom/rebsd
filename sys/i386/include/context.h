#ifndef _I386_CONTEXT_H_
#define _I386_CONTEXT_H_

#ifdef __ASSEMBLER__
#define I386_LABEL_EBX       0
#define I386_LABEL_ESI       4
#define I386_LABEL_EDI       8
#define I386_LABEL_EBP       12
#define I386_LABEL_ESP       16
#define I386_LABEL_EIP       20
#define I386_LABEL_EFLAGS    24
#define I386_USER_CODE_SELECTOR 0x001b
#define I386_USER_DATA_SELECTOR 0x0023
#else
#define I386_LABEL_EBX       0
#define I386_LABEL_ESI       1
#define I386_LABEL_EDI       2
#define I386_LABEL_EBP       3
#define I386_LABEL_ESP       4
#define I386_LABEL_EIP       5
#define I386_LABEL_EFLAGS    6
#endif

#define I386_EFLAGS_RESERVED 0x00000002
#define I386_UAREA_SIZE       16384

#endif
