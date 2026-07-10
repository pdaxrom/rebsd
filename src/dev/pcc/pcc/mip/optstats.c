/*	$Id$	*/

#include "pass2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct optstats_record {
	const char *function;
	unsigned long long basic_blocks;
	unsigned long long cfg_edges;
	unsigned long long temps;
	unsigned long long max_live_temps;
	unsigned long long interference_edges;
	unsigned long long coalesce_attempts;
	unsigned long long coalesce_successes;
	unsigned long long coalesce_rejected;
	unsigned long long spill_candidates;
	unsigned long long selected_spills;
	unsigned long long reloads;
	unsigned long long spill_stores;
	unsigned long long rematerialized;
	unsigned long long gpr_pressure;
	unsigned long long fpr_pressure;
	unsigned long long frame_bytes;
	unsigned long long spill_area_bytes;
	unsigned long long caller_saved_used;
	unsigned long long callee_saved_used;
	unsigned long long calls;
	unsigned long long max_loop_depth;
	unsigned long long functions;
	int active;
};

static struct optstats_record current;
static struct optstats_record total;
static int summary_registered;
static unsigned char used_colors[MAXREGS];

static void
optstats_print(const char *kind, const struct optstats_record *s)
{
	fprintf(stderr,
	    "PCC_OPTSTATS kind=%s function=%s basic_blocks=%llu cfg_edges=%llu"
	    " temps=%llu max_live_temps=%llu interference_edges=%llu"
	    " coalesce_attempts=%llu coalesce_successes=%llu"
	    " coalesce_rejected=%llu spill_candidates=%llu"
	    " selected_spills=%llu reloads=%llu spill_stores=%llu"
	    " rematerialized=%llu gpr_pressure=%llu fpr_pressure=%llu"
	    " frame_bytes=%llu spill_area_bytes=%llu"
	    " caller_saved_used=%llu callee_saved_used=%llu calls=%llu"
	    " max_loop_depth=%llu functions=%llu\n",
	    kind, s->function != NULL ? s->function : "*",
	    s->basic_blocks, s->cfg_edges, s->temps, s->max_live_temps,
	    s->interference_edges, s->coalesce_attempts,
	    s->coalesce_successes, s->coalesce_rejected,
	    s->spill_candidates, s->selected_spills, s->reloads,
	    s->spill_stores, s->rematerialized, s->gpr_pressure,
	    s->fpr_pressure, s->frame_bytes, s->spill_area_bytes,
	    s->caller_saved_used, s->callee_saved_used, s->calls,
	    s->max_loop_depth, s->functions);
}

static void
optstats_summary(void)
{
	if (total.active)
		optstats_print("summary", &total);
}

static void
count_calls(NODE *p, unsigned long long *calls)
{
	int o;

	if (p == NIL)
		return;
	o = optype(p->n_op);
	if (callop(p->n_op))
		(*calls)++;
	if (o != LTYPE)
		count_calls(p->n_left, calls);
	if (o == BITYPE)
		count_calls(p->n_right, calls);
}

#define OBITS ((unsigned)(sizeof(unsigned) * 8))
#define OWORD(n) ((unsigned)(n) / OBITS)
#define OMASK(n) ((unsigned)1U << ((unsigned)(n) % OBITS))

static unsigned
cfg_loop_depth(struct p2env *p2e)
{
	struct basicblock **blocks;
	struct basicblock *bb;
	struct cfgnode *cn, *parent_cn;
	unsigned *dom, *next, *members;
	unsigned *depth;
	int *stack;
	unsigned n, words, i, j, k, parent, changed, have_parent, top;
	unsigned maxdepth;
	int ok;

	n = (unsigned)p2e->nbblocks;
	if (n == 0)
		return 0;
	maxdepth = 0;
	ok = 0;
	words = (n + OBITS - 1) / OBITS;
	blocks = calloc(n, sizeof(*blocks));
	dom = calloc(n * words, sizeof(*dom));
	next = calloc(words, sizeof(*next));
	members = calloc(words, sizeof(*members));
	depth = calloc(n, sizeof(*depth));
	stack = calloc(n, sizeof(*stack));
	if (blocks == NULL || dom == NULL || next == NULL ||
	    members == NULL || depth == NULL || stack == NULL)
		goto out;
	ok = 1;

	DLIST_FOREACH(bb, &p2e->bblocks, bbelem)
		if (bb->bbnum >= 0 && (unsigned)bb->bbnum < n)
			blocks[bb->bbnum] = bb;
	for (i = 0; i < n; i++) {
		for (j = 0; j < words; j++)
			dom[i * words + j] = i == 0 ? 0 : ~0U;
		dom[i * words + OWORD(i)] |= OMASK(i);
	}

	do {
		changed = 0;
		for (i = 1; i < n; i++) {
			if (blocks[i] == NULL)
				continue;
			memset(next, 0, words * sizeof(*next));
			have_parent = 0;
			SLIST_FOREACH(cn, &blocks[i]->parents, cfgelem) {
				k = (unsigned)cn->bblock->bbnum;
				if (k >= n)
					continue;
				if (!have_parent) {
					memcpy(next, &dom[k * words],
					    words * sizeof(*next));
					have_parent = 1;
				} else {
					for (j = 0; j < words; j++)
						next[j] &= dom[k * words + j];
				}
			}
			next[OWORD(i)] |= OMASK(i);
			if (memcmp(next, &dom[i * words],
			    words * sizeof(*next)) != 0) {
				memcpy(&dom[i * words], next,
				    words * sizeof(*next));
				changed = 1;
			}
		}
	} while (changed);

	for (i = 0; i < n; i++) {
		if (blocks[i] == NULL)
			continue;
		SLIST_FOREACH(cn, &blocks[i]->child, chld) {
			k = (unsigned)cn->bblock->bbnum;
			if (k >= n ||
			    (dom[i * words + OWORD(k)] & OMASK(k)) == 0)
				continue;
			memset(members, 0, words * sizeof(*members));
			members[OWORD(k)] |= OMASK(k);
			members[OWORD(i)] |= OMASK(i);
			top = 0;
			if (i != k)
				stack[top++] = (int)i;
			while (top != 0) {
				j = (unsigned)stack[--top];
				SLIST_FOREACH(parent_cn, &blocks[j]->parents, cfgelem) {
					parent = (unsigned)parent_cn->bblock->bbnum;
					if (parent >= n ||
					    (members[OWORD(parent)] &
					    OMASK(parent)) != 0)
						continue;
					members[OWORD(parent)] |= OMASK(parent);
					if (parent != k)
						stack[top++] = (int)parent;
				}
			}
			for (j = 0; j < n; j++)
				if ((members[OWORD(j)] & OMASK(j)) != 0)
					depth[j]++;
		}
	}
	for (i = 0; i < n; i++)
		if (depth[i] > maxdepth)
			maxdepth = depth[i];

out:
	free(blocks);
	free(dom);
	free(next);
	free(members);
	free(depth);
	free(stack);
	return ok ? maxdepth : 0;
}

void
optstats_begin(struct p2env *p2e)
{
	if (!p2stats)
		return;
	memset(&current, 0, sizeof(current));
	memset(used_colors, 0, sizeof(used_colors));
	current.function = p2e->ipp->ipp_name;
	current.active = 1;
	if (!summary_registered) {
		if (atexit(optstats_summary) == 0)
			summary_registered = 1;
	}
}

void
optstats_capture_cfg(struct p2env *p2e)
{
	struct basicblock *bb;
	struct cfgnode *cn;
	struct interpass *ip;
	int ntemps;

	if (!current.active)
		return;
	current.basic_blocks = (unsigned)p2e->nbblocks;
	current.cfg_edges = 0;
	DLIST_FOREACH(bb, &p2e->bblocks, bbelem)
		SLIST_FOREACH(cn, &bb->child, chld)
			current.cfg_edges++;
	current.max_loop_depth = cfg_loop_depth(p2e);
	current.calls = 0;
	DLIST_FOREACH(ip, &p2e->ipole, qelem)
		if (ip->type == IP_NODE)
			count_calls(ip->ip_node, &current.calls);
	ntemps = p2e->epp->ip_tmpnum - p2e->ipp->ip_tmpnum;
	current.temps = ntemps > 0 ? (unsigned)ntemps : 0;
}

void
optstats_finish(struct p2env *p2e)
{
	int i, ntemps;

	if (!current.active)
		return;
	for (i = 0; tempregs[i] >= 0; i++)
		if (tempregs[i] < MAXREGS && used_colors[tempregs[i]])
			current.caller_saved_used++;
	for (i = 0; i < MAXREGS; i++)
		if (TESTBIT(p2e->p_regs, i))
			current.callee_saved_used++;
	ntemps = p2e->epp->ip_tmpnum - p2e->ipp->ip_tmpnum;
	if (ntemps > 0 && (unsigned)ntemps > current.temps)
		current.temps = (unsigned)ntemps;

#define ADD(field) total.field += current.field
	ADD(basic_blocks);
	ADD(cfg_edges);
	ADD(temps);
	ADD(interference_edges);
	ADD(coalesce_attempts);
	ADD(coalesce_successes);
	ADD(coalesce_rejected);
	ADD(spill_candidates);
	ADD(selected_spills);
	ADD(reloads);
	ADD(spill_stores);
	ADD(rematerialized);
	ADD(frame_bytes);
	ADD(spill_area_bytes);
	ADD(caller_saved_used);
	ADD(callee_saved_used);
	ADD(calls);
#undef ADD
#define MAXIMUM(field) \
	if (current.field > total.field) total.field = current.field
	MAXIMUM(max_live_temps);
	MAXIMUM(gpr_pressure);
	MAXIMUM(fpr_pressure);
	MAXIMUM(max_loop_depth);
#undef MAXIMUM
	total.function = "*";
	total.functions++;
	total.active++;
	current.functions = 1;
	optstats_print("function", &current);
	current.active = 0;
}

void
optstats_note_live(unsigned temps, unsigned gpr, unsigned fpr)
{
	if (!current.active)
		return;
	if (temps > current.max_live_temps)
		current.max_live_temps = temps;
	if (gpr > current.gpr_pressure)
		current.gpr_pressure = gpr;
	if (fpr > current.fpr_pressure)
		current.fpr_pressure = fpr;
}

void
optstats_note_interference_edge(void)
{
	if (current.active)
		current.interference_edges++;
}

void
optstats_note_coalesce(int success)
{
	if (!current.active)
		return;
	current.coalesce_attempts++;
	if (success)
		current.coalesce_successes++;
	else
		current.coalesce_rejected++;
}

void
optstats_note_spill_candidates(unsigned n)
{
	if (current.active)
		current.spill_candidates += n;
}

void
optstats_note_selected_spill(void)
{
	if (current.active)
		current.selected_spills++;
}

void
optstats_note_reload(void)
{
	if (current.active)
		current.reloads++;
}

void
optstats_note_spill_store(void)
{
	if (current.active)
		current.spill_stores++;
}

void
optstats_note_spill_slot(unsigned bytes)
{
	if (current.active)
		current.spill_area_bytes += bytes;
}

void
optstats_note_rematerialized(void)
{
	if (current.active)
		current.rematerialized++;
}

void
optstats_note_register_used(int reg)
{
	if (current.active && reg >= 0 && reg < MAXREGS)
		used_colors[reg] = 1;
}

void
optstats_set_frame(unsigned bytes)
{
	if (current.active)
		current.frame_bytes = bytes;
}
