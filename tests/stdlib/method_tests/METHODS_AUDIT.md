# Method-coverage audit: per-method suites vs. the authoritative coverage tool

Scope: reconcile the nine sibling per-method suites under
`tests/stdlib/method_tests/<group>/` against the repository's authoritative
API inventory, `sh tests/stdlib/run_coverage.sh`.

Everything below was measured on branch `agent/modern-stdlib`, macOS/arm64,
`CPLUS_CLANG=/usr/bin/clang`, using `./build/c+` (Apple clang 16). No source
file, header, or sibling-owned subdirectory was modified by this audit.

## 1. Authoritative inventory (exact command and result)

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/run_coverage.sh 2>&1; echo "rc=$?"
```

The tool builds `tests/stdlib/coverage.cp` and runs it from the repo root.
Exact final output:

```
library             covered    total percent
------------------ -------- -------- -------
allocator.hp              4        4  100.0%
async.hp                 14       14  100.0%
audio.hp                 29       29  100.0%
collections.hp           63       63  100.0%
error.hp                  9        9  100.0%
file.hp                  13       13  100.0%
fs.hp                     8        8  100.0%
graphics.hp              16       16  100.0%
gsl_adapt.hp             20       23   87.0%
gui.hp                   18       18  100.0%
http.hp                  19       19  100.0%
image.hp                 10       10  100.0%
input.hp                 10       10  100.0%
io.hp                     6        6  100.0%
json.hp                  32       32  100.0%
lifetime_adapt.hp         2        2  100.0%
log.hp                   14       14  100.0%
net.hp                   15       15  100.0%
string.hp                24       24  100.0%
thread.hp                21       21  100.0%
time.hp                   5        5  100.0%
vector.hp                23       23  100.0%
video.hp                 25       25  100.0%
window.hp                11       11  100.0%

all libraries >= 80% API coverage (worst 87.0%)
```

Exit code 0. Summing the columns with
`awk 'NR>2 && $1 ~ /\.hp$/ {t+=$3;c+=$2;n++} END{...}'` gives:

```
headers=24 covered=411 total=414
```

**Reconciling the "411" figure.** The task brief quotes *411 API entries across
24 headers*. The tool actually parses **414** entries across those 24 headers;
**411** is the number of those entries reached by the sources the tool scans
(the 3 unreached entries are listed in §3). This audit uses 414 as the correct
denominator and reports 411 as the tool's covered count.

## 2. How the tool counts (and what it cannot see)

`tests/stdlib/coverage.cp` is a *static call-site scan*, not execution coverage.
Relevant implementation facts (read from that file, not inferred):

- `main()` enumerates `find stdlib -maxdepth 1 -type f -name '*.hp'` → **26**
  headers exist, but `parse_header()`/`main()` skip any header with zero parsed
  entries. 24 headers survive (§1).
- `is_skipped()` drops control keywords, `main`, and **every name beginning with
  `stdlib_`**. This is why the two additive headers `stdlib/alloc_policy.hp`
  and `stdlib/memory.hp` contribute **zero** entries: all of their public
  helpers are named `stdlib_*` (4 in `alloc_policy.hp`, 17 in `memory.hp`).
  Those 21 public, callable, documented helpers are real API surface but are
  *outside* the 411/414 inventory.
- `gather_sources()` concatenates `.cp` files from exactly these locations:
  `tests/stdlib/` (maxdepth 1), `tests/stdlib/multi_tu/`,
  `tests/stdlib/examples/`, `tests/stdlib/upstream_reuse/`,
  `tests/stdlib/allocator/`, `tests/stdlib/system_safety/support/`.
  It does **not** descend into `tests/stdlib/method_tests/`. Consequently the
  authoritative tool cannot observe any call made by the new per-method suites;
  its 411/414 numbers are invariant to this work.
- A destructor `drop` is credited when the owning class name occurs as a word;
  `*_drop` is credited when the owner name occurs as a word. All other entries
  need a `name(` token. Class constructors are reported as `<Class>.<Class>`
  (e.g. `condvar.condvar`), not `<Class>.ctor`.

## 3. The three entries the authoritative tool does not reach

Compiling a temporary copy of `coverage.cp` with `THRESHOLD 100.0` (temporary
file under `/tmp`, repository untouched) prints the below-100% list:

```
BELOW 100%: gsl_adapt.hp (87.0%)
  not called by any test/example: gsl_adapt_refuse_null, gsl_adapt_refuse_range, gsl_adapt_refuse_bounds
```

These three are the entire 411-vs-414 difference. The sibling `net_http` suite
adds dedicated cases for them in `methods_gsl_adapt.cp`; see §5 for whether
those invocations were actually executed and verified.

## 4. Full 414-entry inventory (per header)

Obtained by compiling a temporary copy of `coverage.cp` with `THRESHOLD 200.0`
and `if (hit)` changed to `if (0)`, which forces every parsed entry into the
"not called" report. Counts match the `total` column of §1 exactly:

| header | entries | header | entries |
|---|---:|---|---:|
| allocator.hp | 4 | json.hp | 32 |
| async.hp | 14 | lifetime_adapt.hp | 2 |
| audio.hp | 29 | log.hp | 14 |
| collections.hp | 63 | net.hp | 15 |
| error.hp | 9 | string.hp | 24 |
| file.hp | 13 | thread.hp | 21 |
| fs.hp | 8 | time.hp | 5 |
| graphics.hp | 16 | vector.hp | 23 |
| gsl_adapt.hp | 23 | video.hp | 25 |
| gui.hp | 18 | window.hp | 11 |
| http.hp | 19 | | |
| image.hp | 10 | **TOTAL** | **414** |
| input.hp | 10 | | |
| io.hp | 6 | | |

## 5. Manifest reconciliation

*To be filled from the final real run of `run_all.sh` and the completed sibling
manifests; this section is the only part that depends on in-flight sibling
work.*
