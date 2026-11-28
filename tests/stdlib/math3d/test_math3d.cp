/* math3d.hp self-checking suite.
 *
 * Each check prints "<tag> <1|0>" (1 = passed).  The final line is
 * "MATH3D <checks> <failures>".  Exit code 0 == no failures.
 *
 * Covers: identity, rotation matrix correctness, projection, quaternion
 * round-trip, NaN/Inf refusal and division-by-zero guards, plus the vec3
 * and mat4 building blocks.  All loops are bounded; no dynamic memory.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/math3d.hp"

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *tag, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(tag, cond ? 1 : 0);
}

static int near(float a, float b, float eps) {
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d <= eps;
}

static float quiet_nan(void) {
    return 0.0f / 0.0f;
}

static float pos_inf(void) {
    return 1.0f / 0.0f;
}

int main(void) {
    vec3 a, b, c, z, axis, n;
    mat4 m, r, p, v, qm, rm;
    quat q, qi, qn, qout;
    float f, d;
    int rc;
    int i;

    a.x = 1.0f; a.y = 2.0f; a.z = 3.0f;
    b.x = 4.0f; b.y = 5.0f; b.z = 6.0f;
    z.x = 0.0f; z.y = 0.0f; z.z = 0.0f;
    axis.x = 0.0f; axis.y = 0.0f; axis.z = 1.0f;

    /* ---- finite helper ------------------------------------------------ */
    check("t_finite_ok", math3d_finite(1.0f) == 1 && math3d_finite(-3.5f) == 1);
    check("t_finite_nan", math3d_finite(quiet_nan()) == 0);
    check("t_finite_inf", math3d_finite(pos_inf()) == 0 && math3d_finite(-pos_inf()) == 0);

    /* ---- vec3 --------------------------------------------------------- */
    rc = vec3_add(a, b, &c);
    check("t_v_add", rc == MATH3D_OK && c.x == 5.0f && c.y == 7.0f && c.z == 9.0f);
    rc = vec3_sub(b, a, &c);
    check("t_v_sub", rc == MATH3D_OK && c.x == 3.0f && c.y == 3.0f && c.z == 3.0f);
    c.x = 0.0f; c.y = 0.0f; c.z = 0.0f;
    rc = vec3_scale(a, 2.0f, &c);
    check("t_v_scale", rc == MATH3D_OK && c.x == 2.0f && c.y == 4.0f && c.z == 6.0f);
    f = -1.0f;
    rc = vec3_dot(a, b, &f);
    check("t_v_dot", rc == MATH3D_OK && f == 32.0f);
    c.x = 0.0f; c.y = 0.0f; c.z = 0.0f;
    n.x = 0.0f; n.y = 1.0f; n.z = 0.0f;
    rc = vec3_cross(axis, n, &c);
    /* z cross y == -x */
    check("t_v_cross", rc == MATH3D_OK && c.x == -1.0f && c.y == 0.0f && c.z == 0.0f);
    c.x = 3.0f; c.y = 4.0f; c.z = 0.0f;
    f = 0.0f;
    rc = vec3_length(c, &f);
    check("t_v_length", rc == MATH3D_OK && f == 5.0f);
    n.x = 0.0f; n.y = 0.0f; n.z = 0.0f;
    rc = vec3_normalize(c, &n);
    check("t_v_normalize", rc == MATH3D_OK &&
                          near(n.x, 0.6f, 1e-5f) && near(n.y, 0.8f, 1e-5f) && n.z == 0.0f);

    /* ---- mat4 identity / multiply ------------------------------------- */
    m.m[0] = 9.0f;
    rc = mat4_identity(&m);
    {
        int ok = (rc == MATH3D_OK);
        for (i = 0; i < 16; i++) {
            float want = ((i % 5) == 0) ? 1.0f : 0.0f;
            if (m.m[i] != want) ok = 0;
        }
        check("t_m_identity", ok);
    }
    mat4_identity(&m);
    mat4_identity(&r);
    r.m[12] = 7.0f; r.m[13] = 8.0f; r.m[14] = 9.0f;
    rc = mat4_multiply(m, r, &m);
    check("t_m_multiply_id", rc == MATH3D_OK &&
                            m.m[12] == 7.0f && m.m[13] == 8.0f && m.m[14] == 9.0f &&
                            m.m[0] == 1.0f && m.m[15] == 1.0f);

    /* ---- mat4 translate / scale --------------------------------------- */
    mat4_identity(&m);
    b.x = 1.0f; b.y = 2.0f; b.z = 3.0f;
    rc = mat4_translate(m, b, &r);
    check("t_m_translate", rc == MATH3D_OK &&
                          r.m[12] == 1.0f && r.m[13] == 2.0f && r.m[14] == 3.0f &&
                          r.m[15] == 1.0f && r.m[0] == 1.0f);
    mat4_identity(&m);
    b.x = 2.0f; b.y = 3.0f; b.z = 4.0f;
    rc = mat4_scale(m, b, &r);
    check("t_m_scale", rc == MATH3D_OK &&
                      r.m[0] == 2.0f && r.m[5] == 3.0f && r.m[10] == 4.0f &&
                      r.m[15] == 1.0f && r.m[12] == 0.0f);

    /* ---- mat4 rotation correctness: 90 deg about +Z ------------------- */
    mat4_identity(&m);
    axis.x = 0.0f; axis.y = 0.0f; axis.z = 1.0f;
    rc = mat4_rotate(m, 1.57079632679f, axis, &r);
    check("t_m_rotate", rc == MATH3D_OK &&
                       near(r.m[0], 0.0f, 1e-5f) && near(r.m[1], 1.0f, 1e-5f) &&
                       near(r.m[4], -1.0f, 1e-5f) && near(r.m[5], 0.0f, 1e-5f) &&
                       near(r.m[10], 1.0f, 1e-5f) && r.m[15] == 1.0f);
    rm = r; /* keep for quaternion round-trip */

    /* ---- mat4 perspective: fovy=90, aspect=1, near=1, far=100 --------- */
    rc = mat4_perspective(1.57079632679f, 1.0f, 1.0f, 100.0f, &p);
    check("t_m_perspective", rc == MATH3D_OK &&
                            near(p.m[0], 1.0f, 1e-5f) && near(p.m[5], 1.0f, 1e-5f) &&
                            p.m[11] == -1.0f && p.m[15] == 0.0f &&
                            p.m[10] < 0.0f && p.m[14] < 0.0f);

    /* ---- mat4 look_at: eye (0,0,5) -> origin -------------------------- */
    a.x = 0.0f; a.y = 0.0f; a.z = 5.0f;
    b.x = 0.0f; b.y = 0.0f; b.z = 0.0f;
    c.x = 0.0f; c.y = 1.0f; c.z = 0.0f;
    rc = mat4_look_at(a, b, c, &v);
    check("t_m_look_at", rc == MATH3D_OK &&
                        v.m[0] == 1.0f && v.m[5] == 1.0f && near(v.m[10], 1.0f, 1e-5f) &&
                        near(v.m[14], -5.0f, 1e-5f) && v.m[15] == 1.0f);

    /* ---- quaternion basics -------------------------------------------- */
    qi.x = 0.0f; qi.y = 0.0f; qi.z = 0.0f; qi.w = 1.0f;
    q.x = 1.0f; q.y = 2.0f; q.z = 3.0f; q.w = 4.0f;
    rc = quat_multiply(qi, q, &qout);
    check("t_q_mul_identity", rc == MATH3D_OK &&
                             qout.x == q.x && qout.y == q.y &&
                             qout.z == q.z && qout.w == q.w);
    q.x = 0.0f; q.y = 0.0f; q.z = 0.0f; q.w = 2.0f;
    rc = quat_normalize(q, &qn);
    check("t_q_normalize", rc == MATH3D_OK && qn.w == 1.0f &&
                          qn.x == 0.0f && qn.y == 0.0f && qn.z == 0.0f);

    /* ---- quaternion <-> matrix round-trip: 90 deg about +Z ----------- */
    axis.x = 0.0f; axis.y = 0.0f; axis.z = 1.0f;
    rc = quat_from_axis_angle(axis, 1.57079632679f, &q);
    check("t_q_from_axis", rc == MATH3D_OK &&
                          near(q.z, 0.70710678f, 1e-5f) && near(q.w, 0.70710678f, 1e-5f) &&
                          q.x == 0.0f && q.y == 0.0f);
    rc = quat_to_mat4(q, &qm);
    {
        int ok = (rc == MATH3D_OK);
        for (i = 0; i < 16; i++)
            if (!near(qm.m[i], rm.m[i], 1e-4f)) ok = 0;
        check("t_q_roundtrip", ok);
    }

    /* ---- NaN / Inf refusal -------------------------------------------- */
    a.x = quiet_nan(); a.y = 1.0f; a.z = 2.0f;
    b.x = 1.0f; b.y = 2.0f; b.z = 3.0f;
    c.x = 9.0f; c.y = 9.0f; c.z = 9.0f;
    rc = vec3_add(a, b, &c);
    check("t_refuse_v_add", rc == MATH3D_ERR_NONFINITE &&
                           c.x == 0.0f && c.y == 0.0f && c.z == 0.0f);
    b.x = pos_inf(); b.y = 0.0f; b.z = 0.0f;
    c.x = 9.0f; c.y = 9.0f; c.z = 9.0f;
    rc = vec3_scale(b, 2.0f, &c);
    check("t_refuse_v_scale_inf", rc == MATH3D_ERR_NONFINITE && c.x == 0.0f);
    a.x = pos_inf(); a.y = 0.0f; a.z = 0.0f;
    b.x = 1.0f; b.y = 0.0f; b.z = 0.0f;
    f = -1.0f;
    rc = vec3_dot(a, b, &f);
    check("t_refuse_v_dot", rc == MATH3D_ERR_NONFINITE && f == 0.0f);
    mat4_identity(&m);
    m.m[5] = quiet_nan();
    mat4_identity(&r);
    r.m[0] = 42.0f;
    rc = mat4_multiply(m, r, &r);
    check("t_refuse_m_multiply", rc == MATH3D_ERR_NONFINITE && r.m[0] == 1.0f);
    q.x = 0.0f; q.y = 0.0f; q.z = 0.0f; q.w = quiet_nan();
    mat4_identity(&qm);
    rc = quat_to_mat4(q, &qm);
    check("t_refuse_q_nonfinite", rc == MATH3D_ERR_NONFINITE && qm.m[0] == 1.0f);
    rc = mat4_perspective(quiet_nan(), 1.0f, 1.0f, 100.0f, &p);
    check("t_refuse_perspective", rc == MATH3D_ERR_NONFINITE && p.m[0] == 1.0f);

    /* ---- division-by-zero guards -------------------------------------- */
    z.x = 0.0f; z.y = 0.0f; z.z = 0.0f;
    c.x = 9.0f; c.y = 9.0f; c.z = 9.0f;
    rc = vec3_normalize(z, &c);
    check("t_zero_v_normalize", rc == MATH3D_ERR_ZERO &&
                               c.x == 0.0f && c.y == 0.0f && c.z == 0.0f);
    mat4_identity(&m);
    rc = mat4_rotate(m, 1.0f, z, &r);
    check("t_zero_m_rotate_axis", rc == MATH3D_ERR_ZERO && r.m[0] == 1.0f);
    rc = mat4_perspective(1.0f, 0.0f, 1.0f, 100.0f, &p);
    check("t_zero_persp_aspect", rc == MATH3D_ERR_ZERO && p.m[0] == 1.0f);
    rc = mat4_perspective(1.0f, 1.0f, 5.0f, 5.0f, &p);
    check("t_zero_persp_nearfar", rc == MATH3D_ERR_ZERO && p.m[0] == 1.0f);
    a.x = 0.0f; a.y = 0.0f; a.z = 0.0f; /* eye == center */
    b.x = 0.0f; b.y = 0.0f; b.z = 0.0f;
    c.x = 0.0f; c.y = 1.0f; c.z = 0.0f;
    rc = mat4_look_at(a, b, c, &v);
    check("t_zero_lookat_eye", rc == MATH3D_ERR_ZERO && v.m[0] == 1.0f);
    a.x = 0.0f; a.y = 0.0f; a.z = 5.0f; /* up parallel to forward */
    b.x = 0.0f; b.y = 0.0f; b.z = 0.0f;
    c.x = 0.0f; c.y = 0.0f; c.z = 1.0f;
    rc = mat4_look_at(a, b, c, &v);
    check("t_zero_lookat_up", rc == MATH3D_ERR_ZERO && v.m[0] == 1.0f);
    rc = quat_from_axis_angle(z, 1.0f, &qout);
    check("t_zero_q_axis", rc == MATH3D_ERR_ZERO && qout.w == 0.0f);
    q.x = 0.0f; q.y = 0.0f; q.z = 0.0f; q.w = 0.0f;
    rc = quat_normalize(q, &qout);
    check("t_zero_q_normalize", rc == MATH3D_ERR_ZERO && qout.w == 0.0f);
    rc = quat_to_mat4(q, &qm);
    check("t_zero_q_to_mat4", rc == MATH3D_ERR_ZERO && qm.m[0] == 1.0f);

    /* Direct reach for finite/zero helpers (coverage tool call sites).
     * Deliberately silent: this suite pins its exact stdout fixture. */
    {
        vec3 v = vec3_zero();
        mat4 mz;
        quat qz = quat_zero();
        /* finite predicates return 1 (finite) / 0 (non-finite). */
        if (v.x != 0.0f || vec3_finite(v) != 1) g_failed++;
        mat4_identity(&mz);
        if (mat4_finite(mz) != 1) g_failed++;
        if (quat_finite(qz) != 1) g_failed++;
    }

    print("MATH3D", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
