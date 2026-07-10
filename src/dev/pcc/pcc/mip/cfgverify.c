/*	$Id$	*/
/*
 * Verify pass2 control-flow and SSA metadata without changing the IR.
 */

#include "pass2.h"

#include <string.h>

static int
block_member(struct p2env *p2e, struct basicblock *target)
{
	struct basicblock *bb;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem)
		if (bb == target)
			return 1;
	return 0;
}

static int
child_count(struct basicblock *from, struct basicblock *to)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	SLIST_FOREACH(cn, &from->child, chld)
		if (cn->bblock == to)
			count++;
	return count;
}

static int
parent_count(struct basicblock *to, struct basicblock *from)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	SLIST_FOREACH(cn, &to->parents, cfgelem)
		if (cn->bblock == from)
			count++;
	return count;
}

static int
children_total(struct basicblock *bb)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	SLIST_FOREACH(cn, &bb->child, chld)
		count++;
	return count;
}

static int
parents_total(struct basicblock *bb)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	SLIST_FOREACH(cn, &bb->parents, cfgelem)
		count++;
	return count;
}

struct basicblock *
cfg_label_block(struct p2env *p2e, int label)
{
	struct basicblock *bb;
	int index;

	index = label - (int)p2e->labinfo.low;
	if (index < 0 || index >= p2e->labinfo.size) {
		comperr("CFG label %d outside [%u,%u]", label,
		    p2e->labinfo.low,
		    p2e->labinfo.low + p2e->labinfo.size - 1);
		return NULL;
	}
	bb = p2e->labinfo.arr[index];
	if (bb == NULL) {
		comperr("CFG label %d has no basic block", label);
		return NULL;
	}
	return bb;
}

static int
computed_label_count(struct p2env *p2e, int label)
{
	int *lp;
	int count;

	count = 0;
	for (lp = p2e->epp->ip_labels; *lp; lp++)
		if (*lp == label)
			count++;
	return count;
}

static void
verify_expected_children(struct p2env *p2e, struct basicblock *bb,
    const char *stage)
{
	struct basicblock *next, *target;
	NODE *p;
	int *lp;
	int count, expected, label;

	count = children_total(bb);
	if (bb->first->type == IP_EPILOG) {
		if (count != 0)
			comperr("CFG verifier %s: epilogue block %d has children",
			    stage, bb->bbnum);
		return;
	}

	next = DLIST_NEXT(bb, bbelem);
	if (next == &p2e->bblocks) {
		comperr("CFG verifier %s: block %d has no epilogue successor",
		    stage, bb->bbnum);
		return;
	}

	if (bb->last->type == IP_NODE) {
		p = bb->last->ip_node;
		if (p->n_op == GOTO) {
			if (p->n_left->n_op == ICON) {
				target = cfg_label_block(p2e,
				    (int)getlval(p->n_left));
				if (count != 1 || child_count(bb, target) != 1)
					comperr("CFG verifier %s: bad direct goto in block %d",
					    stage, bb->bbnum);
				return;
			}
			expected = 0;
			for (lp = p2e->epp->ip_labels; *lp; lp++) {
				target = cfg_label_block(p2e, *lp);
				expected++;
				if (child_count(bb, target) !=
				    computed_label_count(p2e, *lp))
					comperr("CFG verifier %s: bad computed goto in block %d",
					    stage, bb->bbnum);
			}
			if (count != expected)
				comperr("CFG verifier %s: computed goto edge count in block %d",
				    stage, bb->bbnum);
			return;
		}
		if (p->n_op == CBRANCH) {
			label = (int)getlval(p->n_right);
			target = cfg_label_block(p2e, label);
			expected = target == next ? 2 : 1;
			if (count != 2 || child_count(bb, target) != expected ||
			    child_count(bb, next) != expected)
				comperr("CFG verifier %s: bad conditional edges in block %d",
				    stage, bb->bbnum);
			return;
		}
	}

	if (count != 1 || child_count(bb, next) != 1)
		comperr("CFG verifier %s: bad fallthrough in block %d",
		    stage, bb->bbnum);
}

void
cfg_verify(struct p2env *p2e, const char *stage)
{
	struct interpass *ip, *pole;
	struct basicblock *bb;
	struct cfgnode *cn;
	int blocks, i, index;

	pole = &p2e->ipole;
	ip = DLIST_NEXT(pole, qelem);
	blocks = 0;
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		if (bb->bbnum != blocks)
			comperr("CFG verifier %s: block number %d, expected %d",
			    stage, bb->bbnum, blocks);
		if (bb->first == NULL || bb->last == NULL || bb->first != ip) {
			comperr("CFG verifier %s: broken extent for block %d",
			    stage, bb->bbnum);
			return;
		}
		for (;;) {
			if (ip == pole) {
				comperr("CFG verifier %s: unterminated block %d",
				    stage, bb->bbnum);
				return;
			}
			if (ip == bb->last) {
				ip = DLIST_NEXT(ip, qelem);
				break;
			}
			if (ip->type == IP_NODE &&
			    (ip->ip_node->n_op == GOTO ||
			    ip->ip_node->n_op == CBRANCH))
				comperr("CFG verifier %s: terminator inside block %d",
				    stage, bb->bbnum);
			ip = DLIST_NEXT(ip, qelem);
		}
		if (bb->first->type == IP_DEFLAB) {
			index = bb->first->ip_lbl - (int)p2e->labinfo.low;
			if (index < 0 || index >= p2e->labinfo.size ||
			    p2e->labinfo.arr[index] != bb)
				comperr("CFG verifier %s: bad label map in block %d",
				    stage, bb->bbnum);
		}
		blocks++;
	}
	if (ip != pole || blocks != p2e->nbblocks)
		comperr("CFG verifier %s: basic-block coverage mismatch", stage);

	for (i = 0; i < p2e->labinfo.size; i++) {
		bb = p2e->labinfo.arr[i];
		if (bb == NULL)
			continue;
		if (!block_member(p2e, bb) || bb->first->type != IP_DEFLAB ||
		    bb->first->ip_lbl != (int)p2e->labinfo.low + i)
			comperr("CFG verifier %s: stale label entry %d", stage,
			    (int)p2e->labinfo.low + i);
	}

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		SLIST_FOREACH(cn, &bb->child, chld) {
			if (cn->bblock == NULL || !block_member(p2e, cn->bblock)) {
				comperr("CFG verifier %s: invalid child of block %d",
				    stage, bb->bbnum);
				return;
			}
			if (child_count(bb, cn->bblock) !=
			    parent_count(cn->bblock, bb))
				comperr("CFG verifier %s: asymmetric edge from block %d",
				    stage, bb->bbnum);
		}
		SLIST_FOREACH(cn, &bb->parents, cfgelem) {
			if (cn->bblock == NULL || !block_member(p2e, cn->bblock)) {
				comperr("CFG verifier %s: invalid parent of block %d",
				    stage, bb->bbnum);
				return;
			}
			if (parent_count(bb, cn->bblock) !=
			    child_count(cn->bblock, bb))
				comperr("CFG verifier %s: asymmetric parent of block %d",
				    stage, bb->bbnum);
		}
		verify_expected_children(p2e, bb, stage);
	}
}

static int
immediate_dominator(bittype **dom, int block, int nblocks)
{
	int candidate, d, other, deepest;

	candidate = 0;
	for (d = 1; d < nblocks; d++) {
		if (d == block || !TESTBIT(dom[block], d))
			continue;
		deepest = 1;
		for (other = 1; other < nblocks; other++) {
			if (other == block || other == d ||
			    !TESTBIT(dom[block], other))
				continue;
			if (!TESTBIT(dom[d], other)) {
				deepest = 0;
				break;
			}
		}
		if (deepest) {
			if (candidate != 0)
				return -1;
			candidate = d;
		}
	}
	return candidate;
}

void
cfg_verify_dominators(struct p2env *p2e)
{
	struct basicblock *bb, *parent;
	struct cfgnode *cn;
	bittype **dom, *scratch;
	size_t bytes;
	int changed, expected, first, i, idom, iteration, j, k, nblocks;
	int reachable;

	nblocks = p2e->bbinfo.size;
	if (nblocks <= 1 || p2e->bbinfo.arr[1] == NULL)
		comperr("dominator verifier: missing entry block");

	reachable = 0;
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		if (bb->dfnum == 0)
			continue;
		reachable++;
		if (bb->dfnum >= (unsigned)nblocks ||
		    p2e->bbinfo.arr[bb->dfnum] != bb)
			comperr("dominator verifier: bad DFS number in block %d",
			    bb->bbnum);
	}
	if (reachable != nblocks - 1)
		comperr("dominator verifier: reachable block count mismatch");

	bytes = BIT2BYTE(nblocks);
	dom = tmpalloc((size_t)nblocks * sizeof(*dom));
	memset(dom, 0, (size_t)nblocks * sizeof(*dom));
	for (i = 1; i < nblocks; i++) {
		dom[i] = tmpalloc(bytes);
		memset(dom[i], 0, bytes);
		if (i == 1)
			BITSET(dom[i], i);
		else
			for (j = 1; j < nblocks; j++)
				BITSET(dom[i], j);
	}
	scratch = tmpalloc(bytes);
	iteration = 0;
	do {
		changed = 0;
		for (i = 2; i < nblocks; i++) {
			bb = p2e->bbinfo.arr[i];
			first = 1;
			SLIST_FOREACH(cn, &bb->parents, cfgelem) {
				if (cn->bblock->dfnum == 0)
					continue;
				if (first) {
					memcpy(scratch, dom[cn->bblock->dfnum], bytes);
					first = 0;
				} else {
					for (k = 0; k < (int)(bytes / sizeof(bittype)); k++)
						scratch[k] &= dom[cn->bblock->dfnum][k];
				}
			}
			if (first)
				comperr("dominator verifier: block %d has no reachable parent",
				    bb->bbnum);
			BITSET(scratch, i);
			if (memcmp(dom[i], scratch, bytes) != 0) {
				memcpy(dom[i], scratch, bytes);
				changed = 1;
			}
		}
		if (++iteration > nblocks * nblocks)
			comperr("dominator verifier: fixed point did not converge");
	} while (changed);

	bb = p2e->bbinfo.arr[1];
	if (bb->dfparent != 0 || bb->idom != 0)
		comperr("dominator verifier: bad entry metadata");
	for (i = 2; i < nblocks; i++) {
		bb = p2e->bbinfo.arr[i];
		if (bb->dfparent == 0 || bb->dfparent >= (unsigned)nblocks)
			comperr("dominator verifier: bad DFS parent for block %d",
			    bb->bbnum);
		parent = p2e->bbinfo.arr[bb->dfparent];
		if (child_count(parent, bb) == 0)
			comperr("dominator verifier: DFS parent edge missing for block %d",
			    bb->bbnum);
		idom = immediate_dominator(dom, i, nblocks);
		if (idom <= 0 || bb->idom != (unsigned)idom)
			comperr("dominator verifier: bad idom for block %d",
			    bb->bbnum);
	}

	for (i = 1; i < nblocks; i++) {
		parent = p2e->bbinfo.arr[i];
		for (j = 2; j < nblocks; j++) {
			expected = p2e->bbinfo.arr[j]->idom == (unsigned)i;
			if (!!TESTBIT(parent->dfchildren, j) != expected)
				comperr("dominator verifier: bad dominator tree edge %d -> %d",
				    parent->bbnum, p2e->bbinfo.arr[j]->bbnum);
		}
	}
}

void
cfg_verify_phi(struct p2env *p2e, int renamed)
{
	struct basicblock *bb;
	struct phiinfo *phi, *other;
	int high, i, low, nparents;

	low = p2e->ipp->ip_tmpnum;
	high = p2e->epp->ip_tmpnum;
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		nparents = parents_total(bb);
		SLIST_FOREACH(phi, &bb->phi, phielem) {
			if (phi->size != nparents || phi->size <= 0 ||
			    phi->intmpregno == NULL)
				comperr("phi verifier: bad arity in block %d", bb->bbnum);
			if (phi->tmpregno < low || phi->tmpregno >= high)
				comperr("phi verifier: source TEMP out of range in block %d",
				    bb->bbnum);
			if (bb->Aphi == NULL ||
			    !TESTBIT(bb->Aphi, phi->tmpregno - low))
				comperr("phi verifier: missing Aphi bit in block %d",
				    bb->bbnum);
			SLIST_FOREACH(other, &bb->phi, phielem)
				if (other != phi && other->tmpregno == phi->tmpregno)
					comperr("phi verifier: duplicate TEMP in block %d",
					    bb->bbnum);
			if (renamed) {
				if (phi->newtmpregno < low || phi->newtmpregno >= high)
					comperr("phi verifier: renamed TEMP out of range in block %d",
					    bb->bbnum);
				for (i = 0; i < phi->size; i++)
					if (phi->intmpregno[i] != 0 &&
					    (phi->intmpregno[i] < low ||
					    phi->intmpregno[i] >= high))
						comperr("phi verifier: input TEMP out of range in block %d",
						    bb->bbnum);
			} else {
				if (phi->newtmpregno != 0)
					comperr("phi verifier: premature rename in block %d",
					    bb->bbnum);
				for (i = 0; i < phi->size; i++)
					if (phi->intmpregno[i] != 0)
						comperr("phi verifier: premature input in block %d",
						    bb->bbnum);
			}
		}
	}
}
