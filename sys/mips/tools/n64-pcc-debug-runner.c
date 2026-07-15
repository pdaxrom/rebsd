/*
 * Minimal N64 PCC hardware diagnostic runner.
 *
 * This binary is built with the host GCC-to-a.out wrapper and the ReBSD
 * a.out linker, then staged into the debug root filesystem.  It avoids
 * shell control flow so VR4300/PCC shell failures do not hide compiler
 * regressions.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define WORKDIR "/var/tmp/n64-pcc-debug"
#define BINDIR  "/var/tmp/n64-pcc-debug/bin"
#define SRCDIR  "/root/pcc-debug-src"
#define AOUT_MAX_BYTES 2048

struct test_case {
	const char *name;
	const char *out;
	const char *subdir;
	const char *source;
	int need_math;
};

struct inline_test {
	const char *tag;
	const char *file;
	const char *source;
};

static const struct inline_test fp_probe_tests[] = {
	{
		"fp__div_zero",
		"fp-div-zero.c",
		"double f(void) { return 1/0.0; }\n"
		"int main(void) { return f() == 0; }\n"
	},
	{
		"fp__nan_zero",
		"fp-nan-zero.c",
		"double f(void) { return 0.0/0.0; }\n"
		"int main(void) { return 0; }\n"
	},
	{
		"fp__builtin_inf",
		"fp-builtin-inf.c",
		"float f(void) { return __builtin_inff(); }\n"
		"int main(void) { return 0; }\n"
	},
	{
		"fp__builtin_nan",
		"fp-builtin-nan.c",
		"double f(void) { return __builtin_nan(\"\"); }\n"
		"int main(void) { return 0; }\n"
	},
	{
		"fp__isnan_call",
		"fp-isnan-call.c",
		"#include <math.h>\n"
		"int main(void) { double x = 0.0/0.0; return !isnan(x); }\n"
	},
};

static const struct test_case primary_tests[] = {
	{ "c99__arith002", "c99__arith002", "c99", "arith002.c", 1 },
	{ "c99__arith003", "c99__arith003", "c99", "arith003.c", 1 },
	{ "jira__PCC-70", "jira__PCC_70", "jira", "PCC-70.c", 0 },
	{ "jira__PCC-77", "jira__PCC_77", "jira", "PCC-77.c", 0 },
	{ "jira__PCC-152", "jira__PCC_152", "jira", "PCC-152.c", 0 },
	{ "jira__PCC-185", "jira__PCC_185", "jira", "PCC-185.c", 0 },
};

static const struct test_case extended_tests[] = {
	{ "jira__PCC-84", "jira__PCC_84", "jira", "PCC-84.c", 0 },
	{ "jira__PCC-85", "jira__PCC_85", "jira", "PCC-85.c", 0 },
	{ "misc__llcall001", "misc__llcall001", "misc", "llcall001.c", 0 },
	{ "misc__ssastrength001", "misc__ssastrength001", "misc",
	    "ssastrength001.c", 0 },
	{ "misc__ssacounted001", "misc__ssacounted001", "misc",
	    "ssacounted001.c", 0 },
	{ "misc__pointertemp001", "misc__pointertemp001", "misc",
	    "pointertemp001.c", 0 },
	{ "jira__PCC-97", "jira__PCC_97", "jira", "PCC-97.c", 0 },
	{ "jira__PCC-101", "jira__PCC_101", "jira", "PCC-101.c", 0 },
	{ "jira__PCC-123", "jira__PCC_123", "jira", "PCC-123.c", 0 },
	{ "jira__PCC-125", "jira__PCC_125", "jira", "PCC-125.c", 0 },
	{ "jira__PCC-129", "jira__PCC_129", "jira", "PCC-129.c", 0 },
	{ "jira__PCC-131", "jira__PCC_131", "jira", "PCC-131.c", 0 },
	{ "jira__PCC-135", "jira__PCC_135", "jira", "PCC-135.c", 0 },
	{ "jira__PCC-138", "jira__PCC_138", "jira", "PCC-138.c", 0 },
	{ "jira__PCC-149", "jira__PCC_149", "jira", "PCC-149.c", 0 },
	{ "jira__PCC-154", "jira__PCC_154", "jira", "PCC-154.c", 0 },
	{ "jira__PCC-155", "jira__PCC_155", "jira", "PCC-155.c", 0 },
	{ "jira__PCC-157", "jira__PCC_157", "jira", "PCC-157.c", 0 },
	{ "jira__PCC-158", "jira__PCC_158", "jira", "PCC-158.c", 0 },
	{ "jira__PCC-159", "jira__PCC_159", "jira", "PCC-159.c", 0 },
	{ "jira__PCC-160", "jira__PCC_160", "jira", "PCC-160.c", 0 },
	{ "jira__PCC-165", "jira__PCC_165", "jira", "PCC-165.c", 0 },
	{ "jira__PCC-169", "jira__PCC_169", "jira", "PCC-169.c", 0 },
	{ "jira__PCC-170", "jira__PCC_170", "jira", "PCC-170.c", 0 },
	{ "jira__PCC-176", "jira__PCC_176", "jira", "PCC-176.c", 0 },
	{ "jira__PCC-179", "jira__PCC_179", "jira", "PCC-179.c", 0 },
	{ "jira__PCC-180", "jira__PCC_180", "jira", "PCC-180.c", 0 },
	{ "jira__PCC-182", "jira__PCC_182", "jira", "PCC-182.c", 0 },
	{ "jira__PCC-183", "jira__PCC_183", "jira", "PCC-183.c", 0 },
	{ "jira__PCC-187", "jira__PCC_187", "jira", "PCC-187.c", 0 },
	{ "jira__PCC-189", "jira__PCC_189", "jira", "PCC-189.c", 0 },
	{ "jira__PCC-190", "jira__PCC_190", "jira", "PCC-190.c", 0 },
	{ "jira__PCC-191", "jira__PCC_191", "jira", "PCC-191.c", 0 },
	{ "jira__PCC-192", "jira__PCC_192", "jira", "PCC-192.c", 0 },
	{ "jira__PCC-193", "jira__PCC_193", "jira", "PCC-193.c", 0 },
	{ "jira__PCC-195", "jira__PCC_195", "jira", "PCC-195.c", 0 },
	{ "jira__PCC-196", "jira__PCC_196", "jira", "PCC-196.c", 0 },
	{ "jira__PCC-197", "jira__PCC_197", "jira", "PCC-197.c", 0 },
	{ "jira__PCC-200", "jira__PCC_200", "jira", "PCC-200.c", 0 },
	{ "jira__PCC-201", "jira__PCC_201", "jira", "PCC-201.c", 0 },
	{ "jira__PCC-202", "jira__PCC_202", "jira", "PCC-202.c", 0 },
	{ "jira__PCC-203", "jira__PCC_203", "jira", "PCC-203.c", 0 },
	{ "jira__PCC-204", "jira__PCC_204", "jira", "PCC-204.c", 0 },
};

static void
make_path(char *buf, const char *dir, const char *name, const char *suffix)
{
	strcpy(buf, dir);
	strcat(buf, "/");
	strcat(buf, name);
	if (suffix)
		strcat(buf, suffix);
}

static int
ensure_dir(const char *path)
{
	if (mkdir(path, 0775) == 0 || errno == EEXIST)
		return 0;
	printf("N64_PCC_MKDIR_FAIL %s %d\n", path, errno);
	return 1;
}

static void
setup_console(void)
{
	int fd;

	fd = open("/dev/console", O_RDWR, 0);
	if (fd < 0)
		return;
	if (fd != 0)
		dup2(fd, 0);
	dup2(0, 1);
	dup2(0, 2);
	if (fd > 2)
		close(fd);
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
}

static int
file_size(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return -1;
	return (int)st.st_size;
}

static int
decode_wait(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 126;
}

static void print_file_head(const char *path, int max_lines);

static int
run_argv_env(const char *label, const char *cwd, char *const argv[],
    const char *tmpdir)
{
	int pid;
	int status;
	int wpid;
	int i;

	printf("N64_PCC_CMD %s", label);
	for (i = 0; argv[i] != NULL; i++)
		printf(" %s", argv[i]);
	if (tmpdir)
		printf(" TMPDIR=%s", tmpdir);
	printf("\n");
	fflush(stdout);

	pid = fork();
	if (pid < 0) {
		printf("N64_PCC_FORK_FAIL %s %d\n", label, errno);
		return 125;
	}
	if (pid == 0) {
		if (cwd && chdir(cwd) < 0) {
			perror(cwd);
			_exit(126);
		}
		if (tmpdir && setenv("TMPDIR", tmpdir, 1) < 0) {
			perror("TMPDIR");
			_exit(126);
		}
		execv(argv[0], argv);
		perror(argv[0]);
		_exit(127);
	}
	do {
		wpid = wait(&status);
		if (wpid < 0) {
			printf("N64_PCC_WAIT_FAIL %s %d\n", label, errno);
			return 124;
		}
	} while (wpid != pid);
	if (WIFSIGNALED(status))
		printf("N64_PCC_SIGNAL %s %d\n", label, WTERMSIG(status));
	else if (!WIFEXITED(status))
		printf("N64_PCC_ABNORMAL %s\n", label);
	return decode_wait(status);
}

static int
run_argv(const char *label, const char *cwd, char *const argv[])
{
	return run_argv_env(label, cwd, argv, NULL);
}

static void
print_mem(const char *tag)
{
	printf("N64_PCC_MEM %s maxmem=%u sbrk=%08x\n",
	    tag, (unsigned)MAXMEM, (unsigned)sbrk(0));
}

static unsigned
read_fcsr(void)
{
	unsigned fcsr;

	__asm__ volatile("cfc1 %0,$31" : "=r"(fcsr));
	return fcsr;
}

static void
write_fcsr(unsigned fcsr)
{
	__asm__ volatile("ctc1 %0,$31\nnop\nnop\nnop" : : "r"(fcsr));
}

static void
native_fpu_probe_child(const char *tag, int clear_fcsr)
{
	volatile double one;
	volatile double zero;
	volatile double inf;
	volatile double nanv;
	unsigned before;
	unsigned after;
	int inf_ok;
	int nan_ok;
	int exit_code;

	one = 1.0;
	zero = 0.0;
	if (clear_fcsr)
		write_fcsr(0);
	before = read_fcsr();
	printf("N64_PCC_FPU_CHILD_BEGIN %s fcsr=%08x clear=%d\n",
	    tag, before, clear_fcsr);
	fflush(stdout);

	inf = one / zero;
	nanv = zero / zero;
	inf_ok = inf > one;
	nan_ok = nanv != nanv;
	after = read_fcsr();
	exit_code = (inf_ok != 0 && nan_ok != 0) ? 0 : 1;
	printf("N64_PCC_FPU_CHILD_END %s fcsr=%08x inf_ok=%d nan_ok=%d exit=%d\n",
	    tag, after, inf_ok, nan_ok, exit_code);
	fflush(stdout);
	_exit(exit_code);
}

static int
run_native_fpu_probe_one(const char *tag, int clear_fcsr)
{
	int pid;
	int status;
	int wpid;

	pid = fork();
	if (pid < 0) {
		printf("N64_PCC_FPU_FORK_FAIL %s %d\n", tag, errno);
		return 1;
	}
	if (pid == 0)
		native_fpu_probe_child(tag, clear_fcsr);

	do {
		wpid = wait(&status);
		if (wpid < 0) {
			printf("N64_PCC_FPU_WAIT_FAIL %s %d\n", tag, errno);
			return 1;
		}
	} while (wpid != pid);

	printf("N64_PCC_FPU_WAIT %s raw=%08x exited=%d signaled=%d\n",
	    tag, status, WIFEXITED(status), WIFSIGNALED(status));
	if (WIFSIGNALED(status)) {
		printf("N64_PCC_FPU_SIGNAL %s %d\n", tag, WTERMSIG(status));
		return 1;
	} else if (WIFEXITED(status)) {
		printf("N64_PCC_FPU_RC %s %d\n", tag, WEXITSTATUS(status));
		return WEXITSTATUS(status) != 0;
	} else {
		printf("N64_PCC_FPU_ABNORMAL %s\n", tag);
		return 1;
	}
}

static int
run_native_fpu_probe(void)
{
	unsigned fcsr;
	int fails;

	fcsr = read_fcsr();
	printf("N64_PCC_FPU_PARENT_FCSR %08x\n", fcsr);
	fails = 0;
	fails += run_native_fpu_probe_one("inherit", 0);
	fails += run_native_fpu_probe_one("clear", 1);
	return fails;
}

static int
copy_file(const char *from, const char *to)
{
	FILE *in;
	FILE *out;
	char buf[256];
	size_t n;

	in = fopen(from, "r");
	if (!in) {
		printf("N64_PCC_COPY_OPEN_IN_FAIL %s %d\n", from, errno);
		return 1;
	}
	out = fopen(to, "w");
	if (!out) {
		printf("N64_PCC_COPY_OPEN_OUT_FAIL %s %d\n", to, errno);
		fclose(in);
		return 1;
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			printf("N64_PCC_COPY_WRITE_FAIL %s %d\n", to, errno);
			fclose(in);
			fclose(out);
			return 1;
		}
	}
	if (ferror(in)) {
		printf("N64_PCC_COPY_READ_FAIL %s %d\n", from, errno);
		fclose(in);
		fclose(out);
		return 1;
	}
	fclose(in);
	if (fclose(out) != 0) {
		printf("N64_PCC_COPY_CLOSE_FAIL %s %d\n", to, errno);
		return 1;
	}
	return 0;
}

static void
cleanup_driver_temps(void)
{
	char path[32];
	int i;

	for (i = 0; i < 128; i++) {
		snprintf(path, sizeof(path), "/tmp/ctm.%06d", i);
		unlink(path);
	}
}

static void
dump_driver_temps(const char *tag)
{
	char path[32];
	int i;
	int n;
	int size;

	n = 0;
	for (i = 0; i < 128 && n < 8; i++) {
		snprintf(path, sizeof(path), "/tmp/ctm.%06d", i);
		size = file_size(path);
		if (size < 0)
			continue;
		printf("N64_PCC_DRIVER_TEMP %s %s %d\n", tag, path, size);
		print_file_head(path, 40);
		n++;
	}
	if (n == 0)
		printf("N64_PCC_DRIVER_TEMP_NONE %s\n", tag);
}

static int
diagnose_driver_temp(const char *tag, const char *cwd, const char *source,
    const char *i_path)
{
	char tmp_i_path[128];
	char tmp_s_path[128];
	char x_s_path[128];
	char work_s_path[128];
	int rc;
	int size;
	char *ccom_tmp_argv[] = {
		"/usr/libexec/pcc/ccom", "-v", tmp_i_path, tmp_s_path, NULL
	};
	char *cc_s_x_argv[] = {
		"/usr/bin/cc", "-X", "-v", "-S", "-o", x_s_path,
		(char *)source, NULL
	};
	char *cc_s_work_argv[] = {
		"/usr/bin/cc", "-v", "-S", "-o", work_s_path,
		(char *)source, NULL
	};
	int fails;

	fails = 0;
	make_path(tmp_i_path, "/tmp", tag, ".copy.i");
	make_path(tmp_s_path, WORKDIR, tag, ".tmp-ccom.s");
	make_path(x_s_path, WORKDIR, tag, ".cc-X.s");
	make_path(work_s_path, WORKDIR, tag, ".cc-worktmp.s");
	unlink(tmp_i_path);
	unlink(tmp_s_path);
	unlink(x_s_path);
	unlink(work_s_path);

	if (copy_file(i_path, tmp_i_path) == 0) {
		size = file_size(tmp_i_path);
		printf("N64_PCC_TMP_I_SIZE %s %d\n", tag, size);
		if (size >= 0)
			print_file_head(tmp_i_path, 40);
		rc = run_argv("ccom-v-tmp-copy", NULL, ccom_tmp_argv);
		size = file_size(tmp_s_path);
		printf("N64_PCC_PIPE_CCOM_TMP_V_RC %s %d\n", tag, rc);
		printf("N64_PCC_PIPE_CCOM_TMP_V_S_SIZE %s %d\n", tag, size);
		if (size >= 0)
			print_file_head(tmp_s_path, 80);
		if (rc != 0)
			fails++;
	}

	cleanup_driver_temps();
	rc = run_argv("cc-S-v-X", cwd, cc_s_x_argv);
	size = file_size(x_s_path);
	printf("N64_PCC_PIPE_CC_S_X_RC %s %d\n", tag, rc);
	printf("N64_PCC_PIPE_CC_S_X_SIZE %s %d\n", tag, size);
	if (size >= 0)
		print_file_head(x_s_path, 120);
	dump_driver_temps(tag);
	if (rc != 0)
		fails++;

	rc = run_argv_env("cc-S-v-worktmp", cwd, cc_s_work_argv, WORKDIR);
	size = file_size(work_s_path);
	printf("N64_PCC_PIPE_CC_S_WORKTMP_RC %s %d\n", tag, rc);
	printf("N64_PCC_PIPE_CC_S_WORKTMP_SIZE %s %d\n", tag, size);
	if (size >= 0)
		print_file_head(work_s_path, 120);
	if (rc != 0)
		fails++;

	return fails;
}

static void
print_file_head(const char *path, int max_lines)
{
	FILE *fp;
	char line[160];
	int n;

	fp = fopen(path, "r");
	if (!fp) {
		printf("N64_PCC_FILE_OPEN_FAIL %s %d\n", path, errno);
		return;
	}
	printf("N64_PCC_FILE_BEGIN %s\n", path);
	for (n = 0; n < max_lines && fgets(line, sizeof(line), fp); n++)
		fputs(line, stdout);
	if (!feof(fp))
		printf("N64_PCC_FILE_TRUNCATED %s\n", path);
	printf("N64_PCC_FILE_END %s\n", path);
	fclose(fp);
}

static int
write_text_file(const char *path, const char *text)
{
	FILE *fp;

	fp = fopen(path, "w");
	if (!fp) {
		printf("N64_PCC_FILE_WRITE_OPEN_FAIL %s %d\n", path, errno);
		return 1;
	}
	fputs(text, fp);
	if (fclose(fp) != 0) {
		printf("N64_PCC_FILE_WRITE_CLOSE_FAIL %s %d\n", path, errno);
		return 1;
	}
	return 0;
}

static int
setup_var(void)
{
	int rc;

	printf("N64_PCC_SETUP_VAR_BEGIN\n");
	rc = 0;
	rc |= ensure_dir("/var/db");
	rc |= ensure_dir("/var/log");
	rc |= ensure_dir("/var/run");
	rc |= ensure_dir("/var/tmp");
	rc |= ensure_dir("/var/lock");
	if (rc) {
		printf("N64_PCC_SETUP_VAR_END 1\n");
		return 1;
	}
	chmod("/var/tmp", 01777);
	if (write_text_file("/var/run/utmp", "") == 0)
		chmod("/var/run/utmp", 0664);
	if (write_text_file("/var/log/wtmp", "") == 0)
		chmod("/var/log/wtmp", 0664);
	printf("N64_PCC_SETUP_VAR_END 0\n");
	return 0;
}

static void
sleep_forever(void)
{
	for (;;)
		sleep(3600);
}

static int
probe_pipeline(const char *tag, const char *cwd, const char *source)
{
	char i_path[128];
	char s_path[128];
	char ccom_s_path[128];
	char ccom_v_s_path[128];
	char o_path[128];
	int rc;
	int size;
	char *cc_e_argv[] = {
		"/usr/bin/cc", "-v", "-E", "-o", i_path, (char *)source, NULL
	};
	char *cc_s_argv[] = {
		"/usr/bin/cc", "-v", "-S", "-o", s_path, (char *)source, NULL
	};
	char *ccom_argv[] = {
		"/usr/libexec/pcc/ccom", i_path, ccom_s_path, NULL
	};
	char *ccom_v_argv[] = {
		"/usr/libexec/pcc/ccom", "-v", i_path, ccom_v_s_path, NULL
	};
	char *as_argv[] = {
		"/usr/bin/as", "-o", o_path, ccom_s_path, NULL
	};
	char *aout_argv[] = {
		"/usr/bin/aout", "-d", o_path, NULL
	};
	int fails;

	fails = 0;
	make_path(i_path, WORKDIR, tag, ".i");
	make_path(s_path, WORKDIR, tag, ".cc.s");
	make_path(ccom_s_path, WORKDIR, tag, ".ccom.s");
	make_path(ccom_v_s_path, WORKDIR, tag, ".ccom-v.s");
	make_path(o_path, WORKDIR, tag, ".manual.o");
	unlink(i_path);
	unlink(s_path);
	unlink(ccom_s_path);
	unlink(ccom_v_s_path);
	unlink(o_path);

	printf("N64_PCC_PIPE_BEGIN %s\n", tag);
	rc = run_argv("cc-E-v", cwd, cc_e_argv);
	size = file_size(i_path);
	printf("N64_PCC_PIPE_CC_E_RC %s %d\n", tag, rc);
	printf("N64_PCC_PIPE_I_SIZE %s %d\n", tag, size);
	if (size >= 0)
		print_file_head(i_path, 80);
	if (rc != 0)
		fails++;

	rc = run_argv("cc-S-v", cwd, cc_s_argv);
	size = file_size(s_path);
	printf("N64_PCC_PIPE_CC_S_RC %s %d\n", tag, rc);
	printf("N64_PCC_PIPE_CC_S_SIZE %s %d\n", tag, size);
	if (size >= 0)
		print_file_head(s_path, 120);
	if (rc != 0 && file_size(i_path) >= 0)
		fails += diagnose_driver_temp(tag, cwd, source, i_path);
	if (rc != 0)
		fails++;

	if (file_size(i_path) >= 0) {
		rc = run_argv("ccom-v-direct", NULL, ccom_v_argv);
		size = file_size(ccom_v_s_path);
		printf("N64_PCC_PIPE_CCOM_V_RC %s %d\n", tag, rc);
		printf("N64_PCC_PIPE_CCOM_V_S_SIZE %s %d\n", tag, size);
		if (size >= 0)
			print_file_head(ccom_v_s_path, 80);
		if (rc != 0)
			fails++;

		rc = run_argv("ccom-direct", NULL, ccom_argv);
		size = file_size(ccom_s_path);
		printf("N64_PCC_PIPE_CCOM_RC %s %d\n", tag, rc);
		printf("N64_PCC_PIPE_CCOM_S_SIZE %s %d\n", tag, size);
		if (size >= 0)
			print_file_head(ccom_s_path, 120);
		if (rc != 0)
			fails++;
	}

	if (file_size(ccom_s_path) > 0) {
		rc = run_argv("as-direct", NULL, as_argv);
		size = file_size(o_path);
		printf("N64_PCC_PIPE_AS_RC %s %d\n", tag, rc);
		printf("N64_PCC_PIPE_OBJ_SIZE %s %d\n", tag, size);
		if (rc != 0)
			fails++;
		if (size > 0 && size <= AOUT_MAX_BYTES) {
			rc = run_argv("aout-direct-obj", NULL, aout_argv);
			printf("N64_PCC_PIPE_AOUT_OBJ_RC %s %d\n", tag, rc);
			if (rc != 0)
				fails++;
		} else if (size > AOUT_MAX_BYTES) {
			printf("N64_PCC_PIPE_AOUT_OBJ_SKIP %s %d\n", tag, size);
		}
	}
	if (fails)
		printf("N64_PCC_PIPE_FAIL %s %d\n", tag, fails);
	printf("N64_PCC_PIPE_END %s\n", tag);
	return fails;
}

static void
diagnose_compile_failure(const struct test_case *tc, const char *srcdir)
{
	char asm_path[128];
	char obj_path[128];
	int rc;
	int size;
	char *asm_argv[] = {
		"/usr/bin/cc", "-S", "-o", asm_path, (char *)tc->source, NULL
	};
	char *obj_argv[] = {
		"/usr/bin/cc", "-c", "-o", obj_path, (char *)tc->source, NULL
	};
	char *nm_argv[] = {
		"/usr/bin/nm", obj_path, NULL
	};
	char *aout_obj_argv[] = {
		"/usr/bin/aout", "-d", obj_path, NULL
	};

	make_path(asm_path, WORKDIR, tc->out, ".s");
	make_path(obj_path, WORKDIR, tc->out, ".o");
	unlink(asm_path);
	unlink(obj_path);

	printf("N64_PCC_DIAG_COMPILE_FAIL %s\n", tc->name);
	rc = run_argv("asm", srcdir, asm_argv);
	printf("N64_PCC_ASM_RC %s %d\n", tc->name, rc);
	if (file_size(asm_path) > 0)
		print_file_head(asm_path, 120);

	rc = run_argv("obj", srcdir, obj_argv);
	printf("N64_PCC_OBJ_RC %s %d\n", tc->name, rc);
	size = file_size(obj_path);
	printf("N64_PCC_OBJ_SIZE %s %d\n", tc->name, size);
	if (size > 0) {
		rc = run_argv("nm-obj", NULL, nm_argv);
		printf("N64_PCC_NM_OBJ_RC %s %d\n", tc->name, rc);
		if (size <= AOUT_MAX_BYTES) {
			rc = run_argv("aout-obj", NULL, aout_obj_argv);
			printf("N64_PCC_AOUT_OBJ_RC %s %d\n", tc->name, rc);
		} else {
			printf("N64_PCC_AOUT_OBJ_SKIP %s %d\n", tc->name,
			    size);
		}
	}
	probe_pipeline(tc->name, srcdir, tc->source);
}

static int
run_test(const struct test_case *tc)
{
	char srcdir[128];
	char outpath[128];
	int rc;
	int size;
	char *compile_argv_no_math[] = {
		"/usr/bin/cc", "-L", "/usr/lib", "-o", outpath,
		(char *)tc->source, NULL
	};
	char *compile_argv_math[] = {
		"/usr/bin/cc", "-L", "/usr/lib", "-o", outpath,
		(char *)tc->source, "-lm", NULL
	};
	char *size_argv[] = {
		"/usr/bin/size", outpath, NULL
	};
	char *aout_argv[] = {
		"/usr/bin/aout", outpath, NULL
	};
	char *run_argvv[] = {
		outpath, NULL
	};

	make_path(srcdir, SRCDIR, tc->subdir, NULL);
	make_path(outpath, BINDIR, tc->out, NULL);
	unlink(outpath);

	printf("N64_PCC_TEST_BEGIN %s\n", tc->name);
	rc = run_argv("compile", srcdir,
	    tc->need_math ? compile_argv_math : compile_argv_no_math);
	printf("N64_PCC_COMPILE_RC %s %d\n", tc->name, rc);

	size = file_size(outpath);
	printf("N64_PCC_OUTPUT_SIZE %s %d\n", tc->name, size);
	if (rc != 0 || size <= 0) {
		if (size > 0) {
			if (size <= AOUT_MAX_BYTES) {
				rc = run_argv("aout-partial", NULL, aout_argv);
				printf("N64_PCC_AOUT_PARTIAL_RC %s %d\n",
				    tc->name, rc);
			} else {
				printf("N64_PCC_AOUT_PARTIAL_SKIP %s %d\n",
				    tc->name, size);
			}
		}
		diagnose_compile_failure(tc, srcdir);
		printf("N64_PCC_TEST_END %s\n", tc->name);
		return 1;
	}

	rc = run_argv("size", NULL, size_argv);
	printf("N64_PCC_SIZE_RC %s %d\n", tc->name, rc);
	if (size <= AOUT_MAX_BYTES) {
		rc = run_argv("aout", NULL, aout_argv);
		printf("N64_PCC_AOUT_RC %s %d\n", tc->name, rc);
	} else {
		printf("N64_PCC_AOUT_SKIP %s %d\n", tc->name, size);
	}
	rc = run_argv("run", NULL, run_argvv);
	printf("N64_PCC_RUN_RC %s %d\n", tc->name, rc);
	printf("N64_PCC_TEST_END %s\n", tc->name);
	return rc != 0;
}

static int
run_group(const struct test_case *tests, int count)
{
	int i;
	int fails;

	fails = 0;
	for (i = 0; i < count; i++)
		fails += run_test(&tests[i]);
	return fails;
}

static int
run_linpack_comparison(void)
{
	int fails;
	int rc;
	char *gcc_argv[] = { "/root/linpack-gcc", NULL };
	char *pcc_argv[] = { "/root/linpack-pcc", NULL };

	fails = 0;
	printf("N64_LINPACK_CONFIG array_size=120 min_seconds=1\n");
	if (setenv("LINPACK_ARRAY_SIZE", "120", 1) < 0 ||
	    setenv("LINPACK_MIN_SECONDS", "1", 1) < 0) {
		printf("N64_LINPACK_ENV_FAIL %d\n", errno);
		return 1;
	}

	printf("N64_LINPACK_BEGIN gcc\n");
	rc = run_argv("linpack-gcc", NULL, gcc_argv);
	printf("N64_LINPACK_RC gcc %d\n", rc);
	printf("N64_LINPACK_END gcc\n");
	fails += rc != 0;

	printf("N64_LINPACK_BEGIN pcc\n");
	rc = run_argv("linpack-pcc", NULL, pcc_argv);
	printf("N64_LINPACK_RC pcc %d\n", rc);
	printf("N64_LINPACK_END pcc\n");
	fails += rc != 0;
	return fails;
}

static int
run_linpack_kernel_comparison(void)
{
	int fails;
	int rc;
	char *gcc_argv[] = { "/root/linpack-kernels-gcc", NULL };
	char *pcc_argv[] = { "/root/linpack-kernels-pcc", NULL };

	fails = 0;
	printf("N64_LINPACK_KERNEL_CONFIG array_size=120 min_seconds=1\n");
	if (setenv("LINPACK_KERNEL_ARRAY_SIZE", "120", 1) < 0 ||
	    setenv("LINPACK_KERNEL_MIN_SECONDS", "1", 1) < 0) {
		printf("N64_LINPACK_KERNEL_ENV_FAIL %d\n", errno);
		return 1;
	}

	printf("N64_LINPACK_KERNEL_BEGIN gcc\n");
	rc = run_argv("linpack-kernels-gcc", NULL, gcc_argv);
	printf("N64_LINPACK_KERNEL_RC gcc %d\n", rc);
	printf("N64_LINPACK_KERNEL_END gcc\n");
	fails += rc != 0;

	printf("N64_LINPACK_KERNEL_BEGIN pcc\n");
	rc = run_argv("linpack-kernels-pcc", NULL, pcc_argv);
	printf("N64_LINPACK_KERNEL_RC pcc %d\n", rc);
	printf("N64_LINPACK_KERNEL_END pcc\n");
	fails += rc != 0;
	return fails;
}

static int
run_compiler_bench_comparison(void)
{
	int fails;
	int rc;
	char *gcc_argv[] = { "/root/mips-compiler-bench-gcc", NULL };
	char *pcc_argv[] = { "/root/mips-compiler-bench-pcc", NULL };

	fails = 0;
	printf("N64_COMPILER_BENCH_CONFIG min_seconds=1\n");
	if (setenv("MIPS_COMPILER_BENCH_MIN_SECONDS", "1", 1) < 0) {
		printf("N64_COMPILER_BENCH_ENV_FAIL %d\n", errno);
		return 1;
	}

	printf("N64_COMPILER_BENCH_BEGIN gcc\n");
	rc = run_argv("mips-compiler-bench-gcc", NULL, gcc_argv);
	printf("N64_COMPILER_BENCH_RC gcc %d\n", rc);
	printf("N64_COMPILER_BENCH_END gcc\n");
	fails += rc != 0;

	printf("N64_COMPILER_BENCH_BEGIN pcc\n");
	rc = run_argv("mips-compiler-bench-pcc", NULL, pcc_argv);
	printf("N64_COMPILER_BENCH_RC pcc %d\n", rc);
	printf("N64_COMPILER_BENCH_END pcc\n");
	fails += rc != 0;
	return fails;
}

static int
run_debug(int run_extended)
{
	int fails;
	int i;
	char probe_path[128];

	printf("N64_PCC_DEBUG_BEGIN\n");
	printf("N64_PCC_DEBUG_MODE %s\n", run_extended ? "all" : "primary");
	print_mem("begin");
	fails = run_native_fpu_probe();
	if (ensure_dir("/var/tmp") || ensure_dir(WORKDIR) ||
	    ensure_dir(BINDIR)) {
		printf("N64_PCC_DEBUG_END 125\n");
		return 125;
	}
	if (write_text_file(WORKDIR "/min.c",
	    "int main(void) { return 7; }\n") == 0)
		fails += probe_pipeline("manual__min", WORKDIR, "min.c");
	for (i = 0; i < (int)(sizeof(fp_probe_tests) /
	    sizeof(fp_probe_tests[0])); i++) {
		make_path(probe_path, WORKDIR, fp_probe_tests[i].file, NULL);
		if (write_text_file(probe_path, fp_probe_tests[i].source) == 0)
			fails += probe_pipeline(fp_probe_tests[i].tag, WORKDIR,
			    fp_probe_tests[i].file);
	}

	fails += run_group(primary_tests,
	    sizeof(primary_tests) / sizeof(primary_tests[0]));
	if (run_extended) {
		fails += run_group(extended_tests,
		    sizeof(extended_tests) / sizeof(extended_tests[0]));
		fails += run_linpack_comparison();
		fails += run_linpack_kernel_comparison();
		fails += run_compiler_bench_comparison();
	}

	printf("N64_PCC_DEBUG_END %d\n", fails);
	return fails ? 1 : 0;
}

static int
run_boot_rc(void)
{
	int rc;
	int mrc;
	char *mkfs_argv[] = {
		"/sbin/mkfs", "-i", "4096", "/dev/ram0", NULL
	};
	char *mount_direct_argv[] = {
		"/sbin/mount", "-o", "rw", "/dev/ram0", "/var", NULL
	};
	char *runner_argv[] = {
		"/root/n64-pcc-debug-runner", "all", NULL
	};

	setup_console();
	printf("N64_PCC_DEBUG_RC_BEGIN\n");
	rc = run_argv("rc-mkfs", NULL, mkfs_argv);
	printf("N64_PCC_DEBUG_MKFS_RC %d\n", rc);
	if (rc == 0) {
		mrc = run_argv("rc-mount-direct", NULL, mount_direct_argv);
		printf("N64_PCC_DEBUG_MOUNT_DIRECT_RC %d\n", mrc);
		if (mrc == 0) {
			rc = setup_var();
			printf("N64_PCC_DEBUG_SETUP_VAR_RC %d\n", rc);
		} else {
			printf("N64_PCC_DEBUG_VAR_MOUNT_FAIL\n");
			rc = mrc;
		}
	} else {
		printf("N64_PCC_DEBUG_VAR_MKFS_FAIL\n");
	}

	if (rc == 0) {
		rc = run_argv("rc-runner", NULL, runner_argv);
		printf("N64_PCC_DEBUG_RUNNER_RC %d\n", rc);
	} else {
		printf("N64_PCC_DEBUG_RUNNER_SKIP %d\n", rc);
	}
	printf("N64_PCC_DEBUG_RC_END\n");
	fflush(stdout);

	return 0;
}

int
main(int argc, char **argv)
{
	const char *arg0;

	arg0 = strrchr(argv[0], '/');
	arg0 = arg0 ? arg0 + 1 : argv[0];
	if (argc > 1 && strcmp(argv[1], "/etc/rc") == 0)
		return run_boot_rc();
	if (strcmp(arg0, "sh") == 0 || strcmp(arg0, "-") == 0) {
		setup_console();
		printf("N64_PCC_SH_STUB unsupported argv0=%s argc=%d\n",
		    argv[0], argc);
		sleep_forever();
		return 0;
	}

	return run_debug(argc > 1 && strcmp(argv[1], "all") == 0);
}
