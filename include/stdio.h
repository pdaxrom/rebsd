/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef FILE

#include <sys/cdefs.h>
#include <sys/types.h>

#define BUFSIZ  1024
extern  struct  _iobuf {
    int     _cnt;
    char    *_ptr;      /* should be unsigned char */
    char    *_base;     /* ditto */
    int     _bufsiz;
    short   _flag;
    short   _file;
} _iob[];

#define _IOREAD     01
#define _IOWRT      02
#define _IONBF      04
#define _IOMYBUF    010
#define _IOEOF      020
#define _IOERR      040
#define _IOSTRG     0100
#define _IOLBF      0200
#define _IORW       0400

/*
 * The following definition is for ANSI C, which took them
 * from System V, which brilliantly took internal interface macros and
 * made them official arguments to setvbuf(), without renaming them.
 * Hence, these ugly _IOxxx names are *supposed* to appear in user code.
*/
#define _IOFBF      0   /* setvbuf should set fully buffered */
                        /* _IONBF and _IOLBF are used from the flags above */

#ifndef NULL
#define NULL        0
#endif

#include <stddef.h>

#define FILE        struct _iobuf
#define EOF         (-1)

#define stdin       (&_iob[0])
#define stdout      (&_iob[1])
#define stderr      (&_iob[2])

#define SEEK_SET    0   /* set file offset to offset */
#define SEEK_CUR    1   /* set file offset to current plus offset */
#define SEEK_END    2   /* set file offset to EOF plus offset */

#ifndef lint
#define getc(p)     (--(p)->_cnt>=0? (int)(*(unsigned char *)(p)->_ptr++):_filbuf(p))
#define putc(x, p)  (--(p)->_cnt >= 0 ?\
    (int)(*(unsigned char *)(p)->_ptr++ = (x)) :\
    (((p)->_flag & _IOLBF) && -(p)->_cnt < (p)->_bufsiz ?\
        ((*(p)->_ptr = (x)) != '\n' ?\
            (int)(*(unsigned char *)(p)->_ptr++) :\
            _flsbuf(*(unsigned char *)(p)->_ptr, p)) :\
        _flsbuf((unsigned char)(x), p)))
#endif /* not lint */

FILE    *fopen (const char *, const char *);
FILE    *fdopen (int, const char *);
FILE    *freopen (const char *, const char *, FILE *);
FILE    *popen (const char *, const char *);
int     pclose(FILE *stream);
FILE    *tmpfile (void);
char    *tmpnam(char *s);
int     fclose (FILE *);
long    ftell (FILE *);
off_t   ftello (FILE *);
int     fflush (FILE *);
int     fgetc (FILE *);
int     ungetc (int, FILE *);
int     fputc (int, FILE *);
int     putchar (int);
int     fputs (const char *, FILE *);
int     puts (const char *);
char    *fgets (char *, int, FILE *);
char    *gets (char *);
FILE    *_findiop (void);
int     _filbuf (FILE *);
int     _flsbuf (unsigned char, FILE *);
void    setbuf (FILE *, char *);
void    setbuffer (FILE *, char *, size_t);
void    setlinebuf (FILE *);
int     setvbuf (FILE *, char *, int, size_t);
int     fseek (FILE *, long, int);
int     fseeko (FILE *, off_t, int);
int     rename(const char *, const char *);
void    rewind (FILE *);
int     remove (const char *);
int     getw(FILE *stream);
int     putw(int w, FILE *stream);

size_t  fread (void *, size_t, size_t, FILE *);
size_t  fwrite (const void *, size_t, size_t, FILE *);

int     fprintf (FILE *, const char *, ...) __printflike(2, 3);
int     printf (const char *, ...) __printflike(1, 2);
int     sprintf (char *, const char *, ...) __printflike(2, 3);
int     snprintf (char *, size_t, const char *, ...) __printflike(3, 4);
int     asprintf (char **, const char *, ...) __printflike(2, 3);
int     dprintf (int, const char *, ...) __printflike(2, 3);
ssize_t getdelim (char **, size_t *, int, FILE *);
ssize_t getline (char **, size_t *, FILE *);

int     fscanf (FILE *, const char *, ...) __scanflike(2, 3);
int     scanf (const char *, ...) __scanflike(1, 2);
int     sscanf (const char *, const char *, ...) __scanflike(2, 3);

#define getchar()   getc(stdin)
#define putchar(x)  putc(x,stdout)
#define feof(p)     (((p)->_flag&_IOEOF)!=0)
#define ferror(p)   (((p)->_flag&_IOERR)!=0)
#define fileno(p)   ((p)->_file)
#define clearerr(p) ((p)->_flag &= ~(_IOERR|_IOEOF))

#ifndef _VA_LIST_
# ifdef __GNUC__
#  define va_list   __builtin_va_list   /* For Gnu C */
# endif
# ifdef __SMALLER_C__
#  define va_list   char *              /* For Smaller C */
# endif
#endif

int     vfprintf (FILE *, const char *, va_list) __printflike(2, 0);
int     vprintf (const char *, va_list) __printflike(1, 0);
int     vsprintf (char *, const char *, va_list) __printflike(2, 0);
int     vsnprintf (char *, size_t, const char *, va_list) __printflike(3, 0);
int     vasprintf (char **, const char *, va_list) __printflike(2, 0);
int     vdprintf (int, const char *, va_list) __printflike(2, 0);

const char *fmtcheck(const char *, const char *)
#ifdef __GNUC__
    __attribute__((__format_arg__(2)))
#endif
    ;

int     vfscanf (FILE *, const char *, va_list) __scanflike(2, 0);
int     vscanf (const char *, va_list) __scanflike(1, 0);
int     vsscanf (const char *, const char *, va_list) __scanflike(2, 0);

int     _doprnt (const char *, va_list, FILE *);
int     _doscan (FILE *, const char *, va_list);

#ifndef _VA_LIST_
# undef va_list
#endif

void    perror (const char *);

#endif /* _FILE */
