/* F-071: vector insert/erase/pop/clear with positional shadow model.
 * Every index is bounded by the live extent; erase and pop return values are
 * compared with the shadow before the model is shifted.
 * Fixed seed 0xF071..., 2500 valid operations. */
#define F100_ID "F-071"
#define F100_SEED 0xF0715EEDULL
#define F100_OPS 2500
#define F100_MODE F100_M_VEC_ERASE
#include "f100_cont_engine.hp"
