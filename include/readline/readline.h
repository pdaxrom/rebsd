/*
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * Based on linenoise.c with API modified for compatibility with
 * traditional readline library.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2015, Serge Vakulenko <serge at vak dot ru>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __READLINE_H
#define __READLINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define READLINE_MAX_LINE 4096

typedef struct readline_completions {
    size_t len;
    char **cvec;
} readline_completions;

typedef void readline_completion_callback(const char *line, size_t cursor,
                                           readline_completions *completions);

/*
 * Read a line of input.
 * Prompt with PROMPT.
 * A NULL PROMPT means none.
 */
char *readline(const char *prompt);

/*
 * Read a line using explicit input and output descriptors.  This is useful
 * to programs which move their standard descriptors before reading commands.
 */
char *readline_fd(const char *prompt, int input_fd, int output_fd);

/*
 * Register an application-specific completion callback.  Completion strings
 * are complete replacement lines, not just the word being completed.
 */
void readline_set_completion_callback(readline_completion_callback *callback);
int readline_add_completion(readline_completions *completions,
                            const char *line);

/*
 * Restore the terminal after an asynchronous interruption of readline_fd().
 */
void readline_abort(void);

/*
 * Clear the screen.
 * Used to handle Ctrl+L.
 */
void readline_clear_screen(void);

/*
 * Set if to use or not the multi line mode.
 */
void readline_set_multiline(int ml);

/*
 * This routine is used in order to print scan codes on screen
 * for debugging / development purposes.
 */
void readline_print_keycodes(void);

#ifdef __cplusplus
}
#endif

#endif /* __READLINE_H */
