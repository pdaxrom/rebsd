/*
 *      TtySet ()
 *
 *              - set terminal CBREAK mode.
 *
 *      TtyReset ()
 *
 *              - restore terminal mode.
 *
 *      TtyFlushInput ()
 *              - flush input queue.
 */
#include <sys/ioctl.h>
#include <unistd.h>
#include "deco_compat.h"
#include "scr.h"

#define CHANNEL 2 /* output file descriptor */

static struct sgttyb oldtio, newtio;
static struct tchars oldtchars, newtchars;
static struct ltchars oldchars, newchars;
static int oldpgrp;
static int have_oldpgrp;
static int own_pgrp;

#define NOCHAR 0

#define GET(addr) ioctl(CHANNEL, TIOCGETP, (addr))
#define SET(addr) ioctl(CHANNEL, TIOCSETP, (addr))

void TtySet()
{
    if (GET(&oldtio) < 0)
        return;
    newtio = oldtio;
    newtio.sg_flags &= ~(ECHO | CRMOD | XTABS);
    newtio.sg_flags |= CBREAK;
    SET(&newtio);

    ioctl(CHANNEL, TIOCGETC, (char *)&oldtchars);
    newtchars = oldtchars;
    newtchars.t_intrc = NOCHAR;
    newtchars.t_quitc = NOCHAR;
    ioctl(CHANNEL, TIOCSETC, (char *)&newtchars);

    ioctl(CHANNEL, TIOCGLTC, (char *)&oldchars);
    newchars          = oldchars;
    newchars.t_lnextc = NOCHAR;
    newchars.t_rprntc = NOCHAR;
    newchars.t_dsuspc = NOCHAR;
    newchars.t_flushc = NOCHAR;
    ioctl(CHANNEL, TIOCSLTC, (char *)&newchars);
}

void TtyReset()
{
    SET(&oldtio);

    ioctl(CHANNEL, TIOCSETC, (char *)&oldtchars);
    ioctl(CHANNEL, TIOCSLTC, (char *)&oldchars);
}

void TtyOwnPgrp(int pgrp)
{
    if (pgrp <= 0)
        return;
    if (!have_oldpgrp && ioctl(CHANNEL, TIOCGPGRP, (char *)&oldpgrp) == 0)
        have_oldpgrp = 1;
    setpgid(0, pgrp);
    tcsetpgrp(CHANNEL, pgrp);
    own_pgrp = 1;
}

void TtyRestorePgrp()
{
    if (!own_pgrp || !have_oldpgrp)
        return;
    tcsetpgrp(CHANNEL, oldpgrp);
    own_pgrp = 0;
}

void TtyFlushInput()
{
#ifdef TCFLSH
    ioctl(CHANNEL, TCFLSH, 0);
#else
#ifdef TIOCFLUSH
    int p = 1;

    ioctl(CHANNEL, TIOCFLUSH, (char *)&p);
#endif
#endif
}
