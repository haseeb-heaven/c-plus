/* F-064: strmap with collision-heavy owned string keys.
 * Keys are selected via the public stdlib_map_hash_text helper so 64 keys
 * share bucket 0 and 64 share bucket 1 modulo 256.  Fixed seed 0xF064...,
 * 2200 valid operations, shadow model after each op. */
#define F100_ID "F-064"
#define F100_SEED 0xF0645EEDULL
#define F100_OPS 2200
#define F100_MODE F100_M_COLLIDE_STR
#include "f100_cont_engine.hp"
