# math3d.hp — implementation & verification results

## Files (strict ownership)

- `stdlib/math3d.hp` — new additive C-style header (plain structs + `static inline`
  functions; no classes/templates, no exported symbols, no ABI change).
- `tests/stdlib/math3d/test_math3d.cp` — self-checking suite (36 checks).
- `tests/stdlib/math3d/run.sh` — bounded runner (build + run under `timeout 30`,
  exact stdout fixture, empty-stderr and exit-code checks).
- `tests/stdlib/math3d/RESULTS.md` — this report.

No other files were created or modified. No `git add`/`git commit`. No compiler or
runtime edits.

## Design

- Types: `vec3 {float x,y,z}`, `mat4 {float m[16]}`, `quat {float x,y,z,w}`.
- Matrices are **column-major**: `m[col*4 + row]`; translation in `m[12..14]`.
- All operations are pure (`static inline`, no mutable static state) → deterministic.
- Every operation returns `int`: `MATH3D_OK` (0), `MATH3D_ERR_NONFINITE` (-1),
  `MATH3D_ERR_ZERO` (-2), `MATH3D_ERR_NULL` (-3). Outputs are written through pointers; on refusal they
  are set to a safe deterministic value (zero vec3/quat/scalar, identity mat4).
- `<math.h>` is deliberately **not** included (its inline union macros trip the
  C+ lifetime checker); `sqrtf/sinf/cosf/atan2f/fabsf` are declared `extern` directly.
- Finite check: `x != x` rejects NaN; magnitude beyond `FLT_MAX` rejects ±Inf.
- Division-by-zero guards cover zero-length vectors/axes, `aspect == 0`,
  `near == far`, `eye == center`, and view/up parallelism.

## Operations provided

- vec3: `vec3_add`, `vec3_sub`, `vec3_scale`, `vec3_dot`, `vec3_cross`,
  `vec3_length`, `vec3_normalize` (+ `vec3_finite`, `vec3_zero`).
- mat4: `mat4_identity`, `mat4_multiply`, `mat4_translate`, `mat4_rotate`,
  `mat4_scale`, `mat4_perspective`, `mat4_look_at` (+ `mat4_finite`).
- quat: `quat_from_axis_angle`, `quat_multiply`, `quat_normalize`,
  `quat_to_mat4` (+ `quat_finite`, `quat_zero`).

## Build & run command

```sh
./build/c+ build tests/stdlib/math3d/test_math3d.cp \
  --default --sanitize=none -I. -o <bin>      # under timeout 30
timeout 30 <bin>
sh tests/stdlib/math3d/run.sh                 # full bounded verification
```

## Exact results

Build exit code: **0** (no diagnostics).
Run exit code: **0**.
stderr: **empty**.
stdout: **37 lines** (36 `<tag> <1|0>` lines + summary), SHA-256
`265f00932e93872623ef33f98385983f2c5d46cb3b17eb07d7bef9b812f699ea`.

Exact stdout:

```
t_finite_ok 1
t_finite_nan 1
t_finite_inf 1
t_v_add 1
t_v_sub 1
t_v_scale 1
t_v_dot 1
t_v_cross 1
t_v_length 1
t_v_normalize 1
t_m_identity 1
t_m_multiply_id 1
t_m_translate 1
t_m_scale 1
t_m_rotate 1
t_m_perspective 1
t_m_look_at 1
t_q_mul_identity 1
t_q_normalize 1
t_q_from_axis 1
t_q_roundtrip 1
t_refuse_v_add 1
t_refuse_v_scale_inf 1
t_refuse_v_dot 1
t_refuse_m_multiply 1
t_refuse_q_nonfinite 1
t_refuse_perspective 1
t_zero_v_normalize 1
t_zero_m_rotate_axis 1
t_zero_persp_aspect 1
t_zero_persp_nearfar 1
t_zero_lookat_eye 1
t_zero_lookat_up 1
t_zero_q_axis 1
t_zero_q_normalize 1
t_zero_q_to_mat4 1
MATH3D 36 0
```

Runner output (exit 0):

```
ok math3d: MATH3D 36 0
math3d: PASS (build rc=0, run rc=0, exact stdout, empty stderr)
```

## Coverage mapping

- **identity** — `t_m_identity` (diagonal 1, off-diagonal 0).
- **rotation matrix correctness** — `t_m_rotate` (90° about +Z yields the
  expected column-major `R_z`: `m[0]≈0, m[1]=1, m[4]=-1, m[5]≈0, m[10]=1`).
- **projection** — `t_m_perspective` (90° fovy, aspect 1, near 1, far 100 →
  `m[0]≈1, m[5]≈1, m[11]=-1, m[15]=0`, negative `m[10]`/`m[14]`).
- **quaternion round-trip** — `t_q_from_axis`, `t_q_roundtrip` (quat 90° about +Z
  → `quat_to_mat4` matches `mat4_rotate` element-wise within `1e-4`).
- **NaN/Inf refusal** — `t_finite_nan`, `t_finite_inf`, `t_refuse_v_add`,
  `t_refuse_v_scale_inf`, `t_refuse_v_dot`, `t_refuse_m_multiply`,
  `t_refuse_q_nonfinite`, `t_refuse_perspective` (each returns
  `MATH3D_ERR_NONFINITE` and writes the documented safe fallback).
- **division-by-zero guards** — `t_zero_v_normalize`, `t_zero_m_rotate_axis`,
  `t_zero_persp_aspect`, `t_zero_persp_nearfar`, `t_zero_lookat_eye`,
  `t_zero_lookat_up`, `t_zero_q_axis`, `t_zero_q_normalize`, `t_zero_q_to_mat4`
  (each returns `MATH3D_ERR_ZERO` with safe fallback).

## Notes / limitations

- Float comparisons in tests use absolute tolerance `1e-5` (`1e-4` for the
  quat↔matrix round-trip) where trig values are involved; exact equality is used
  for integer-valued results and structural matrix entries.
- `atan2f`/`fabsf` are declared for API completeness but are not required by the
  current operation set; they are not called by the header.
- Only the mandated `--default --sanitize=none` mode was verified (that is the
  task's specified build command).
