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
count_multiply_candidates(NODE *p, int low, int high)
{
	int count, o;

	count = 0;
	o = optype(p->n_op);
	if (o != LTYPE)
		count += count_multiply_candidates(p->n_left, low, high);
	if (o == BITYPE)
		count += count_multiply_candidates(p->n_right, low, high);
	if (p->n_op == MUL && lvn_expression(p, low, high))
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
	if (p->n_op != MUL || !lvn_expression(p, low, high) ||
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
	if (p->n_op != MUL || !lvn_expression(p, low, high))
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
 * Materialize repeated pure integer multiplies used inside trees.  Memory
 * accesses do not invalidate SSA names, but calls and asm delimit regions.
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
