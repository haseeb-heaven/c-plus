/* F-070: vector capacity growth and documented overflow refusal.
 * push_back/insert drive 4->8->... doubling; reserve/shrink_to_fit/copy_from
 * are exercised and SIZE_MAX reserve must return -1 without mutating state.
 * Fixed seed 0xF070..., 2500 valid operations. */
#define F100_ID "F-070"
#define F100_SEED 0xF0705EEDULL
#define F100_OPS 2500
#define F100_MODE F100_M_VEC_GROW
#include "f100_cont_engine.hp"
