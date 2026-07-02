static char *
find_char(char *sp, int c)
{
	do {
		if (*sp == c)
			return sp;
	} while (*sp++);
	return 0;
}

int
main(int argc, char **argv)
{
	char s[] = "adfo:rwt:uv";
	char *p;

	p = find_char(s, 'o');
	if (p != s + 3 || p[1] != ':')
		return 1;

	p = find_char(s, 't');
	if (p != s + 7 || p[1] != ':')
		return 2;

	p = find_char(s, 'z');
	if (p != 0)
		return 3;

	return 0;
}
