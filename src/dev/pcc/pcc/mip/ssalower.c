/*	$Id$	*/
/*
 * Split SSA critical edges and lower phi functions to parallel copies.
 */

#include "pass2.h"

#include <string.h>

#define	mktemp(n, t)	mklnode(TEMP, 0, n, t)

#ifndef TARGET_SSA_STRENGTH_REDUCE_MUL
#define TARGET_SSA_STRENGTH_REDUCE_MUL()	0
#endif
#ifndef TARGET_SSA_CSE_CONST_SHIFT
#define TARGET_SSA_CSE_CONST_SHIFT()	0
#endif

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

struct ssa_constant {
	CONSZ value;
	TWORD type;
	int known;
	int replaceable;
};

struct lvn_value {
	NODE *expression;
	TWORD type;
	int temp;
};

struct multiply_cse_value {
	NODE *expression;
	TWORD type;
	int count;
	int temp;
};

static char propagated_copy_marker;

static int
block_dominates(struct p2env *p2e, struct basicblock *dominator,
    struct basicblock *bb)
{
	while (bb != NULL) {
		if (bb == dominator)
			return 1;
		if (bb->idom == 0 || bb->idom >= (unsigned)p2e->bbinfo.size)
			break;
		bb = p2e->bbinfo.arr[bb->idom];
	}
	return 0;
}

static int
single_successor(struct basicblock *bb, struct basicblock *successor)
{
	struct cfgnode *cn;
	int count;

	count = 0;
	SLIST_FOREACH(cn, &bb->child, chld) {
		if (cn->bblock != successor)
			return 0;
		count++;
	}
	return count == 1;
}

static struct basicblock *
temp_definition_block(struct p2env *p2e, int temp)
{
	struct basicblock *bb, *definition;
	struct interpass *ip;
	struct phiinfo *phi;
	NODE *p;

	definition = NULL;
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		SLIST_FOREACH(phi, &bb->phi, phielem)
			if (phi->newtmpregno == temp) {
				if (definition != NULL)
					return NULL;
				definition = bb;
			}
		for (ip = bb->first;; ip = DLIST_NEXT(ip, qelem)) {
			if (ip->type == IP_NODE) {
				p = ip->ip_node;
				if (p->n_op == ASSIGN &&
				    p->n_left->n_op == TEMP &&
				    regno(p->n_left) == temp) {
					if (definition != NULL)
						return NULL;
					definition = bb;
				}
			}
			if (ip == bb->last)
				break;
		}
	}
	return definition;
}

static int
temp_integer_constant(struct p2env *p2e, int temp, TWORD type, CONSZ *value)
{
	struct interpass *ip;
	NODE *p;
	int found;

	found = 0;
	DLIST_FOREACH(ip, &p2e->ipole, qelem) {
		if (ip->type != IP_NODE)
			continue;
		p = ip->ip_node;
		if (p->n_op != ASSIGN || p->n_left->n_op != TEMP ||
		    regno(p->n_left) != temp)
			continue;
		if (found || p->n_left->n_type != type ||
		    p->n_right->n_op != ICON || p->n_right->n_type != type ||
		    p->n_right->n_name == NULL ||
		    p->n_right->n_name[0] != '\0')
			return 0;
		*value = getlval(p->n_right);
		found = 1;
	}
	return found;
}

static int
phi_parent_index(struct basicblock *header, struct basicblock *parent)
{
	struct cfgnode *cn;
	int edge;

	edge = 0;
	SLIST_FOREACH(cn, &header->parents, cfgelem) {
		if (cn->bblock == parent)
			return edge;
		edge++;
	}
	return -1;
}

static int
induction_delta(struct basicblock *latch, struct phiinfo *phi,
    int back_input)
{
	struct interpass *ip;
	NODE *left, *p, *right;
	CONSZ value;

	for (ip = latch->first;; ip = DLIST_NEXT(ip, qelem)) {
		if (ip->type != IP_NODE)
			goto next;
		p = ip->ip_node;
		if (p->n_op != ASSIGN || p->n_left->n_op != TEMP ||
		    regno(p->n_left) != back_input ||
		    p->n_left->n_type != phi->n_type)
			goto next;
		right = p->n_right;
		if (right->n_type != phi->n_type ||
		    (right->n_op != PLUS && right->n_op != MINUS))
			return 0;
		left = right->n_left;
		if (left->n_op != TEMP ||
		    regno(left) != phi->newtmpregno ||
		    left->n_type != phi->n_type ||
		    right->n_right->n_op != ICON ||
		    right->n_right->n_type != phi->n_type ||
		    right->n_right->n_name == NULL ||
		    right->n_right->n_name[0] != '\0')
			return 0;
		value = getlval(right->n_right);
		if (right->n_op == MINUS)
			value = -value;
		if (value == 1)
			return 1;
		if (value == -1)
			return -1;
		return 0;
next:
		if (ip == latch->last)
			break;
	}
	return 0;
}

static int
find_induction_candidate(struct p2env *p2e, NODE *p, struct phiinfo *phi,
    struct basicblock *preheader, TWORD *type)
{
	struct basicblock *definition;
	NODE *invariant;
	int candidate, o;

	o = optype(p->n_op);
	if (o != LTYPE) {
		candidate = find_induction_candidate(p2e, p->n_left, phi,
		    preheader, type);
		if (candidate >= 0)
			return candidate;
	}
	if (o == BITYPE) {
		candidate = find_induction_candidate(p2e, p->n_right, phi,
		    preheader, type);
		if (candidate >= 0)
			return candidate;
	}
	if (p->n_op != MUL || p->n_type != phi->n_type ||
	    !ISINTEGER(BTYPE(p->n_type)) || ISPTR(p->n_type))
		return -1;
	if (p->n_left->n_op == TEMP &&
	    regno(p->n_left) == phi->newtmpregno)
		invariant = p->n_right;
	else if (p->n_right->n_op == TEMP &&
	    regno(p->n_right) == phi->newtmpregno)
		invariant = p->n_left;
	else
		return -1;
	if (invariant->n_op != TEMP || invariant->n_type != p->n_type ||
	    regno(invariant) == phi->newtmpregno)
		return -1;
	definition = temp_definition_block(p2e, regno(invariant));
	if (definition == NULL ||
	    !block_dominates(p2e, definition, preheader))
		return -1;
	*type = p->n_type;
	return regno(invariant);
}

static void
replace_induction_multiply(NODE **nodep, int induction, int invariant,
    int replacement, TWORD type)
{
	NODE *p;
	int o;

	p = *nodep;
	o = optype(p->n_op);
	if (o != LTYPE)
		replace_induction_multiply(&p->n_left, induction, invariant,
		    replacement, type);
	if (o == BITYPE)
		replace_induction_multiply(&p->n_right, induction, invariant,
		    replacement, type);
	p = *nodep;
	if (p->n_op != MUL || p->n_type != type)
		return;
	if (!((p->n_left->n_op == TEMP &&
	    regno(p->n_left) == induction &&
	    p->n_right->n_op == TEMP &&
	    regno(p->n_right) == invariant) ||
	    (p->n_right->n_op == TEMP &&
	    regno(p->n_right) == induction &&
	    p->n_left->n_op == TEMP &&
	    regno(p->n_left) == invariant)))
		return;
	tfree(p);
	*nodep = mktemp(replacement, type);
}

static void
insert_edge_value(struct basicblock *bb, struct interpass *ip)
{
	if (bb->last->type == IP_NODE &&
	    bb->last->ip_node->n_op == GOTO) {
		DLIST_INSERT_BEFORE(bb->last, ip, qelem);
		if (bb->first == bb->last)
			bb->first = ip;
	} else {
		DLIST_INSERT_AFTER(bb->last, ip, qelem);
		bb->last = ip;
	}
}

static void
reduce_induction_candidate(struct p2env *p2e, struct basicblock *header,
    struct basicblock *preheader, struct basicblock *latch,
    struct phiinfo *induction, int preedge, int backedge, int delta,
    int invariant, TWORD type)
{
	struct basicblock *bb;
	struct interpass *ip;
	struct phiinfo *scaled_phi;
	NODE *initial_expression, *next_expression;
	CONSZ initial_value;
	int initial, scaled, scaled_initial, scaled_next;

	initial = induction->intmpregno[preedge];
	scaled_initial = p2e->epp->ip_tmpnum++;
	scaled = p2e->epp->ip_tmpnum++;
	scaled_next = p2e->epp->ip_tmpnum++;

	if (temp_integer_constant(p2e, initial, type, &initial_value) &&
	    initial_value == 0)
		initial_expression = mklnode(ICON, 0, 0, type);
	else
		initial_expression = mkbinode(MUL, mktemp(initial, type),
		    mktemp(invariant, type), type);
	ip = ipnode(mkbinode(ASSIGN, mktemp(scaled_initial, type),
	    initial_expression, type));
	insert_edge_value(preheader, ip);

	next_expression = mkbinode(delta > 0 ? PLUS : MINUS,
	    mktemp(scaled, type), mktemp(invariant, type), type);
	ip = ipnode(mkbinode(ASSIGN, mktemp(scaled_next, type),
	    next_expression, type));
	insert_edge_value(latch, ip);

	scaled_phi = tmpcalloc(sizeof(*scaled_phi));
	scaled_phi->tmpregno = scaled;
	scaled_phi->newtmpregno = scaled;
	scaled_phi->n_type = type;
	scaled_phi->size = induction->size;
	scaled_phi->intmpregno = tmpcalloc((size_t)scaled_phi->size *
	    sizeof(*scaled_phi->intmpregno));
	scaled_phi->intmpregno[preedge] = scaled_initial;
	scaled_phi->intmpregno[backedge] = scaled_next;
	SLIST_INSERT_LAST(&header->phi, scaled_phi, phielem);

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		if (!block_dominates(p2e, header, bb))
			continue;
		for (ip = bb->first;; ip = DLIST_NEXT(ip, qelem)) {
			if (ip->type == IP_NODE)
				replace_induction_multiply(&ip->ip_node,
				    induction->newtmpregno, invariant, scaled,
				    type);
			if (ip == bb->last)
				break;
		}
	}
}

/*
 * Replace i * stride in a canonical natural loop with a scaled induction
 * value.  Keep the first version deliberately narrow: one entry, one latch,
 * an exact +/-1 update, and an SSA stride available before the loop.
 */
static void
ssa_strength_reduce_induction(struct p2env *p2e)
{
	struct basicblock *bb, *definition, *latch, *preheader;
	struct cfgnode *cn;
	struct interpass *ip;
	struct phiinfo *nextphi, *phi;
	TWORD type;
	int backedge, delta, invariant, preedge;

	if (!TARGET_SSA_STRENGTH_REDUCE_MUL())
		return;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		preheader = NULL;
		latch = NULL;
		SLIST_FOREACH(cn, &bb->parents, cfgelem) {
			if (block_dominates(p2e, bb, cn->bblock)) {
				if (latch != NULL)
					latch = bb;
				else
					latch = cn->bblock;
			} else {
				if (preheader != NULL)
					preheader = bb;
				else
					preheader = cn->bblock;
			}
		}
		if (preheader == NULL || latch == NULL || preheader == bb ||
		    latch == bb || !single_successor(preheader, bb) ||
		    !single_successor(latch, bb))
			continue;
		preedge = phi_parent_index(bb, preheader);
		backedge = phi_parent_index(bb, latch);
		if (preedge < 0 || backedge < 0)
			continue;

		for (phi = SLIST_FIRST(&bb->phi); phi != NULL; phi = nextphi) {
			nextphi = phi->phielem.q_forw;
			if (phi->size != 2 || preedge >= phi->size ||
			    backedge >= phi->size ||
			    !ISINTEGER(BTYPE(phi->n_type)) || ISPTR(phi->n_type))
				continue;
			delta = induction_delta(latch, phi,
			    phi->intmpregno[backedge]);
			if (delta == 0)
				continue;
			invariant = -1;
			DLIST_FOREACH(definition, &p2e->bblocks, bbelem) {
				if (invariant >= 0 ||
				    !block_dominates(p2e, bb, definition))
					continue;
				for (ip = definition->first;;
				    ip = DLIST_NEXT(ip, qelem)) {
					if (ip->type == IP_NODE && invariant < 0)
						invariant = find_induction_candidate(
						    p2e, ip->ip_node, phi,
						    preheader, &type);
					if (ip == definition->last)
						break;
				}
			}
			if (invariant >= 0)
				reduce_induction_candidate(p2e, bb, preheader,
				    latch, phi, preedge, backedge, delta,
				    invariant, type);
		}
	}
}

static int
lvn_operation(int op)
{
	switch (op) {
	case PLUS:
	case MINUS:
	case MUL:
	case AND:
	case OR:
	case ER:
	case LS:
	case RS:
	case UMINUS:
	case COMPL:
		return 1;
	default:
		return 0;
	}
}

static int
lvn_expression(NODE *p, int low, int high)
{
	int o;

	if (p->n_qual != 0 || p->n_ap != NULL ||
	    !ISINTEGER(BTYPE(p->n_type)) || ISPTR(p->n_type))
		return 0;
	if (p->n_op == TEMP)
		return regno(p) >= low && regno(p) < high;
	if (p->n_op == ICON)
		return p->n_name != NULL && p->n_name[0] == '\0';
	if (!lvn_operation(p->n_op))
		return 0;
	o = optype(p->n_op);
	if (!lvn_expression(p->n_left, low, high))
		return 0;
	return o != BITYPE || lvn_expression(p->n_right, low, high);
}

static int
lvn_same_expression(NODE *left, NODE *right)
{
	int o;

	if (left->n_op != right->n_op || left->n_type != right->n_type ||
	    left->n_qual != right->n_qual)
		return 0;
	if (left->n_op == TEMP)
		return regno(left) == regno(right);
	if (left->n_op == ICON)
		return getlval(left) == getlval(right);
	o = optype(left->n_op);
	if (!lvn_same_expression(left->n_left, right->n_left))
		return 0;
	return o != BITYPE ||
	    lvn_same_expression(left->n_right, right->n_right);
}

static int
lvn_barrier(NODE *p)
{
	int o;

	if (callop(p->n_op) || p->n_op == XASM || p->n_op == NAME ||
	    p->n_op == OREG || p->n_op == UMUL || p->n_op == STASG ||
	    p->n_op == STARG || p->n_op == STCLR)
		return 1;
	o = optype(p->n_op);
	if (asgop(p->n_op) &&
	    (p->n_op != ASSIGN || o != BITYPE ||
	    p->n_left->n_op != TEMP))
		return 1;
	if (o != LTYPE && lvn_barrier(p->n_left))
		return 1;
	return o == BITYPE && lvn_barrier(p->n_right);
}

static int
multiply_cse_barrier(NODE *p)
{
	int o;

	if (callop(p->n_op) || p->n_op == XASM)
		return 1;
	o = optype(p->n_op);
	if (o != LTYPE && multiply_cse_barrier(p->n_left))
		return 1;
	return o == BITYPE && multiply_cse_barrier(p->n_right);
}

static int
tree_defines_temp(NODE *p, int temp)
{
	int o;

	o = optype(p->n_op);
	if (asgop(p->n_op) && o == BITYPE && p->n_left->n_op == TEMP &&
	    regno(p->n_left) == temp)
		return 1;
	if (o != LTYPE && tree_defines_temp(p->n_left, temp))
		return 1;
	return o == BITYPE && tree_defines_temp(p->n_right, temp);
}

static int
multiply_operand_defined_in_tree(NODE *p, NODE *tree)
{
	int o;

	if (p->n_op == TEMP)
		return tree_defines_temp(tree, regno(p));
	o = optype(p->n_op);
	if (o != LTYPE && multiply_operand_defined_in_tree(p->n_left, tree))
		return 1;
	return o == BITYPE &&
	    multiply_operand_defined_in_tree(p->n_right, tree);
}

static int
multiply_cse_candidate(NODE *p, int low, int high)
{
	if (!lvn_expression(p, low, high))
		return 0;
	if (p->n_op == MUL)
		return 1;
	return TARGET_SSA_CSE_CONST_SHIFT() && p->n_op == LS &&
	    p->n_right->n_op == ICON && getlval(p->n_right) > 0 &&
	    getlval(p->n_right) < SZINT;
}

static int
count_multiply_candidates(NODE *p, int low, int high)
{
	int count, o;

	count = 0;
	o = optype(p->n_op);
	if (o != LTYPE)
		count += count_multiply_candidates(p->n_left, low, high);
	if (o == BITYPE)
		count += count_multiply_candidates(p->n_right, low, high);
	if (multiply_cse_candidate(p, low, high))
		count++;
	return count;
}

static void
collect_multiply_candidates(NODE *p, NODE *tree,
    struct multiply_cse_value *value, int *nvalue, int low, int high)
{
	int i, o;

	o = optype(p->n_op);
	if (o != LTYPE)
		collect_multiply_candidates(p->n_left, tree, value, nvalue,
		    low, high);
	if (o == BITYPE)
		collect_multiply_candidates(p->n_right, tree, value, nvalue,
		    low, high);
	if (!multiply_cse_candidate(p, low, high) ||
	    multiply_operand_defined_in_tree(p, tree))
		return;
	for (i = 0; i < *nvalue; i++)
		if (value[i].type == p->n_type &&
		    lvn_same_expression(value[i].expression, p))
			break;
	if (i != *nvalue) {
		value[i].count++;
		return;
	}
	value[i].expression = tcopy(p);
	value[i].type = p->n_type;
	value[i].count = 1;
	value[i].temp = -1;
	(*nvalue)++;
}

static void
replace_multiply_candidates(struct p2env *p2e, NODE **nodep,
    struct multiply_cse_value *value, int nvalue, struct interpass *before,
    struct basicblock *bb, int low, int high)
{
	struct interpass *ip;
	NODE *p;
	int i, o, temp;

	p = *nodep;
	o = optype(p->n_op);
	if (o != LTYPE)
		replace_multiply_candidates(p2e, &p->n_left, value, nvalue,
		    before, bb, low, high);
	if (o == BITYPE)
		replace_multiply_candidates(p2e, &p->n_right, value, nvalue,
		    before, bb, low, high);
	p = *nodep;
	if (!multiply_cse_candidate(p, low, high))
		return;
	for (i = 0; i < nvalue; i++)
		if (value[i].count > 1 && value[i].type == p->n_type &&
		    lvn_same_expression(value[i].expression, p))
			break;
	if (i == nvalue)
		return;
	if (value[i].temp < 0) {
		temp = p2e->epp->ip_tmpnum++;
		ip = ipnode(mkbinode(ASSIGN, mktemp(temp, p->n_type),
		    tcopy(p), p->n_type));
		DLIST_INSERT_BEFORE(before, ip, qelem);
		if (bb->first == before)
			bb->first = ip;
		value[i].temp = temp;
	}
	tfree(p);
	*nodep = mktemp(value[i].temp, value[i].type);
}

static void
multiply_cse_region(struct p2env *p2e, struct basicblock *bb,
    struct interpass *first, struct interpass *last,
    struct multiply_cse_value *value, int low, int high)
{
	struct interpass *ip;
	int i, nvalue;

	nvalue = 0;
	for (ip = first;; ip = DLIST_NEXT(ip, qelem)) {
		if (ip->type == IP_NODE)
			collect_multiply_candidates(ip->ip_node, ip->ip_node,
			    value, &nvalue, low, high);
		if (ip == last)
			break;
	}
	for (ip = first;; ip = DLIST_NEXT(ip, qelem)) {
		if (ip->type == IP_NODE)
			replace_multiply_candidates(p2e, &ip->ip_node, value,
			    nvalue, ip, bb, low, high);
		if (ip == last)
			break;
	}
	for (i = 0; i < nvalue; i++)
		tfree(value[i].expression);
}

/*
 * Materialize selected repeated pure integer scales used inside trees.
 * Memory accesses do not invalidate SSA names, but calls and asm delimit
 * regions.
 */
static void
ssa_local_multiply_cse(struct p2env *p2e)
{
	struct basicblock *bb;
	struct interpass *first, *ip, *previous;
	struct multiply_cse_value *value;
	int high, low, maximum;

	low = p2e->ipp->ip_tmpnum;
	high = p2e->epp->ip_tmpnum;
	maximum = 0;
	DLIST_FOREACH(ip, &p2e->ipole, qelem)
		if (ip->type == IP_NODE)
			maximum += count_multiply_candidates(ip->ip_node,
			    low, high);
	if (maximum == 0)
		return;
	value = tmpalloc((size_t)maximum * sizeof(*value));

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		first = NULL;
		previous = NULL;
		for (ip = bb->first;; ip = DLIST_NEXT(ip, qelem)) {
			if (ip->type == IP_ASM ||
			    (ip->type == IP_NODE &&
			    multiply_cse_barrier(ip->ip_node))) {
				if (first != NULL)
					multiply_cse_region(p2e, bb, first,
					    previous, value, low, high);
				first = NULL;
			} else if (first == NULL) {
				first = ip;
			}
			previous = ip;
			if (ip == bb->last) {
				if (first != NULL)
					multiply_cse_region(p2e, bb, first, ip,
					    value, low, high);
				break;
			}
		}
	}
}

/*
 * Number exact, side-effect-free integer expressions within one SSA block.
 * Calls, asm, and memory references end the local value-numbering region.
 */
void
ssa_local_value_numbering(struct p2env *p2e)
{
	struct basicblock *bb;
	struct interpass *ip;
	struct lvn_value *value;
	NODE *p;
	int high, i, low, nnode, nvalue, temp;

	ssa_strength_reduce_induction(p2e);
	ssa_local_multiply_cse(p2e);

	nnode = 0;
	DLIST_FOREACH(ip, &p2e->ipole, qelem)
		if (ip->type == IP_NODE)
			nnode++;
	if (nnode == 0)
		return;
	value = tmpalloc((size_t)nnode * sizeof(*value));
	low = p2e->ipp->ip_tmpnum;
	high = p2e->epp->ip_tmpnum;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		nvalue = 0;
		for (ip = bb->first;; ip = DLIST_NEXT(ip, qelem)) {
			if (ip->type == IP_ASM) {
				nvalue = 0;
			} else if (ip->type == IP_NODE) {
				p = ip->ip_node;
				if (lvn_barrier(p))
					nvalue = 0;
				if (p->n_op != ASSIGN ||
				    p->n_left->n_op != TEMP ||
				    p->n_left->n_type != p->n_right->n_type ||
				    !lvn_operation(p->n_right->n_op) ||
				    !lvn_expression(p->n_right, low, high))
					goto next;
				temp = regno(p->n_left);
				if (temp < low || temp >= high)
					goto next;
				for (i = 0; i < nvalue; i++)
					if (value[i].type == p->n_left->n_type &&
					    lvn_same_expression(value[i].expression,
					    p->n_right))
						break;
				if (i != nvalue) {
					tfree(p->n_right);
					p->n_right = mktemp(value[i].temp,
					    value[i].type);
				} else {
					value[nvalue].expression = p->n_right;
					value[nvalue].type = p->n_left->n_type;
					value[nvalue].temp = temp;
					nvalue++;
				}
			}
next:
			if (ip == bb->last)
				break;
		}
	}
}

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

static void
replace_temp_uses(NODE *p, int oldtemp, int newtemp)
{
	int o;

	if (p->n_op == TEMP) {
		if (regno(p) == oldtemp)
			regno(p) = newtemp;
		return;
	}
	o = optype(p->n_op);
	if (asgop(p->n_op) && o == BITYPE && p->n_left->n_op == TEMP) {
		if (regno(p->n_left) == oldtemp)
			comperr("trivial SSA phi TEMP %d has a tree definition",
			    oldtemp);
		replace_temp_uses(p->n_right, oldtemp, newtemp);
		return;
	}
	if (o != LTYPE)
		replace_temp_uses(p->n_left, oldtemp, newtemp);
	if (o == BITYPE)
		replace_temp_uses(p->n_right, oldtemp, newtemp);
}

static void
replace_phi_inputs(struct p2env *p2e, int oldtemp, int newtemp)
{
	struct basicblock *bb;
	struct phiinfo *phi;
	int i;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem)
		SLIST_FOREACH(phi, &bb->phi, phielem)
			for (i = 0; i < phi->size; i++)
				if (phi->intmpregno[i] == oldtemp)
					phi->intmpregno[i] = newtemp;
}

static void
count_temp_definitions(NODE *p, int low, int high, int *definitions)
{
	int o, temp;

	o = optype(p->n_op);
	if (asgop(p->n_op) && o == BITYPE && p->n_left->n_op == TEMP) {
		temp = regno(p->n_left);
		if (temp >= low && temp < high)
			definitions[temp - low]++;
		count_temp_definitions(p->n_right, low, high, definitions);
		return;
	}
	if (o != LTYPE)
		count_temp_definitions(p->n_left, low, high, definitions);
	if (o == BITYPE)
		count_temp_definitions(p->n_right, low, high, definitions);
}

/*
 * SSA TEMP copies are immutable aliases.  Replace their uses before phi
 * lowering so the copies cannot inflate live ranges or create redundant phi
 * inputs.  The unique-definition check keeps this pass valid if a frontend
 * leaves an unexpected non-SSA TEMP in the stream.
 */
void
ssa_propagate_temp_copies(struct p2env *p2e)
{
	struct interpass *copyip, *ip;
	NODE *p;
	int *definitions;
	int changed, destination, high, low, source;

	low = p2e->ipp->ip_tmpnum;
	high = p2e->epp->ip_tmpnum;
	if (high <= low)
		return;
	definitions = tmpcalloc((size_t)(high - low) * sizeof(*definitions));
	do {
		memset(definitions, 0,
		    (size_t)(high - low) * sizeof(*definitions));
		DLIST_FOREACH(ip, &p2e->ipole, qelem)
			if (ip->type == IP_NODE)
				count_temp_definitions(ip->ip_node, low, high,
				    definitions);

		changed = 0;
		DLIST_FOREACH(copyip, &p2e->ipole, qelem) {
			if (copyip->type != IP_NODE)
				continue;
			p = copyip->ip_node;
			if (p->n_op != ASSIGN || p->n_left->n_op != TEMP ||
			    p->n_right->n_op != TEMP ||
			    p->n_left->n_type != p->n_right->n_type)
				continue;
			destination = regno(p->n_left);
			source = regno(p->n_right);
			if (destination == source || destination < low ||
			    destination >= high ||
			    definitions[destination - low] != 1)
				continue;

			DLIST_FOREACH(ip, &p2e->ipole, qelem)
				if (ip != copyip && ip->type == IP_NODE)
					replace_temp_uses(ip->ip_node,
					    destination, source);
			replace_phi_inputs(p2e, destination, source);
			tfree(p);
			copyip->type = IP_ASM;
			copyip->ip_asm = &propagated_copy_marker;
			changed = 1;
		}
	} while (changed);
}

/*
 * A phi whose non-self inputs all name one TEMP is a copy, not a merge.
 * Replace its SSA result before edge copies are materialized.  Repetition is
 * needed because removing one trivial phi can make another one trivial.
 */
void
ssa_simplify_trivial_phi(struct p2env *p2e)
{
	struct basicblock *bb;
	struct interpass *ip;
	struct phiinfo *phi;
	int changed, i, oldtemp, source, trivial;

	do {
		changed = 0;
		DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
			SLIST_FOREACH(phi, &bb->phi, phielem) {
				oldtemp = phi->newtmpregno;
				source = 0;
				trivial = 1;
				for (i = 0; i < phi->size; i++) {
					if (phi->intmpregno[i] <= 0) {
						trivial = 0;
						break;
					}
					if (phi->intmpregno[i] == oldtemp)
						continue;
					if (source == 0) {
						source = phi->intmpregno[i];
					} else if (source != phi->intmpregno[i]) {
						trivial = 0;
						break;
					}
				}
				if (!trivial || source <= 0 || source == oldtemp)
					continue;

				DLIST_FOREACH(ip, &p2e->ipole, qelem)
					if (ip->type == IP_NODE)
						replace_temp_uses(ip->ip_node,
							    oldtemp, source);
				replace_phi_inputs(p2e, oldtemp, source);
				phi->newtmpregno = source;
				changed = 1;
			}
		}
	} while (changed);
}

static int
constant_temp(struct ssa_constant *constant, int low, int high, int temp)
{
	return temp >= low && temp < high && constant[temp - low].known;
}

static void
replace_integer_constant_uses(NODE *p, struct ssa_constant *constant,
    int low, int high)
{
	struct ssa_constant *value;
	int o, temp;

	if (p->n_op == TEMP) {
		temp = regno(p);
		if (!constant_temp(constant, low, high, temp))
			return;
		value = &constant[temp - low];
		if (!value->replaceable || p->n_type != value->type)
			return;
		p->n_op = ICON;
		setlval(p, value->value);
		p->n_name = "";
		regno(p) = 0;
		return;
	}
	o = optype(p->n_op);
	if (asgop(p->n_op) && o == BITYPE && p->n_left->n_op == TEMP) {
		replace_integer_constant_uses(p->n_right, constant, low, high);
		return;
	}
	if (o != LTYPE)
		replace_integer_constant_uses(p->n_left, constant, low, high);
	if (o == BITYPE)
		replace_integer_constant_uses(p->n_right, constant, low, high);
}

static void
validate_integer_constant_uses(NODE *p, struct ssa_constant *constant,
    int low, int high)
{
	int o, temp;

	if (p->n_op == TEMP) {
		temp = regno(p);
		if (constant_temp(constant, low, high, temp) &&
		    p->n_type != constant[temp - low].type)
			constant[temp - low].replaceable = 0;
		return;
	}
	o = optype(p->n_op);
	if (o != LTYPE)
		validate_integer_constant_uses(p->n_left, constant, low, high);
	if (o == BITYPE)
		validate_integer_constant_uses(p->n_right, constant, low, high);
}

static void
invalidate_xasm_constants(NODE *p, struct ssa_constant *constant,
    int low, int high)
{
	int o, temp;

	if (p->n_op == TEMP) {
		temp = regno(p);
		if (constant_temp(constant, low, high, temp))
			constant[temp - low].replaceable = 0;
		return;
	}
	o = optype(p->n_op);
	if (o != LTYPE)
		invalidate_xasm_constants(p->n_left, constant, low, high);
	if (o == BITYPE)
		invalidate_xasm_constants(p->n_right, constant, low, high);
}

static int
other_phi_uses(struct p2env *p2e, struct phiinfo *owner, int temp)
{
	struct basicblock *bb;
	struct phiinfo *phi;
	int i;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		SLIST_FOREACH(phi, &bb->phi, phielem) {
			if (phi == owner)
				continue;
			for (i = 0; i < phi->size; i++)
				if (phi->intmpregno[i] == temp)
					return 1;
		}
	}
	return 0;
}

/*
 * Propagate direct integer constants through SSA names and phis.  This is the
 * non-conditional part of SCCP: a phi is constant only when every defined
 * non-self input has the same known value.  Expression and branch folding are
 * deliberately separate passes.
 */
void
ssa_propagate_integer_constants(struct p2env *p2e)
{
	struct basicblock *bb;
	struct interpass *ip;
	struct phiinfo *phi;
	struct ssa_constant *constant, *input, *output;
	NODE *p;
	int changed, have_value, high, i, low, temp;

	low = p2e->ipp->ip_tmpnum;
	high = p2e->epp->ip_tmpnum;
	if (high <= low)
		return;
	constant = tmpcalloc((size_t)(high - low) * sizeof(*constant));

	DLIST_FOREACH(ip, &p2e->ipole, qelem) {
		if (ip->type != IP_NODE)
			continue;
		p = ip->ip_node;
		if (p->n_op != ASSIGN || p->n_left->n_op != TEMP ||
		    p->n_right->n_op != ICON ||
		    p->n_left->n_type != p->n_right->n_type ||
		    !ISINTEGER(BTYPE(p->n_left->n_type)) ||
		    p->n_right->n_name == NULL ||
		    p->n_right->n_name[0] != '\0')
			continue;
		temp = regno(p->n_left);
		if (temp < low || temp >= high)
			continue;
		output = &constant[temp - low];
		output->known = 1;
		output->replaceable = 1;
		output->value = getlval(p->n_right);
		output->type = p->n_left->n_type;
	}

	do {
		changed = 0;
		DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
			SLIST_FOREACH(phi, &bb->phi, phielem) {
				temp = phi->newtmpregno;
				if (temp <= 0 || temp < low || temp >= high)
					continue;
				output = &constant[temp - low];
				if (output->known ||
				    !ISINTEGER(BTYPE(phi->n_type)))
					continue;
				have_value = 0;
				for (i = 0; i < phi->size; i++) {
					if (phi->intmpregno[i] <= 0) {
						have_value = -1;
						break;
					}
					if (phi->intmpregno[i] == temp)
						continue;
					if (!constant_temp(constant, low, high,
					    phi->intmpregno[i])) {
						have_value = -1;
						break;
					}
					input = &constant[
					    phi->intmpregno[i] - low];
					if (input->type != phi->n_type ||
					    (have_value &&
					    input->value != output->value)) {
						have_value = -1;
						break;
					}
					if (!have_value) {
						output->value = input->value;
						output->type = input->type;
						have_value = 1;
					}
				}
				if (have_value == 1) {
					output->known = 1;
					output->replaceable = 1;
					changed = 1;
				}
			}
		}
	} while (changed);

	DLIST_FOREACH(ip, &p2e->ipole, qelem)
		if (ip->type == IP_NODE) {
			validate_integer_constant_uses(ip->ip_node,
			    constant, low, high);
			if (ip->ip_node->n_op == XASM)
				invalidate_xasm_constants(ip->ip_node,
				    constant, low, high);
		}

	DLIST_FOREACH(ip, &p2e->ipole, qelem)
		if (ip->type == IP_NODE)
			replace_integer_constant_uses(ip->ip_node,
			    constant, low, high);

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem)
		SLIST_FOREACH(phi, &bb->phi, phielem)
			if (constant_temp(constant, low, high,
			    phi->newtmpregno) &&
			    constant[phi->newtmpregno - low].replaceable &&
			    !other_phi_uses(p2e, phi, phi->newtmpregno))
				phi->newtmpregno = 0;
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
	struct interpass *ip, *next;
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
				if (phi->newtmpregno <= 0)
					continue;
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

	/* Copy propagation leaves empty placeholders until CFG users are done. */
	for (ip = DLIST_NEXT(&p2e->ipole, qelem);
	    ip != &p2e->ipole; ip = next) {
		next = DLIST_NEXT(ip, qelem);
		if (ip->type == IP_ASM &&
		    ip->ip_asm == &propagated_copy_marker)
			DLIST_REMOVE(ip, qelem);
	}
}

static int
constant_comparison(NODE *p, int *result)
{
	NODE *left, *right;
	CONSZ l, r;
	U_CONSZ ul, ur;

	if (!logop(p->n_op))
		return 0;
	left = p->n_left;
	right = p->n_right;
	if (left->n_op != ICON || right->n_op != ICON ||
	    left->n_name == NULL || right->n_name == NULL ||
	    left->n_name[0] != '\0' || right->n_name[0] != '\0' ||
	    !ISINTEGER(BTYPE(left->n_type)) ||
	    !ISINTEGER(BTYPE(right->n_type)))
		return 0;
	l = getlval(left);
	r = getlval(right);
	ul = (U_CONSZ)l;
	ur = (U_CONSZ)r;
	switch (p->n_op) {
	case EQ: *result = l == r; break;
	case NE: *result = l != r; break;
	case LE: *result = l <= r; break;
	case LT: *result = l < r; break;
	case GE: *result = l >= r; break;
	case GT: *result = l > r; break;
	case ULE: *result = ul <= ur; break;
	case ULT: *result = ul < ur; break;
	case UGE: *result = ul >= ur; break;
	case UGT: *result = ul > ur; break;
	default:
		return 0;
	}
	return 1;
}

/*
 * Fold only comparisons made constant by SSA propagation.  This runs after
 * the normal post-SSA jump cleanup, leaving unreachable blocks for the next
 * explicit optimization stage.
 */
int
ssa_fold_constant_branches(struct p2env *p2e)
{
	struct interpass *ip, *next;
	NODE *p;
	int changed, label, result;

	changed = 0;
	for (ip = DLIST_NEXT(&p2e->ipole, qelem);
	    ip != &p2e->ipole; ip = next) {
		next = DLIST_NEXT(ip, qelem);
		if (ip->type != IP_NODE || ip->ip_node->n_op != CBRANCH)
			continue;
		p = ip->ip_node;
		if (!constant_comparison(p->n_left, &result))
			continue;
		label = (int)getlval(p->n_right);
		tfree(p);
		if (result) {
			ip->ip_node = mkunode(GOTO,
			    mklnode(ICON, label, 0, INT), 0, INT);
		} else {
			DLIST_REMOVE(ip, qelem);
		}
		changed = 1;
	}
	return changed;
}

static void
reachability_root(struct basicblock *bb, unsigned char *reachable,
    struct basicblock **worklist, int *tail, int count)
{
	if (bb->bbnum < 0 || bb->bbnum >= count)
		comperr("SSA reachability block number %d outside 0-%d",
		    bb->bbnum, count - 1);
	if (reachable[bb->bbnum])
		return;
	reachable[bb->bbnum] = 1;
	worklist[(*tail)++] = bb;
}

static int
computed_goto_label(struct p2env *p2e, int label)
{
	int *lp;

	for (lp = p2e->epp->ip_labels; *lp; lp++)
		if (*lp == label)
			return 1;
	return 0;
}

/*
 * Remove blocks disconnected by SSA branch folding.  Computed-goto labels
 * remain alternate entry points even when the GOTO itself is unreachable;
 * DEFNAM and the epilogue have similar structural significance to pass2.
 */
int
ssa_remove_unreachable_blocks(struct p2env *p2e)
{
	struct basicblock *bb, *nextbb;
	struct basicblock **worklist;
	struct cfgnode *cn;
	struct interpass *ip, *nextip;
	unsigned char *reachable;
	int changed, head, tail;

	if (p2e->nbblocks == 0)
		return 0;
	reachable = tmpcalloc((size_t)p2e->nbblocks * sizeof(*reachable));
	worklist = tmpalloc((size_t)p2e->nbblocks * sizeof(*worklist));
	head = tail = 0;

	bb = DLIST_NEXT(&p2e->bblocks, bbelem);
	reachability_root(bb, reachable, worklist, &tail, p2e->nbblocks);
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem) {
		if (bb->first->type == IP_EPILOG ||
		    bb->first->type == IP_DEFNAM ||
		    (bb->first->type == IP_DEFLAB &&
		    computed_goto_label(p2e, bb->first->ip_lbl)))
			reachability_root(bb, reachable, worklist, &tail,
			    p2e->nbblocks);
	}
	while (head < tail) {
		bb = worklist[head++];
		SLIST_FOREACH(cn, &bb->child, chld)
			reachability_root(cn->bblock, reachable, worklist,
			    &tail, p2e->nbblocks);
	}

	changed = 0;
	for (bb = DLIST_NEXT(&p2e->bblocks, bbelem);
	    bb != &p2e->bblocks; bb = nextbb) {
		nextbb = DLIST_NEXT(bb, bbelem);
		if (reachable[bb->bbnum])
			continue;
		ip = bb->first;
		for (;;) {
			nextip = DLIST_NEXT(ip, qelem);
			if (ip->type == IP_NODE)
				tfree(ip->ip_node);
			DLIST_REMOVE(ip, qelem);
			if (ip == bb->last)
				break;
			ip = nextip;
		}
		DLIST_REMOVE(bb, bbelem);
		changed = 1;
	}
	return changed;
}
