/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _ARPA_TELNET_H_
#define _ARPA_TELNET_H_

/*
 * Definitions for the TELNET protocol.
 */
#define IAC             255     /* interpret as command */
#define DONT            254
#define DO              253
#define WONT            252
#define WILL            251
#define SB              250
#define GA              249
#define EL              248
#define EC              247
#define AYT             246
#define AO              245
#define IP              244
#define BREAK           243
#define DM              242
#define NOP             241
#define SE              240
#define EOR             239

#define TELOPT_BINARY   0
#define TELOPT_ECHO     1
#define TELOPT_RCP      2
#define TELOPT_SGA      3
#define TELOPT_NAMS     4
#define TELOPT_STATUS   5
#define TELOPT_TM       6
#define TELOPT_RCTE     7
#define TELOPT_NAOL     8
#define TELOPT_NAOP     9
#define TELOPT_NAOCRD   10
#define TELOPT_NAOHTS   11
#define TELOPT_NAOHTD   12
#define TELOPT_NAOFFD   13
#define TELOPT_NAOVTS   14
#define TELOPT_NAOVTD   15
#define TELOPT_NAOLFD   16
#define TELOPT_XASCII   17
#define TELOPT_LOGOUT   18
#define TELOPT_BM       19
#define TELOPT_DET      20
#define TELOPT_SUPDUP   21
#define TELOPT_SUPDUPOUTPUT 22
#define TELOPT_SNDLOC   23
#define TELOPT_TTYPE    24
#define TELOPT_EOR      25
#define TELOPT_TUID     26
#define TELOPT_OUTMRK   27
#define TELOPT_TTYLOC   28
#define TELOPT_3270REGIME 29
#define TELOPT_X3PAD    30
#define TELOPT_NAWS     31
#define TELOPT_TSPEED   32
#define TELOPT_LFLOW    33
#define TELOPT_EXOPL    255

#define TELQUAL_IS      0
#define TELQUAL_SEND    1

#endif /* _ARPA_TELNET_H_ */
