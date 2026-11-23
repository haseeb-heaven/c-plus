/* F-074: deterministic allocation-fault injection across all containers.
 * Every 37th operation injects a one-shot failure into a guaranteed-allocating
 * path (map grow, list node, full queue push, vector reserve) and asserts
 * failure atomicity against the shadow model.  Fixed seed 0xF074...,
 * 2400 valid operations. */
#define F100_ID "F-074"
#define F100_SEED 0xF0745EEDULL
#define F100_OPS 2400
#define F100_MODE F100_M_FAULT
#include "f100_cont_engine.hp"
