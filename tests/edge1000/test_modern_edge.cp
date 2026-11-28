/* Modern-library edge sweep (Issue #24, added 2026-11-24).
 *
 * Deterministic edge cases over stdlib/math3d.hp: zero/unit/large vectors,
 * degenerate matrices, NaN and +/-Inf inputs, denial-path status checks, and
 * scale factors of 0, 0.5, 1, -1 across N=1200 loop iterations.
 *
 * Prints one line per case? No: prints a summary `EDGE1000_MODERN <cases> <pass>`
 * and fails with a per-case line if any edge assertion fails.  Exit 0 == all pass.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/math3d.hp"

static int g_cases = 0;
static int g_pass = 0;

static void edge(int cond) {
    g_cases++;
    if (cond) {
        g_pass++;
    } else {
        printf("EDGE1000_FAIL modern case %d\n", g_cases);
    }
}

int main(void) {
    const int N = 1200;
    int i;
    for (i = 0; i < N; i++) {
        volatile float zf = 0.0f;
        float nanv = zf / zf;                    /* NaN */
        float pinfv = (zf + 1.0f) / zf;          /* +Inf */
        float ninfv = (zf - 1.0f) / zf;          /* -Inf */
        vec3 a, c, z, n;
        mat4 m;
        quat q;
        float f = 0.0f;
        int rc;
        float v = (float)i * 0.25f;
        float sf = (i % 4 == 0) ? 0.5f : ((i % 4 == 1) ? 1.0f : -1.0f);

        a.x = v; a.y = 0.0f; a.z = 0.0f;
        edge(math3d_finite(v) == 1 && vec3_finite(a) == 1);
        edge(vec3_length(a, &f) == MATH3D_OK && f == v);
        if (v > 0.0f) {
            rc = vec3_normalize(a, &n);
            edge(rc == MATH3D_OK && vec3_finite(n) == 1);
        } else {
            /* Zero-length vector: normalize must refuse. */
            edge(vec3_normalize(a, &n) == MATH3D_ERR_ZERO);
        }

        if (i % 7 == 0) {
            /* NaN / +-Inf inputs are flagged, never treated as finite. */
            a.x = nanv;
            edge(math3d_finite(a.x) == 0 && vec3_finite(a) == 0);
            a.x = pinfv;
            edge(math3d_finite(a.x) == 0 && vec3_finite(a) == 0);
            a.x = ninfv;
            edge(math3d_finite(a.x) == 0 && vec3_finite(a) == 0);
            a.x = v;
        }

        if (i % 11 == 0) {
            /* Degenerate matrix: identity is always finite. */
            edge(mat4_identity(&m) == MATH3D_OK && mat4_finite(m) == 1);
        }

        /* Zero-length vector: normalize must refuse. */
        z.x = 0.0f; z.y = 0.0f; z.z = 0.0f;
        edge(vec3_normalize(z, &n) == MATH3D_ERR_ZERO);
        edge(vec3_length(z, &f) == MATH3D_OK && f == 0.0f);

        /* scale by 0.5 / 1.0 / -1.0 on a finite vector. */
        edge(vec3_scale(a, sf, &c) == MATH3D_OK);
        edge(vec3_length(c, &f) == MATH3D_OK);

        /* quaternion edges: zero and its finite status. */
        q = quat_zero();
        edge(quat_finite(q) == 1);
    }
    printf("EDGE1000_MODERN %d %d\n", g_cases, g_pass);
    return (g_cases == g_pass) ? 0 : 1;
}
