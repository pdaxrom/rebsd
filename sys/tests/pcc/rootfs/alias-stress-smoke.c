#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define NODE_COUNT 6
#define LEAF_COUNT 3

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SMOKE_LITTLE_ENDIAN 1
#elif defined(__MIPSEL__) || defined(__mipsel__) || defined(TARGET_LITTLE_ENDIAN)
#define SMOKE_LITTLE_ENDIAN 1
#else
#define SMOKE_LITTLE_ENDIAN 0
#endif

#if SMOKE_LITTLE_ENDIAN
#define EXPECT_FORWARD_CHECKSUM 2594742500UL
#define EXPECT_CHAR_ALIAS_CHECKSUM 4598874UL
#else
#define EXPECT_FORWARD_CHECKSUM 2085209764UL
#define EXPECT_CHAR_ALIAS_CHECKSUM 2759576698UL
#endif

#if defined(__i386__)
#define EXPECT_REVERSE_CHECKSUM 1602776859UL
#else
#define EXPECT_REVERSE_CHECKSUM 499871515UL
#endif

struct alias_leaf {
	unsigned char code;
	short delta;
	unsigned long weight;
};

union alias_payload {
	unsigned long mask;
	unsigned char bytes[4];
};

struct alias_node {
	int id;
	struct alias_leaf leaf[LEAF_COUNT];
	union alias_payload payload;
};

typedef unsigned long (*alias_walk_fn)(struct alias_node *, int);

static int
bad(const char *tag)
{
	printf("alias-stress-smoke fail: %s\n", tag);
	return 1;
}

static int
bad_ulong(const char *tag, unsigned long got, unsigned long expect)
{
	printf("alias-stress-smoke fail: %s got=%lu expect=%lu\n",
	    tag, got, expect);
	return 1;
}

static void
init_nodes(struct alias_node *nodes, int n)
{
	struct alias_node *p;
	int i, j;

	for (i = 0, p = nodes; i != n; ++i, ++p) {
		p->id = i * 3 + 1;
		for (j = 0; j != LEAF_COUNT; ++j) {
			p->leaf[j].code = (unsigned char)(i * 17 + j * 5 + 3);
			p->leaf[j].delta = (short)(i * 11 - j * 7);
			p->leaf[j].weight = 1000UL + (unsigned long)i * 101UL +
			    (unsigned long)j * 13UL;
		}
		p->payload.mask = 0x10203040UL +
		    (unsigned long)i * 0x01010101UL;
	}
}

static unsigned long
checksum_forward(struct alias_node *base, int n)
{
	struct alias_node *p;
	struct alias_leaf *lp;
	unsigned char *bp;
	unsigned long sum;
	int idx;

	sum = 2166136261UL;
	for (p = base; p != base + n; ++p) {
		idx = (int)(p - base);
		sum ^= (unsigned long)(p->id + idx);
		sum *= 16777619UL;
		for (lp = p->leaf; lp != p->leaf + LEAF_COUNT; ++lp) {
			sum ^= (unsigned long)lp->code;
			sum *= 16777619UL;
			sum ^= (unsigned long)(unsigned short)lp->delta;
			sum *= 16777619UL;
			sum ^= lp->weight;
			sum *= 16777619UL;
		}
		for (bp = p->payload.bytes; bp != p->payload.bytes + 4; ++bp) {
			sum ^= (unsigned long)(*bp + (bp - p->payload.bytes));
			sum *= 16777619UL;
		}
	}
	return sum;
}

static unsigned long
checksum_reverse(struct alias_node *base, int n)
{
	struct alias_node *p;
	int idx, j;
	unsigned long sum;

	sum = 0x811c9dc5UL;
	for (p = base + n; p != base; ) {
		--p;
		idx = (int)(p - base);
		sum ^= (unsigned long)(p->id * 33 + idx);
		sum *= 16777619UL;
		for (j = LEAF_COUNT; j != 0; ) {
			--j;
			sum ^= p->leaf[j].weight -
			    (unsigned long)p->leaf[j].delta;
			sum *= 16777619UL;
		}
	}
	return sum;
}

static struct alias_leaf
select_leaf(struct alias_node node, int idx)
{
	return node.leaf[idx];
}

static int
check_pointer_and_dispatch(void)
{
	struct alias_node nodes[NODE_COUNT];
	struct alias_leaf leaf;
	alias_walk_fn walkers[2];
	unsigned long got;

	init_nodes(nodes, NODE_COUNT);
	if ((char *)&nodes[5] - (char *)&nodes[0] !=
	    (ptrdiff_t)(5 * sizeof(nodes[0])))
		return bad("node byte difference");
	if (&nodes[4].leaf[2] - &nodes[4].leaf[0] != 2)
		return bad("leaf pointer difference");

	leaf = select_leaf(nodes[4], 2);
	if (leaf.code != 81 || leaf.delta != 30 || leaf.weight != 1430UL)
		return bad("struct by value");

	walkers[0] = checksum_forward;
	walkers[1] = checksum_reverse;
	got = (*walkers[0])(nodes, NODE_COUNT);
	if (got != EXPECT_FORWARD_CHECKSUM)
		return bad_ulong("forward checksum", got, EXPECT_FORWARD_CHECKSUM);
	got = (*walkers[1])(nodes, NODE_COUNT);
	if (got != EXPECT_REVERSE_CHECKSUM)
		return bad_ulong("reverse checksum", got,
		    EXPECT_REVERSE_CHECKSUM);
	return 0;
}

static int
check_char_alias(void)
{
	struct alias_node nodes[NODE_COUNT];
	unsigned char *raw;
	size_t off;
	unsigned long got;

	init_nodes(nodes, NODE_COUNT);
	raw = (unsigned char *)&nodes[2];
	off = offsetof(struct alias_node, leaf) + sizeof(struct alias_leaf) +
	    offsetof(struct alias_leaf, code);
	raw[off] ^= 0x5aU;
	if (nodes[2].leaf[1].code != 112)
		return bad_ulong("char alias update",
		    (unsigned long)nodes[2].leaf[1].code, 112UL);
	got = checksum_forward(nodes, NODE_COUNT);
	if (got != EXPECT_CHAR_ALIAS_CHECKSUM)
		return bad_ulong("char alias checksum", got,
		    EXPECT_CHAR_ALIAS_CHECKSUM);
	return 0;
}

static int
check_copy_overlap(void)
{
	struct alias_node nodes[NODE_COUNT];
	struct alias_node copy[NODE_COUNT + 1];

	init_nodes(nodes, NODE_COUNT);
	memset(copy, 0, sizeof(copy));
	memcpy(copy + 1, nodes, 3 * sizeof(nodes[0]));
	if (copy[1].id != 1 || copy[2].id != 4 || copy[3].id != 7)
		return bad("memcpy structs");
	memmove(copy + 2, copy + 1, 2 * sizeof(copy[0]));
	if (copy[2].id != 1 || copy[3].id != 4)
		return bad("memmove overlap");
	if (copy[2].leaf[1].weight != 1013UL ||
	    copy[3].leaf[2].weight != 1127UL)
		return bad("memmove payload");
	return 0;
}

int
main(void)
{
	if (sizeof(unsigned long) != 4)
		return bad("unsigned long size");
	if (sizeof(void *) != 4)
		return bad("pointer size");
	if (check_pointer_and_dispatch())
		return 1;
	if (check_char_alias())
		return 1;
	if (check_copy_overlap())
		return 1;
	printf("alias stress smoke ok\n");
	return 0;
}
