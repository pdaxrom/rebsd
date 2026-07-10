/*	$Id$	*/
/*
 * Split SSA critical edges and lower phi functions to parallel copies.
 */

#include "pass2.h"

#include <string.h>

#define	mktemp(n, t)	mklnode(TEMP, 0, n, t)

struct parallel_copy {
	int dst;
	int src;
	TWORD type;
	int pending;
};

struct copy_inserter {
	struct interpass *before;
	struct interpass *cursor;
};

static int
edge_count(struct basicblock *bb, int children)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	if (children) {
		SLIST_FOREACH(cn, &bb->child, chld)
			count++;
	} else {
		SLIST_FOREACH(cn, &bb->parents, cfgelem)
			count++;
	}
	return count;
}

static int
block_label(struct basicblock *bb)
{
	if (bb->first->type != IP_DEFLAB) {
		comperr("SSA edge destination block %d has no label", bb->bbnum);
		return 0;
	}
	return bb->first->ip_lbl;
}

static struct interpass *
new_label(int label)
{
	struct interpass *ip;

	ip = tmpalloc(sizeof(*ip));
	memset(ip, 0, sizeof(*ip));
	ip->type = IP_DEFLAB;
	ip->ip_lbl = label;
	return ip;
}

static struct interpass *
new_goto(int label)
{
	return ipnode(mkunode(GOTO, mklnode(ICON, label, 0, INT), 0, INT));
}

static void
insert_after(struct interpass **cursor, struct interpass *ip)
{
	DLIST_INSERT_AFTER(*cursor, ip, qelem);
	*cursor = ip;
}

static void
isolate_branch_pad(struct basicblock *destination, int label)
{
	struct interpass *ip, *previous;

	previous = DLIST_PREV(destination->first, qelem);
	if (previous->type == IP_NODE && previous->ip_node->n_op == GOTO)
		return;
	ip = new_goto(label);
	DLIST_INSERT_BEFORE(destination->first, ip, qelem);
}

/*
 * Keep each critical CBRANCH edge in a dedicated block.  Fallthrough pads
 * must be installed first.  Branch pads go next to their destination, after
 * making any physical fallthrough predecessor jump over the pad.
 */
int
ssa_split_critical_edges(struct p2env *p2e)
{
	struct basicblock *bb, *branch, *fall;
	struct cfgnode *cn;
	struct interpass *cursor, *ip, *term;
	NODE *p;
	int branch_label, fall_label, new_branch_label, new_fall_label;

	/* Computed goto destinations cannot be retargeted in the pass2 IR. */
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		if (edge_count(bb, 1) <= 1 || bb->last->type != IP_NODE)
			continue;
		p = bb->last->ip_node;
		if (p->n_op != GOTO || p->n_left->n_op == ICON)
			continue;
		SLIST_FOREACH(cn, &bb->child, chld)
			if (edge_count(cn->bblock, 0) > 1)
				return 0;
	}

	/* Split critical physical fallthrough edges without moving the target. */
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		term = bb->last;
		if (term->type != IP_NODE || term->ip_node->n_op != CBRANCH)
			continue;
		fall = DLIST_NEXT(bb, bbelem);
		if (edge_count(fall, 0) <= 1)
			continue;
		fall_label = block_label(fall);
		cursor = term;
		new_fall_label = getlab2();
		insert_after(&cursor, new_label(new_fall_label));
		insert_after(&cursor, new_goto(fall_label));
	}

	/* Split critical taken edges while preserving the source fallthrough. */
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		term = bb->last;
		if (term->type != IP_NODE || term->ip_node->n_op != CBRANCH)
			continue;
		p = term->ip_node;
		branch_label = (int)getlval(p->n_right);
		branch = cfg_label_block(p2e, branch_label);
		if (edge_count(branch, 0) <= 1)
			continue;

		new_branch_label = getlab2();
		setlval(p->n_right, new_branch_label);
		if (!logop(p->n_left->n_op))
			comperr("SSA CBRANCH condition is not a logical operator");
		p->n_left->n_label = new_branch_label;

		isolate_branch_pad(branch, branch_label);
		ip = new_label(new_branch_label);
		DLIST_INSERT_BEFORE(branch->first, ip, qelem);
		ip = new_goto(branch_label);
		DLIST_INSERT_BEFORE(branch->first, ip, qelem);
	}
	return 1;
}

static void
emit_copy(struct copy_inserter *where, int dst, int src, TWORD type)
{
	struct interpass *ip;

	ip = ipnode(mkbinode(ASSIGN, mktemp(dst, type), mktemp(src, type),
	    type));
	if (where->before != NULL) {
		DLIST_INSERT_BEFORE(where->before, ip, qelem);
	} else {
		insert_after(&where->cursor, ip);
	}
}

static int
destination_is_live_source(struct parallel_copy *copy, int count, int index)
{
	int i;

	for (i = 0; i < count; i++)
		if (i != index && copy[i].pending &&
		    copy[i].src == copy[index].dst)
			return 1;
	return 0;
}

static void
lower_parallel_copies(struct p2env *p2e, struct copy_inserter *where,
    struct parallel_copy *copy, int count)
{
	int done, i, j, pending, temporary;
	TWORD type;

	pending = count;
	while (pending != 0) {
		done = 0;
		for (i = 0; i < count; i++) {
			if (!copy[i].pending ||
			    destination_is_live_source(copy, count, i))
				continue;
			emit_copy(where, copy[i].dst, copy[i].src, copy[i].type);
			copy[i].pending = 0;
			pending--;
			done = 1;
		}
		if (done)
			continue;

		/* A cycle remains.  Preserve one old destination value. */
		for (i = 0; i < count && !copy[i].pending; i++)
			continue;
		if (i == count)
			comperr("SSA parallel-copy cycle disappeared");
		temporary = p2e->epp->ip_tmpnum++;
		type = copy[i].type;
		emit_copy(where, temporary, copy[i].dst, type);
		for (j = 0; j < count; j++) {
			if (!copy[j].pending || copy[j].src != copy[i].dst)
				continue;
			if (copy[j].type != type)
				comperr("SSA parallel-copy TEMP has inconsistent types");
			copy[j].src = temporary;
		}
	}
}

static void
edge_inserter(struct basicblock *parent, struct basicblock *bb,
    struct copy_inserter *where)
{
	struct cfgnode *cn;
	int children;

	children = 0;
	SLIST_FOREACH(cn, &parent->child, chld) {
		children++;
		if (cn->bblock != bb)
			comperr("SSA phi predecessor block %d has another successor",
			    parent->bbnum);
	}
	if (children != 1)
		comperr("SSA phi predecessor block %d has %d successors",
		    parent->bbnum, children);

	where->before = NULL;
	where->cursor = NULL;
	if (parent->last->type == IP_NODE &&
	    parent->last->ip_node->n_op == GOTO) {
		where->before = parent->last;
		return;
	}
	if (DLIST_NEXT(parent, bbelem) == bb) {
		where->cursor = parent->last;
		return;
	}
	comperr("SSA phi edge from block %d has no insertion point",
	    parent->bbnum);
}

void
ssa_lower_phi(struct p2env *p2e)
{
	struct basicblock *bb, *parent;
	struct cfgnode *cn;
	struct copy_inserter where;
	struct parallel_copy *copy;
	struct phiinfo *phi;
	int count, edge, nphi;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		nphi = 0;
		SLIST_FOREACH(phi, &bb->phi, phielem)
			nphi++;
		if (nphi == 0)
			continue;
		copy = tmpalloc((size_t)nphi * sizeof(*copy));
		edge = 0;
		SLIST_FOREACH(cn, &bb->parents, cfgelem) {
			parent = cn->bblock;
			count = 0;
			SLIST_FOREACH(phi, &bb->phi, phielem) {
				if (edge >= phi->size)
					comperr("SSA phi input index outside arity");
				if (phi->intmpregno[edge] <= 0 ||
				    phi->intmpregno[edge] == phi->newtmpregno)
					continue;
				copy[count].dst = phi->newtmpregno;
				copy[count].src = phi->intmpregno[edge];
				copy[count].type = phi->n_type;
				copy[count].pending = 1;
				count++;
			}
			if (count != 0) {
				edge_inserter(parent, bb, &where);
				lower_parallel_copies(p2e, &where, copy, count);
			}
			edge++;
		}
	}
}
