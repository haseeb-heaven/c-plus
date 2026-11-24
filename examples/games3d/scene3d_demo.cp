/* Deterministic 3D scene demo built on the new additive modern headers:
 *   stdlib/math3d.hp  -- vec3 / quat / mat4 with finite-input refusals
 *   stdlib/mesh3d.hp  -- allocator-aware cube mesh, AABB, validation
 *   stdlib/scene3d.hp -- stable-index scene graph with cycle refusal
 *
 * This is NOT an interactive game and it does NOT touch a GPU.  It builds a
 * cube mesh, places a fixed set of nodes in a scene graph, converts each
 * node's stored axis-angle rotation into a quaternion and a mat4, composes
 * world transforms, computes world-space bounding boxes, and prints a fixed
 * transcript (node table + top-down ASCII occupancy grid + camera projection).
 * Every emitted byte is folded into an FNV-1a 32-bit checksum printed on the
 * CHECKSUM line, so a transcript is reproducible and self-verifying.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/games3d/scene3d_demo.cp \
 *       --default --sanitize=none -I. -o examples/games3d/bin/scene3d_demo \
 *       -- build/libc/libcplus_hardened.a
 *
 * Run:
 *   bin/scene3d_demo                 # built-in scene + transcript + checksum
 *   bin/scene3d_demo SHOW            # same as no arguments
 *   bin/scene3d_demo NAN             # refuse: NaN transform
 *   bin/scene3d_demo INF             # refuse: infinite rotation axis
 *   bin/scene3d_demo INDEX 8         # refuse: bad mesh vertex index
 *   bin/scene3d_demo NODE 9          # refuse: bad scene child index
 *   bin/scene3d_demo CYCLE           # refuse: reparent under a descendant
 *   bin/scene3d_demo DEGENERATE      # refuse: repeated face index
 *   bin/scene3d_demo PARSE 12x       # refuse: malformed numeric token
 *
 * Exit 0 on the accepted demo, 2 on the first refused hostile input.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/math3d.hp"
#include "stdlib/mesh3d.hp"
#include "stdlib/scene3d.hp"

#define DEMO_NODES 8
#define GRID_W 32
#define GRID_H 16
#define WORLD_LO -8.0f
#define WORLD_HI 8.0f

/* ---- fixed scene description (index-aligned with the scene graph) ------ */
static const char *G_NAME[DEMO_NODES] = {
    "root", "platform", "cube_A", "cube_B", "cube_C", "lights", "sun", 0
};
static const int G_MESH[DEMO_NODES] = {0, 0, 1, 1, 1, 0, 0};
static const char G_GLYPH[DEMO_NODES] = {'r', 'p', 'A', 'B', 'C', 'l', 's', '?'};
static const float G_POS[DEMO_NODES][3] = {
    { 0.0f, 0.0f, 0.0f}, /* root */
    { 0.0f, 0.0f, 0.0f}, /* platform */
    {-3.0f, 0.0f, 0.0f}, /* cube_A */
    { 0.0f, 0.0f, 0.0f}, /* cube_B */
    { 3.0f, 0.0f, 0.0f}, /* cube_C */
    { 0.0f, 4.0f, 0.0f}, /* lights */
    { 0.0f, 4.0f, 0.0f}  /* sun */
};
static const size_t G_PARENT[DEMO_NODES] = {0, 0, 1, 1, 1, 0, 5};
/* rotation is stored as axis * angle(radians); magnitude encodes the angle. */
static const float G_ROT[DEMO_NODES][3] = {
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f},
    {0.0f, 1.5707963267948966f, 0.0f},   /* 90 deg about +Y */
    {0.7853981633974483f, 0.0f, 0.0f},   /* 45 deg about +X */
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f}
};
static const float G_SCALE[DEMO_NODES][3] = {
    {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.5f}, {0.75f, 0.75f, 0.75f},
    {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}
};

/* ---- deterministic output + checksum ----------------------------------- */
static unsigned int G_SUM = 2166136261u; /* FNV-1a offset basis */
static int G_LINES = 0;
static int G_BYTES = 0;

static void fold_text(const char *s) {
    while (*s != '\0') {
        G_SUM ^= (unsigned int)(unsigned char)(*s);
        G_SUM *= 16777619u;
        G_BYTES++;
        s++;
    }
}

/* Format one transcript line, print it, and fold it into the checksum. */
static void emit(const char *fmt, ...) {
    char buf[640];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        buf[0] = '\0';
    printf("%s\n", buf);
    fold_text(buf);
    G_LINES++;
}

static float clean(float f) {
    if (f > -0.0005f && f < 0.0005f)
        return 0.0f;
    return f;
}

/* Produce non-finite values without union type-punning (which the C+
 * lifetime checker rejects).  Floating division by zero is defined by
 * IEEE-754 and yields the required NaN / +Inf at run time. */
static float quiet_nan(void) {
    float z = 0.0f;
    return z / z;
}

static float pos_inf(void) {
    float z = 0.0f;
    float one = 1.0f;
    return one / z;
}

static int refuse(const char *code, const char *reason) {
    printf("REFUSE code=%s reason=%s\n", code, reason);
    printf("SCENE_DONE status=REFUSED code=%s\n", code);
    return 2;
}

/* Strict integer parse: 0 ok, -1 malformed, -2 oversized. */
static int parse_int(const char *arg, int *out) {
    const char *p = arg;
    int negative = 0;
    int value = 0;
    int digits = 0;
    if (arg == 0 || arg[0] == '\0')
        return -1;
    if (*p == '-') { negative = 1; p++; }
    else if (*p == '+') { p++; }
    if (*p == '\0')
        return -1;
    while (*p != '\0') {
        if (*p < '0' || *p > '9')
            return -1;
        if (value > 100000 || digits >= 8)
            return -2;
        value = value * 10 + (*p - '0');
        digits++;
        p++;
    }
    *out = negative ? -value : value;
    return 0;
}

/* ---- mesh + scene construction ----------------------------------------- */
static const float CUBE_P[8][3] = {
    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
};
static const int CUBE_T[12][3] = {
    {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
    {0,4,7}, {0,7,3}, {1,2,6}, {1,6,5},
    {0,1,5}, {0,5,4}, {3,7,6}, {3,6,2}
};

static int build_cube(mesh3d *m, error *err) {
    int i;
    if (mesh3d_init(m, (const stdlib_allocator *)0, err) != STDLIB_OK)
        return -1;
    for (i = 0; i < 8; i++) {
        mesh3d_vertex v;
        v.position.x = CUBE_P[i][0];
        v.position.y = CUBE_P[i][1];
        v.position.z = CUBE_P[i][2];
        v.normal.x = 0.0f; v.normal.y = 0.0f; v.normal.z = 0.0f;
        if (mesh3d_add_vertex(m, &v, (size_t *)0, err) != STDLIB_OK)
            return -1;
    }
    for (i = 0; i < 12; i++) {
        mesh3d_triangle t;
        t.i0 = (size_t)CUBE_T[i][0];
        t.i1 = (size_t)CUBE_T[i][1];
        t.i2 = (size_t)CUBE_T[i][2];
        if (mesh3d_add_triangle(m, &t, err) != STDLIB_OK)
            return -1;
    }
    if (mesh3d_compute_normals(m, err) != STDLIB_OK)
        return -1;
    return 0;
}

static size_t add_node(scene3d_scene *s, size_t parent, int i, error *err) {
    scene3d_transform t = scene3d_transform_identity();
    size_t index;
    t.position.x = G_POS[i][0];
    t.position.y = G_POS[i][1];
    t.position.z = G_POS[i][2];
    t.rotation.x = G_ROT[i][0];
    t.rotation.y = G_ROT[i][1];
    t.rotation.z = G_ROT[i][2];
    t.scale.x = G_SCALE[i][0];
    t.scale.y = G_SCALE[i][1];
    t.scale.z = G_SCALE[i][2];
    index = scene3d_node_add(s, parent, G_NAME[i], err);
    if (index == SCENE3D_NO_INDEX)
        return index;
    if (scene3d_node_transform_set(s, index, &t, err) != STDLIB_OK)
        return SCENE3D_NO_INDEX;
    return index;
}

static int build_scene(scene3d_scene *s, error *err) {
    int i;
    if (scene3d_init(s, (const stdlib_allocator *)0, err) != STDLIB_OK)
        return -1;
    for (i = 1; i < 7; i++) {
        if (add_node(s, G_PARENT[i], i, err) == SCENE3D_NO_INDEX)
            return -1;
    }
    return 0;
}

/* ---- quaternion / matrix transform pipeline ---------------------------- */
static int local_matrix(const scene3d_transform *t, mat4 *out) {
    vec3 pos, rv, axis, scale;
    mat4 m0, m1, m2, m3;
    quat q;
    float angle;
    if (mat4_identity(&m0) != MATH3D_OK)
        return -1;
    pos.x = t->position.x; pos.y = t->position.y; pos.z = t->position.z;
    if (mat4_translate(m0, pos, &m1) != MATH3D_OK)
        return -1;
    rv.x = t->rotation.x; rv.y = t->rotation.y; rv.z = t->rotation.z;
    if (vec3_length(rv, &angle) != MATH3D_OK)
        return -1;
    if (angle > 1e-6f) {
        if (vec3_scale(rv, 1.0f / angle, &axis) != MATH3D_OK)
            return -1;
    } else {
        axis.x = 0.0f; axis.y = 0.0f; axis.z = 1.0f;
        angle = 0.0f;
    }
    if (quat_from_axis_angle(axis, angle, &q) != MATH3D_OK)
        return -1;
    if (quat_to_mat4(q, &m2) != MATH3D_OK)
        return -1;
    if (mat4_multiply(m1, m2, &m3) != MATH3D_OK)
        return -1;
    scale.x = t->scale.x; scale.y = t->scale.y; scale.z = t->scale.z;
    if (mat4_scale(m3, scale, out) != MATH3D_OK)
        return -1;
    return 0;
}

static vec3 mat_point(const mat4 *m, float x, float y, float z) {
    vec3 r;
    r.x = m->m[0] * x + m->m[4] * y + m->m[8]  * z + m->m[12];
    r.y = m->m[1] * x + m->m[5] * y + m->m[9]  * z + m->m[13];
    r.z = m->m[2] * x + m->m[6] * y + m->m[10] * z + m->m[14];
    return r;
}

static int clampi(float v, int lo, int hi) {
    int i;
    if (v < (float)lo) return lo;
    if (v > (float)hi) return hi;
    i = (int)v;
    return i;
}

/* ---- accepted demo ------------------------------------------------------ */
static int run_demo(void) {
    mesh3d mesh;
    mesh3d_validation report;
    scene3d_scene scene;
    error err;
    size_t order[DEMO_NODES];
    size_t used = 0;
    /* Literal extents (not DEMO_NODES) here: the C+ lowering wraps
     * macro-sized arrays of struct types in an invalid CPLUS_NONNULL
     * declaration.  Eight is exactly DEMO_NODES. */
    mat4 world[8];
    mat4 view, proj, vp, local;
    vec3 eye, center, up;
    mesh3d_aabb box[8];
    mesh3d_vec3 ctr[8];
    int has_box[8];
    /* Literal dims: the C+ lowering wrongly wraps array-bound expressions
     * in a runtime bounds check.  16 x 34 == GRID_H x (GRID_W + 2). */
    char grid[16][34];
    int visible_count = 0;
    int mesh_count = 0;
    float aspect = 1.6f;
    float near_z = 0.1f;
    float far_z = 100.0f;
    float fovy = 1.0471975511965976f; /* 60 degrees */
    size_t k, i;
    int r, c, row;

    if (build_cube(&mesh, &err) != 0)
        return refuse("E_INTERNAL", "cube mesh construction failed");
    if (mesh3d_validate(&mesh, &report, &err) != STDLIB_OK)
        return refuse("E_GEOM", "cube mesh failed validation");
    if (build_scene(&scene, &err) != 0)
        return refuse("E_INTERNAL", "scene graph construction failed");

    if (scene3d_traverse(&scene, scene3d_root(&scene), order,
                         DEMO_NODES, &used, &err) != STDLIB_OK)
        return refuse("E_BOUNDS", "scene traversal buffer too small");

    eye.x = 8.0f; eye.y = 6.0f; eye.z = 8.0f;
    center.x = 0.0f; center.y = 0.0f; center.z = 0.0f;
    up.x = 0.0f; up.y = 1.0f; up.z = 0.0f;
    if (mat4_look_at(eye, center, up, &view) != MATH3D_OK)
        return refuse("E_NONFINITE", "camera view matrix refused");
    if (mat4_perspective(fovy, aspect, near_z, far_z, &proj) != MATH3D_OK)
        return refuse("E_NONFINITE", "camera projection matrix refused");
    if (mat4_multiply(proj, view, &vp) != MATH3D_OK)
        return refuse("E_NONFINITE", "camera matrix product refused");

    for (k = 0; k < used; k++) {
        size_t idx = order[k];
        scene3d_transform t;
        size_t parent;
        if (scene3d_node_transform_get(&scene, idx, &t, &err) != STDLIB_OK)
            return refuse("E_BOUNDS", "cannot read node transform");
        if (local_matrix(&t, &local) != 0)
            return refuse("E_NONFINITE", "node transform has a non-finite input");
        parent = scene3d_node_parent(&scene, idx);
        if (parent == SCENE3D_NO_INDEX) {
            world[idx] = local;
        } else if (mat4_multiply(world[parent], local, &world[idx]) != MATH3D_OK) {
            return refuse("E_NONFINITE", "world matrix composition refused");
        }
        if (scene3d_node_is_visible(&scene, idx))
            visible_count++;

        has_box[idx] = 0;
        if (G_MESH[idx] != 0) {
            mesh3d inst;
            int v;
            if (mesh3d_init(&inst, (const stdlib_allocator *)0, &err) != STDLIB_OK)
                return refuse("E_INTERNAL", "instance mesh init failed");
            for (v = 0; v < 8; v++) {
                mesh3d_vertex mv;
                vec3 wp = mat_point(&world[idx],
                                    CUBE_P[v][0], CUBE_P[v][1], CUBE_P[v][2]);
                mv.position.x = wp.x; mv.position.y = wp.y; mv.position.z = wp.z;
                mv.normal.x = 0.0f; mv.normal.y = 0.0f; mv.normal.z = 0.0f;
                if (mesh3d_add_vertex(&inst, &mv, (size_t *)0, &err) != STDLIB_OK)
                    return refuse("E_NONFINITE", "world vertex was not finite");
            }
            if (mesh3d_bounding_box(&inst, &box[idx], &err) != STDLIB_OK)
                return refuse("E_NONFINITE", "world-space AABB refused");
            if (mesh3d_center(&inst, &ctr[idx], &err) != STDLIB_OK)
                return refuse("E_GEOM", "world-space center refused");
            mesh3d_drop(&inst);
            has_box[idx] = 1;
            mesh_count++;
        }
    }

    /* transcript -------------------------------------------------------- */
    emit("SCENE3D demo mesh=cube vertices=%zu triangles=%zu",
         mesh3d_vertex_count(&mesh), mesh3d_triangle_count(&mesh));
    {
        mesh3d_aabb base;
        if (mesh3d_bounding_box(&mesh, &base, &err) != STDLIB_OK)
            return refuse("E_GEOM", "base AABB refused");
        emit("MESH cube aabb_min=(%.3f,%.3f,%.3f) aabb_max=(%.3f,%.3f,%.3f)",
             clean(base.min.x), clean(base.min.y), clean(base.min.z),
             clean(base.max.x), clean(base.max.y), clean(base.max.z));
    }
    emit("VALIDATE nonfinite=%zu bad_index=%zu degenerate=%zu",
         report.nonfinite_vertices, report.bad_index_triangles,
         report.degenerate_triangles);
    emit("CAMERA eye=(%.3f,%.3f,%.3f) center=(%.3f,%.3f,%.3f) fov=%.3f aspect=%.3f near=%.3f far=%.3f",
         eye.x, eye.y, eye.z, center.x, center.y, center.z,
         fovy, aspect, near_z, far_z);
    emit("PROJ m0=%.6f m5=%.6f m10=%.6f m11=%.6f m14=%.6f",
         clean(proj.m[0]), clean(proj.m[5]), clean(proj.m[10]),
         clean(proj.m[11]), clean(proj.m[14]));
    emit("NODES total=%zu visible=%d meshes=%d", used, visible_count, mesh_count);

    for (k = 0; k < used; k++) {
        size_t idx = order[k];
        const char *name = scene3d_node_name(&scene, idx);
        emit("NODE %zu name=%s depth=%zu children=%zu visible=%d mesh=%d",
             idx, name == 0 ? "?" : name,
             scene3d_node_depth(&scene, idx),
             scene3d_node_child_count(&scene, idx),
             scene3d_node_is_visible(&scene, idx), G_MESH[idx]);
        if (has_box[idx] != 0) {
            float cx = ctr[idx].x, cy = ctr[idx].y, cz = ctr[idx].z;
            float sx, sy, sz, sw;
            emit("  WORLD %zu t=(%.3f,%.3f,%.3f)",
                 idx, clean(world[idx].m[12]),
                 clean(world[idx].m[13]), clean(world[idx].m[14]));
            emit("  AABB %zu min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) center=(%.3f,%.3f,%.3f)",
                 idx, clean(box[idx].min.x), clean(box[idx].min.y),
                 clean(box[idx].min.z), clean(box[idx].max.x),
                 clean(box[idx].max.y), clean(box[idx].max.z),
                 clean(cx), clean(cy), clean(cz));
            sx = vp.m[0] * cx + vp.m[4] * cy + vp.m[8]  * cz + vp.m[12];
            sy = vp.m[1] * cx + vp.m[5] * cy + vp.m[9]  * cz + vp.m[13];
            sz = vp.m[2] * cx + vp.m[6] * cy + vp.m[10] * cz + vp.m[14];
            sw = vp.m[3] * cx + vp.m[7] * cy + vp.m[11] * cz + vp.m[15];
            if (sw == 0.0f) {
                return refuse("E_NONFINITE", "degenerate projection (w == 0)");
            }
            emit("  SCREEN %zu ndc=(%.3f,%.3f) depth=%.3f",
                 idx, clean(sx / sw), clean(sy / sw), clean(sz / sw));
        }
    }

    /* top-down XZ occupancy grid ---------------------------------------- */
    for (r = 0; r < GRID_H; r++) {
        for (c = 0; c < GRID_W; c++)
            grid[r][c] = '.';
        grid[r][GRID_W] = '\0';
    }
    for (k = 0; k < used; k++) {
        size_t idx = order[k];
        int c0, c1, r0, r1;
        if (has_box[idx] == 0 || !scene3d_node_is_visible(&scene, idx))
            continue;
        c0 = clampi((box[idx].min.x - WORLD_LO) * (float)GRID_W /
                    (WORLD_HI - WORLD_LO), 0, GRID_W - 1);
        c1 = clampi((box[idx].max.x - WORLD_LO) * (float)GRID_W /
                    (WORLD_HI - WORLD_LO), 0, GRID_W - 1);
        r0 = clampi((WORLD_HI - box[idx].max.z) * (float)GRID_H /
                    (WORLD_HI - WORLD_LO), 0, GRID_H - 1);
        r1 = clampi((WORLD_HI - box[idx].min.z) * (float)GRID_H /
                    (WORLD_HI - WORLD_LO), 0, GRID_H - 1);
        for (r = r0; r <= r1; r++) {
            for (c = c0; c <= c1; c++)
                grid[r][c] = G_GLYPH[idx];
        }
    }
    emit("GRID xz cols=%d rows=%d range=[%.3f,%.3f]",
         GRID_W, GRID_H, WORLD_LO, WORLD_HI);
    for (r = 0; r < GRID_H; r++)
        emit("%s", grid[r]);
    emit("GRID_END");

    printf("CHECKSUM fnv1a32=%08x bytes=%d lines=%d\n",
           G_SUM, G_BYTES, G_LINES);
    printf("SCENE_DONE status=OK nodes=%zu meshes=%d\n", used, mesh_count);

    scene3d_drop(&scene);
    mesh3d_drop(&mesh);
    return 0;
}

/* ---- hostile inputs ----------------------------------------------------- */
static int hostile_nan(void) {
    vec3 a, b, out;
    int rc;
    a.x = quiet_nan(); a.y = 0.0f; a.z = 0.0f;
    b = vec3_zero();
    rc = vec3_add(a, b, &out);
    if (rc == MATH3D_ERR_NONFINITE)
        return refuse("E_NONFINITE", "NaN transform input rejected by math3d");
    return refuse("E_INTERNAL", "NaN finite guard did not fire");
}

static int hostile_inf(void) {
    vec3 axis;
    quat q;
    int rc;
    axis.x = 0.0f; axis.y = pos_inf(); axis.z = 0.0f;
    rc = quat_from_axis_angle(axis, 1.0f, &q);
    if (rc == MATH3D_ERR_NONFINITE)
        return refuse("E_NONFINITE", "infinite rotation axis rejected by math3d");
    return refuse("E_INTERNAL", "infinite finite guard did not fire");
}

static int hostile_index(int n) {
    mesh3d m;
    error e;
    const mesh3d_vertex *v = 0;
    int rc;
    if (build_cube(&m, &e) != 0)
        return refuse("E_INTERNAL", "cube mesh construction failed");
    rc = mesh3d_vertex_at(&m, (size_t)n, &v, &e);
    mesh3d_drop(&m);
    if (n >= 0 && n < 8)
        return refuse("E_FORMAT", "expected an out-of-range mesh index");
    if (rc == STDLIB_FAIL && e.failed_now() &&
        e.category() == STDLIB_CATEGORY_BOUNDS)
        return refuse("E_INDEX", "mesh vertex index out of range");
    return refuse("E_INTERNAL", "mesh index guard did not fire");
}

static int hostile_node(int n) {
    scene3d_scene s;
    error e;
    size_t child;
    if (build_scene(&s, &e) != 0)
        return refuse("E_INTERNAL", "scene graph construction failed");
    {
        int child_total = (int)scene3d_node_child_count(&s, scene3d_root(&s));
        child = scene3d_node_child_at(&s, scene3d_root(&s), (size_t)n);
        scene3d_drop(&s);
        if (n >= 0 && n < child_total)
            return refuse("E_FORMAT", "expected an out-of-range scene child index");
    }
    if (child == SCENE3D_NO_INDEX)
        return refuse("E_BOUNDS", "scene child index out of range");
    return refuse("E_INTERNAL", "scene index guard did not fire");
}

static int hostile_cycle(void) {
    scene3d_scene s;
    error e;
    int rc;
    if (build_scene(&s, &e) != 0)
        return refuse("E_INTERNAL", "scene graph construction failed");
    /* platform is the parent of cube_A; making platform a child of cube_A
     * would close a cycle. */
    rc = scene3d_node_reparent(&s, (size_t)1, (size_t)2, &e);
    if (rc == STDLIB_OK || !e.failed_now() ||
        e.category() != STDLIB_CATEGORY_OWNERSHIP) {
        scene3d_drop(&s);
        return refuse("E_INTERNAL", "cycle guard did not fire");
    }
    scene3d_drop(&s);
    return refuse("E_CYCLE", "reparent under a descendant is a cycle");
}

static int hostile_degenerate(void) {
    mesh3d m;
    error e;
    mesh3d_vertex v;
    mesh3d_triangle t;
    int i;
    if (mesh3d_init(&m, (const stdlib_allocator *)0, &e) != STDLIB_OK)
        return refuse("E_INTERNAL", "mesh init failed");
    for (i = 0; i < 3; i++) {
        v.position.x = CUBE_P[i][0];
        v.position.y = CUBE_P[i][1];
        v.position.z = CUBE_P[i][2];
        v.normal.x = 0.0f; v.normal.y = 0.0f; v.normal.z = 0.0f;
        if (mesh3d_add_vertex(&m, &v, (size_t *)0, &e) != STDLIB_OK) {
            mesh3d_drop(&m);
            return refuse("E_INTERNAL", "mesh vertex add failed");
        }
    }
    t.i0 = 0; t.i1 = 0; t.i2 = 1; /* repeated index */
    i = mesh3d_add_triangle(&m, &t, &e);
    mesh3d_drop(&m);
    if (i == STDLIB_FAIL && e.failed_now())
        return refuse("E_INDEX", "repeated face index rejected by mesh3d");
    return refuse("E_INTERNAL", "degenerate face guard did not fire");
}

int main(int argc, char **argv) {
    const char *cmd;
    int value = 0;
    int pr;
    if (argc <= 1)
        return run_demo();
    cmd = argv[1];
    if (strcmp(cmd, "SHOW") == 0)
        return run_demo();
    if (strcmp(cmd, "NAN") == 0)
        return hostile_nan();
    if (strcmp(cmd, "INF") == 0)
        return hostile_inf();
    if (strcmp(cmd, "CYCLE") == 0)
        return hostile_cycle();
    if (strcmp(cmd, "DEGENERATE") == 0)
        return hostile_degenerate();
    if (strcmp(cmd, "INDEX") == 0) {
        if (argc < 3)
            return refuse("E_PARSE", "INDEX needs an integer operand");
        pr = parse_int(argv[2], &value);
        if (pr != 0)
            return refuse("E_PARSE", pr == -2 ? "index operand is oversized"
                                             : "index operand is malformed");
        return hostile_index(value);
    }
    if (strcmp(cmd, "NODE") == 0) {
        if (argc < 3)
            return refuse("E_PARSE", "NODE needs an integer operand");
        pr = parse_int(argv[2], &value);
        if (pr != 0)
            return refuse("E_PARSE", pr == -2 ? "node operand is oversized"
                                             : "node operand is malformed");
        return hostile_node(value);
    }
    if (strcmp(cmd, "PARSE") == 0) {
        if (argc < 3)
            return refuse("E_PARSE", "PARSE needs an operand");
        pr = parse_int(argv[2], &value);
        if (pr != 0)
            return refuse("E_PARSE", "malformed numeric token");
        return refuse("E_FORMAT", "token unexpectedly parsed as an integer");
    }
    return refuse("E_PARSE", "unknown command token");
}
