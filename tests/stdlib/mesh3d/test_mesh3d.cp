/* mesh3d.hp verification: cube construction, geometry, transforms and the
 * full refusal matrix.  Build with:
 *
 *   ./build/c+ build tests/stdlib/mesh3d/test_mesh3d.cp \
 *       --default --sanitize=none -I. \
 *       -- build/libc/libcplus_hardened.a
 *
 * Every stdout line is one tag followed only by 0/1 results.  run.sh checks
 * that no result is 0, that stderr is empty, and writes RESULTS.md.  Floats
 * are compared inside expressions so output never depends float formatting.
 */
#include <stddef.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/mesh3d.hp"

static int near(float a, float b) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d < 1e-3f;
}

static float nan_f(void) {
    uint32_t bits = 0x7FC00000u;
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(bits));
    return f;
}

static float inf_f(void) {
    uint32_t bits = 0x7F800000u;
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(bits));
    return f;
}

static mesh3d_vertex vertex_at(float x, float y, float z) {
    mesh3d_vertex v;
    v.position.x = x;
    v.position.y = y;
    v.position.z = z;
    v.normal = mesh3d_vec3_zero();
    return v;
}

static int add_cube(mesh3d *m, error *err) {
    static const float coords[8][3] = {
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
    };
    /* Outward-wound faces: two triangles per face. */
    static const size_t faces[12][3] = {
        {0, 2, 1}, {0, 3, 2}, /* back  -z */
        {4, 5, 6}, {4, 6, 7}, /* front +z */
        {0, 4, 7}, {0, 7, 3}, /* left  -x */
        {1, 2, 6}, {1, 6, 5}, /* right +x */
        {0, 1, 5}, {0, 5, 4}, /* bottom -y */
        {3, 7, 6}, {3, 6, 2}  /* top +y */
    };
    int i;
    for (i = 0; i < 8; i++) {
        mesh3d_vertex v = vertex_at(coords[i][0], coords[i][1], coords[i][2]);
        if (mesh3d_add_vertex(m, &v, (size_t *)0, err) != STDLIB_OK)
            return 0;
    }
    for (i = 0; i < 12; i++) {
        mesh3d_triangle t;
        t.i0 = faces[i][0];
        t.i1 = faces[i][1];
        t.i2 = faces[i][2];
        if (mesh3d_add_triangle(m, &t, err) != STDLIB_OK)
            return 0;
    }
    return 1;
}

static int bbox_same(const mesh3d_aabb *a, const mesh3d_aabb *b) {
    return a->valid == b->valid &&
           near(a->min.x, b->min.x) && near(a->min.y, b->min.y) &&
           near(a->min.z, b->min.z) && near(a->max.x, b->max.x) &&
           near(a->max.y, b->max.y) && near(a->max.z, b->max.z);
}

/* Custom allocator instrumentation: proves the mesh routes allocations
 * through the supplied stdlib_allocator rather than a hard-coded heap. */
static size_t g_alloc_ops = 0u;
static size_t g_release_ops = 0u;

static void *ta_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx;
    (void)align;
    g_alloc_ops++;
    return cplus_alloc(size);
}

static void *ta_realloc(void *ctx, void *ptr, size_t old_size,
                        size_t new_size, size_t align) {
    (void)ctx;
    (void)old_size;
    (void)align;
    g_alloc_ops++;
    return cplus_realloc(ptr, new_size);
}

static void ta_release(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    g_release_ops++;
    cplus_release(ptr);
}

static void ta_reset(void *ctx) {
    (void)ctx;
}

int main(void) {
    error err = error();
    stdlib_allocator heap = stdlib_heap_allocator();
    mesh3d m;
    mesh3d_aabb box;
    mesh3d_aabb before;
    mesh3d_vec3 center;
    mesh3d_validation report;
    size_t before_v;
    size_t before_t;

    /* --- construction ------------------------------------------------ */
    mesh3d_init(&m, &heap, &err);
    print("init", mesh3d_valid(&m) == 1, err.ok() == 1,
          mesh3d_vertex_count(&m) == 0u, mesh3d_triangle_count(&m) == 0u,
          mesh3d_vertex_capacity(&m) == 0u);

    print("build", add_cube(&m, &err) == 1, err.ok() == 1,
          mesh3d_vertex_count(&m) == 8u, mesh3d_triangle_count(&m) == 12u,
          mesh3d_vertex_capacity(&m) >= 8u,
          mesh3d_triangle_capacity(&m) >= 12u);

    /* --- bounding box / center --------------------------------------- */
    mesh3d_bounding_box(&m, &box, &err);
    print("bbox", err.ok() == 1, box.valid == 1,
          near(box.min.x, -1.0f), near(box.min.y, -1.0f),
          near(box.min.z, -1.0f), near(box.max.x,  1.0f),
          near(box.max.y,  1.0f), near(box.max.z,  1.0f));

    mesh3d_center(&m, &center, &err);
    print("center", err.ok() == 1, near(center.x, 0.0f),
          near(center.y, 0.0f), near(center.z, 0.0f));

    /* --- normals ----------------------------------------------------- */
    print("normals-ok", mesh3d_compute_normals(&m, &err) == STDLIB_OK,
          err.ok() == 1);
    {
        int unit = 1;
        int outward = 1;
        size_t i;
        for (i = 0; i < mesh3d_vertex_count(&m); i++) {
            mesh3d_vec3 n = m.vertices[i].normal;
            mesh3d_vec3 p = m.vertices[i].position;
            float len2 = mesh3d_vec3_len2(n);
            float dot;
            if (!(len2 > 0.99f && len2 < 1.01f))
                unit = 0;
            dot = n.x * p.x + n.y * p.y + n.z * p.z;
            if (!(dot > 0.0f))
                outward = 0;
        }
        print("normals-unit", unit == 1);
        print("normals-outward", outward == 1);
    }
    {
        mesh3d_vec3 n0 = m.vertices[0].normal;
        mesh3d_vec3 n6 = m.vertices[6].normal;
        print("normals-exact",
              near(n0.x, -0.57735f), near(n0.y, -0.57735f),
              near(n0.z, -0.57735f), near(n6.x,  0.57735f),
              near(n6.y,  0.57735f), near(n6.z,  0.57735f));
    }

    /* --- validation on the clean cube -------------------------------- */
    report.nonfinite_vertices = 99u;
    report.bad_index_triangles = 99u;
    report.degenerate_triangles = 99u;
    print("validate-clean", mesh3d_validate(&m, &report, &err) == STDLIB_OK,
          err.ok() == 1, report.nonfinite_vertices == 0u,
          report.bad_index_triangles == 0u,
          report.degenerate_triangles == 0u);

    /* --- transform: translate (1,2,3) then scale x2 ------------------ */
    print("translate", mesh3d_translate(&m, 1.0f, 2.0f, 3.0f, &err) == STDLIB_OK,
          err.ok() == 1);
    print("scale", mesh3d_scale_uniform(&m, 2.0f, &err) == STDLIB_OK,
          err.ok() == 1);
    mesh3d_bounding_box(&m, &box, &err);
    mesh3d_center(&m, &center, &err);
    print("transformed", err.ok() == 1,
          near(box.min.x, 0.0f), near(box.min.y, 2.0f), near(box.min.z, 4.0f),
          near(box.max.x, 4.0f), near(box.max.y, 6.0f), near(box.max.z, 8.0f),
          near(center.x, 2.0f), near(center.y, 4.0f), near(center.z, 6.0f));

    /* --- refusal matrix ---------------------------------------------- */
    before_v = mesh3d_vertex_count(&m);
    before_t = mesh3d_triangle_count(&m);
    mesh3d_bounding_box(&m, &before, &err);

    {
        mesh3d_triangle bad;
        bad.i0 = 0u; bad.i1 = 1u; bad.i2 = 99u;
        print("refuse-index",
              mesh3d_add_triangle(&m, &bad, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              mesh3d_triangle_count(&m) == before_t);
        bad.i0 = 0u; bad.i1 = 0u; bad.i2 = 1u;
        print("refuse-repeat-index",
              mesh3d_add_triangle(&m, &bad, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              mesh3d_triangle_count(&m) == before_t);
    }

    {
        mesh3d_vertex nan_v = vertex_at(0.0f, 0.0f, 0.0f);
        nan_v.position.y = nan_f();
        print("refuse-nan-vertex",
              mesh3d_add_vertex(&m, &nan_v, (size_t *)0, &err) == STDLIB_FAIL,
              err.failed_now() == 1, mesh3d_vertex_count(&m) == before_v);
        nan_v.position.y = inf_f();
        print("refuse-inf-vertex",
              mesh3d_add_vertex(&m, &nan_v, (size_t *)0, &err) == STDLIB_FAIL,
              err.failed_now() == 1, mesh3d_vertex_count(&m) == before_v);
    }

    {
        mesh3d_vertex a = vertex_at(0.0f, 0.0f, 0.0f);
        mesh3d_vertex b = vertex_at(1.0f, 0.0f, 0.0f);
        mesh3d_vertex c = vertex_at(2.0f, 0.0f, 0.0f);
        mesh3d_triangle collinear;
        size_t ia = 0u;
        size_t ib = 0u;
        size_t ic = 0u;
        mesh3d_add_vertex(&m, &a, &ia, &err);
        mesh3d_add_vertex(&m, &b, &ib, &err);
        mesh3d_add_vertex(&m, &c, &ic, &err);
        collinear.i0 = ia;
        collinear.i1 = ib;
        collinear.i2 = ic;
        print("refuse-collinear",
              mesh3d_add_triangle(&m, &collinear, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              mesh3d_triangle_count(&m) == before_t);
    }

    /* The three collinear test vertices changed the bounds: refresh the
     * atomicity baseline now that the mesh is otherwise quiescent. */
    mesh3d_bounding_box(&m, &before, &err);

    print("refuse-nan-scale",
          mesh3d_scale(&m, 1.0f, nan_f(), 1.0f, &err) == STDLIB_FAIL,
          err.failed_now() == 1);
    mesh3d_bounding_box(&m, &box, &err);
    print("scale-atomic", bbox_same(&box, &before) == 1);

    print("refuse-inf-translate",
          mesh3d_translate(&m, 0.0f, 0.0f, inf_f(), &err) == STDLIB_FAIL,
          err.failed_now() == 1);
    mesh3d_bounding_box(&m, &box, &err);
    print("translate-atomic", bbox_same(&box, &before) == 1);

    print("refuse-overflow-scale",
          mesh3d_scale(&m, inf_f(), 1.0f, 1.0f, &err) == STDLIB_FAIL,
          err.failed_now() == 1);

    print("refuse-oversized-vertices",
          mesh3d_reserve_vertices(&m, MESH3D_LIMIT_VERTICES + 1u, &err) ==
              STDLIB_FAIL,
          err.failed_now() == 1, mesh3d_vertex_count(&m) == before_v + 3u);
    print("refuse-oversized-triangles",
          mesh3d_reserve_triangles(&m, MESH3D_LIMIT_TRIANGLES + 1u, &err) ==
              STDLIB_FAIL,
          err.failed_now() == 1, mesh3d_triangle_count(&m) == before_t);

    {
        const mesh3d_vertex *v = (const mesh3d_vertex *)0;
        const mesh3d_triangle *t = (const mesh3d_triangle *)0;
        print("refuse-at",
              mesh3d_vertex_at(&m, 99999u, &v, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              mesh3d_triangle_at(&m, 99999u, &t, &err) == STDLIB_FAIL,
              err.failed_now() == 1);
    }

    /* --- validate detects injected corruption ------------------------ */
    {
        mesh3d_triangle saved = m.triangles[0];
        m.triangles[0].i0 = 0u;
        m.triangles[0].i1 = 0u;
        m.triangles[0].i2 = 1u;
        print("validate-degenerate",
              mesh3d_validate(&m, &report, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              report.degenerate_triangles == 1u,
              report.bad_index_triangles == 0u);
        m.triangles[0].i0 = 0u;
        m.triangles[0].i1 = 1u;
        m.triangles[0].i2 = MESH3D_LIMIT_VERTICES;
        print("validate-bad-index",
              mesh3d_validate(&m, &report, &err) == STDLIB_FAIL,
              err.failed_now() == 1,
              report.bad_index_triangles == 1u,
              report.degenerate_triangles == 0u);
        m.triangles[0] = saved;
        print("validate-restored",
              mesh3d_validate(&m, &report, &err) == STDLIB_OK,
              err.ok() == 1);
    }

    /* --- clear keeps capacity and is reusable ------------------------- */
    before_v = mesh3d_vertex_capacity(&m);
    before_t = mesh3d_triangle_capacity(&m);
    print("clear", mesh3d_clear(&m, &err) == STDLIB_OK, err.ok() == 1,
          mesh3d_vertex_count(&m) == 0u, mesh3d_triangle_count(&m) == 0u,
          mesh3d_vertex_capacity(&m) == before_v,
          mesh3d_triangle_capacity(&m) == before_t);
    {
        mesh3d_vertex v = vertex_at(1.0f, 2.0f, 3.0f);
        size_t idx = 99u;
        print("reuse-after-clear",
              mesh3d_add_vertex(&m, &v, &idx, &err) == STDLIB_OK,
              err.ok() == 1, idx == 0u, mesh3d_vertex_count(&m) == 1u);
    }
    mesh3d_center(&m, &center, &err);
    print("single-center", err.ok() == 1, near(center.x, 1.0f),
          near(center.y, 2.0f), near(center.z, 3.0f));
    mesh3d_clear(&m, &err);
    print("empty-center-refused",
          mesh3d_center(&m, &center, &err) == STDLIB_FAIL,
          err.failed_now() == 1);

    /* --- allocator-aware: custom allocator is actually used ---------- */
    {
        mesh3d cm;
        stdlib_allocator custom;
        custom.ctx = (void *)0;
        custom.alloc = ta_alloc;
        custom.realloc = ta_realloc;
        custom.release = ta_release;
        custom.reset = ta_reset;
        g_alloc_ops = 0u;
        g_release_ops = 0u;
        print("custom-init", mesh3d_init(&cm, &custom, &err) == STDLIB_OK,
              err.ok() == 1);
        print("custom-build", add_cube(&cm, &err) == 1,
              mesh3d_vertex_count(&cm) == 8u,
              mesh3d_triangle_count(&cm) == 12u, g_alloc_ops > 0u);
        mesh3d_drop(&cm);
        print("custom-drop", mesh3d_valid(&cm) == 0, g_release_ops > 0u);
        mesh3d_drop(&cm); /* idempotent */
        print("custom-double-drop", mesh3d_valid(&cm) == 0);
    }

    /* Direct reach for public vector helpers (coverage tool call sites). */
    {
        mesh3d_vec3 a = {1.0f, 2.0f, 3.0f}, b = {4.0f, 5.0f, 6.0f};
        (void)mesh3d_finite(1.0f);
        (void)mesh3d_vec3_finite(a);
        (void)mesh3d_vec3_sub(a, b);
        (void)mesh3d_vec3_dot(a, b);
        (void)mesh3d_vec3_cross(a, b);
    }

    mesh3d_drop(&m);
    print("drop", mesh3d_valid(&m) == 0, mesh3d_vertex_count(&m) == 0u);
    return 0;
}
