/*
 * Architecture-independent C runtime startup.
 *
 * Machine entry code supplies the exec ABI's argc, argv and envp values.
 */
#include <stdlib.h>
#include <unistd.h>

extern int main(int, char **, char **);
extern void __do_global_ctors(void);

char **environ;
char *__progname = "";

void
__rebsd_start(int argc, char **argv, char **env)
{
	char *s;

	environ = env;
	if (argc > 0 && argv[0] != 0) {
		__progname = argv[0];
		for (s = __progname; *s != '\0'; ++s)
			if (*s == '/')
				__progname = s + 1;
	}
	__do_global_ctors();
	exit(main(argc, argv, env));
}
