/* Exercise dense MIPS switch tables, including holes and unsigned wrap. */

static int
signed_switch(int v)
{
	switch (v) {
	case -3: return 31;
	case -2: return 29;
	case -1: return 23;
	case 0: return 19;
	case 2: return 17;
	case 3: return 13;
	case 4: return 11;
	default: return -1;
	}
}

static int
unsigned_switch(unsigned int v)
{
	switch (v) {
	case 0xfffffff8U: return 2;
	case 0xfffffff9U: return 3;
	case 0xfffffffaU: return 5;
	case 0xfffffffcU: return 7;
	case 0xfffffffdU: return 11;
	case 0xfffffffeU: return 13;
	case 0xffffffffU: return 17;
	default: return -1;
	}
}

static int
second_switch(int v)
{
	int r;

	switch (v) {
	case 10: r = 1; break;
	case 11: r = 4; break;
	case 12: r = 9; break;
	case 13: r = 16; break;
	case 14: r = 25; break;
	case 15: r = 36; break;
	default: r = 0; break;
	}
	return r;
}

static int
paired_switch(int a, int b)
{
	int r;

	switch (a) {
	case 20: r = 1; break;
	case 21: r = 2; break;
	case 22: r = 3; break;
	case 23: r = 4; break;
	case 24: r = 5; break;
	case 25: r = 6; break;
	default: r = -20; break;
	}
	switch (b) {
	case -12: r += 2; break;
	case -11: r += 3; break;
	case -10: r += 4; break;
	case -9: r += 8; break;
	case -8: r += 16; break;
	case -7: r += 64; break;
	}
	return r;
}

static const unsigned int loop_values[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 6, 0
};

static int
loop_switch(int count)
{
	int acc, i;

	acc = 0;
	for (i = 0; i < count; ++i) {
		switch (loop_values[i]) {
		case 0: acc += 1; break;
		case 1: acc += 2; break;
		case 2: acc += 4; break;
		case 3: acc += 8; break;
		case 4: acc += 16; break;
		case 5: acc += 32; break;
		case 6: acc += 64; break;
		default: acc += 128; break;
		}
	}
	return acc;
}

int
main(void)
{
	static const int signed_want[] = { -1, -1, 31, 29, 23, 19,
	    -1, 17, 13, 11, -1 };
	static const unsigned int unsigned_values[] = {
	    0xfffffff7U, 0xfffffff8U, 0xfffffff9U, 0xfffffffaU,
	    0xfffffffbU, 0xfffffffcU, 0xfffffffdU, 0xfffffffeU,
	    0xffffffffU, 0U
	};
	static const int unsigned_want[] = { -1, 2, 3, 5, -1, 7, 11, 13,
	    17, -1 };
	static const int second_want[] = { 0, 1, 4, 9, 16, 25, 36, 0 };
	int i;

	for (i = -5; i <= 5; ++i)
		if (signed_switch(i) != signed_want[i + 5])
			return 1;
	for (i = 0; i < 10; ++i)
		if (unsigned_switch(unsigned_values[i]) != unsigned_want[i])
			return 2;
	for (i = 9; i <= 16; ++i)
		if (second_switch(i) != second_want[i - 9])
			return 3;
	if (paired_switch(20, -12) != 3 ||
	    paired_switch(25, -7) != 70 ||
	    paired_switch(19, -9) != -12 ||
	    paired_switch(22, 0) != 3 ||
	    paired_switch(26, -10) != -16)
		return 4;
	if (loop_switch(10) != 320)
		return 5;
	return 0;
}
