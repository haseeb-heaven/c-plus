/* F-061: intmap/intset with collision-heavy integer keys.
 * Keys are chosen (via the public hash helpers) to share bucket 0/1 modulo
 * 256, so every power-of-two capacity up to 256 produces dense probe chains.
 * Fixed seed 0xF061..., 2200 valid operations, shadow model after each op. */
#define F100_ID "F-061"
#define F100_SEED 0xF0615EEDULL
#define F100_OPS 2200
#define F100_MODE F100_M_COLLIDE_INT
#include "f100_cont_engine.hp"
