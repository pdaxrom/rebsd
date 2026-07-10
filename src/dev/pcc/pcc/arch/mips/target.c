/*	$Id$	*/

#include "manifest.h"

/* VR4300 values come from NEC U10504EJ7V0UM00 tables 3-12 and 7-14. */
static const struct mips_costs generic_mips3_costs =
    MIPS_COSTS_GENERIC_MIPS3;
static const struct mips_costs vr4300_costs = MIPS_COSTS_VR4300;
static const struct mips_costs generic_mips32r2_costs =
    MIPS_COSTS_GENERIC_MIPS32R2;

static void
mips_target_refresh(struct mips_target *target)
{
	switch (target->isa) {
	case MIPS_ISA_III:
		target->capabilities = MIPS_CAPS_MIPS3;
		break;
	case MIPS_ISA_MIPS32R2:
		target->capabilities = MIPS_CAPS_MIPS32R2;
		break;
	default:
		target->capabilities = MIPS_CAPS_BASE;
		break;
	}

	switch (target->tune) {
	case MIPS_TUNE_VR4300:
		target->costs = vr4300_costs;
		if (target->isa == MIPS_ISA_III)
			target->capabilities |= MIPS_CAP_INT_LOAD_INTERLOCK |
			    MIPS_CAP_FP_LOAD_INTERLOCK |
			    MIPS_CAP_VR4300_FMUL_ERRATUM;
		break;
	case MIPS_TUNE_R4000:
		target->costs = generic_mips3_costs;
		break;
	case MIPS_TUNE_24KC:
	case MIPS_TUNE_34KC:
	case MIPS_TUNE_74KC:
	case MIPS_TUNE_JZ4780:
		target->costs = generic_mips32r2_costs;
		break;
	case MIPS_TUNE_GENERIC:
	default:
		target->costs = target->isa == MIPS_ISA_MIPS32R2 ?
		    generic_mips32r2_costs : generic_mips3_costs;
		break;
	}
}

void
mips_target_set_isa(struct mips_target *target, enum mips_isa isa)
{
	target->isa = isa;
	mips_target_refresh(target);
}

void
mips_target_set_tune(struct mips_target *target, enum mips_tune tune)
{
	target->tune = tune;
	mips_target_refresh(target);
}

const char *
mips_target_error(const struct mips_target *target)
{
	switch (target->tune) {
	case MIPS_TUNE_VR4300:
	case MIPS_TUNE_R4000:
		if (target->isa != MIPS_ISA_III)
			return "selected MIPS III tuning requires -march=mips3";
		break;
	case MIPS_TUNE_24KC:
	case MIPS_TUNE_34KC:
	case MIPS_TUNE_74KC:
	case MIPS_TUNE_JZ4780:
		if (target->isa != MIPS_ISA_MIPS32R2)
			return "selected MIPS32 tuning requires -march=mips32r2";
		break;
	case MIPS_TUNE_GENERIC:
		break;
	}
	return 0;
}
