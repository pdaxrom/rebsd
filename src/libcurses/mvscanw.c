/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include "curses.ext"

/*
 * implement the mvscanw commands.  Due to the variable number of
 * arguments, they cannot be macros.  Another sigh....
 */
int
mvscanw(int y, int x, char *fmt, ...)
{
	va_list args;
	int ret;

	if (move(y, x) != OK)
		return ERR;
	va_start(args, fmt);
	ret = _sscans(stdscr, fmt, args);
	va_end(args);
	return ret;
}

int
mvwscanw(WINDOW *win, int y, int x, char *fmt, ...)
{
	va_list args;
	int ret;

	if (wmove(win, y, x) != OK)
		return ERR;
	va_start(args, fmt);
	ret = _sscans(win, fmt, args);
	va_end(args);
	return ret;
}
