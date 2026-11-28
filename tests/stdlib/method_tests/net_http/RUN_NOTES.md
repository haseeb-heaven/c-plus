# RUN NOTES — net/http + additive-adaptation 100% public-method tests

Branch `agent/modern-stdlib`. All work is confined to
`tests/stdlib/method_tests/net_http/`. No header or source file outside that
directory was edited.

## 1. What was enumerated

Every callable defined in the four headers was enumerated from the on-disk
source and given exactly one deterministic case that **calls it and verifies
the result**. A single `<header> <method> PASS|FAIL` line is emitted per method.

| Header | Public callables found | Source of count |
|---|---|---|
| `stdlib/net.hp` | 14 | 12 class methods + `listener_borrow` + `connection_borrow` |
| `stdlib/http.hp` | 22 | 18 class methods + `http_version` + `http_sink_write` + `http_client.request` |
| `stdlib/gsl_adapt.hp` | 29 | 3 refusal helpers + 11 narrow + 5 not_null + 10 span |
| `stdlib/lifetime_adapt.hp` | 8 | check + owner init/release + transfer + borrow/is_live/read/write |

The task brief's parenthetical counts come from the static inventory
`docs/stdlib/REGRESSION_INVENTORY.md` (net.hp 15, http.hp 19). That inventory
counts a phantom `listener.listen` for net.hp (a lexical false match on the
`listen(2)` call inside the constructor at `stdlib/net.hp:120`; there is no such
public method) and omits three real http.hp callables (`http_sink_write`,
`http_client.request`, `http_client.post`). See the tail of `methods.txt`.
The actual header source is authoritative here.

## 2. Build/run commands and exact results

All commands were run from the repository root. `timeout 30` applies to every
run. Binaries were written to `${TMPDIR:-/tmp}/cplus_method_tests_net_http/`.

### 2.1 Runner (all four suites)

```
$ tests/stdlib/method_tests/net_http/run_all.sh
...
METHOD_TEST net.hp 14 14 0 0
METHOD_TEST gsl_adapt.hp 29 29 0 0
METHOD_TEST lifetime_adapt.hp 8 8 0 0
METHOD_TEST http.hp 22 22 0 0
METHOD_TEST net_http 73 73 0 0
$ echo $?
0
```

### 2.2 net.hp — `--strict --sanitize=none`

```
$ ./build/c+ build tests/stdlib/method_tests/net_http/methods_net.cp --strict --sanitize=none -o /tmp/mt_net
$ echo $?
0
$ timeout 30 /tmp/mt_net
net.hp connection.connection PASS
net.hp connection.is_valid PASS
net.hp connection.descriptor PASS
net.hp connection.read PASS
net.hp connection.write PASS
net.hp connection.drop PASS
net.hp listener.listener PASS
net.hp listener.is_valid PASS
net.hp listener.descriptor PASS
net.hp listener.bound_port PASS
net.hp listener.accept PASS
net.hp listener.drop PASS
net.hp listener_borrow PASS
net.hp connection_borrow PASS
METHOD_TEST net.hp 14 14 0 0
$ echo $?
0
```

Offline evidence: connection cases use `socketpair(AF_UNIX, SOCK_STREAM, ...)`;
listener cases bind `127.0.0.1` port 0 and use a `fork()`ed loopback peer.
Ownership is verified with `fcntl(fd, F_GETFD)` after `drop()`, not assumed.
No external host, DNS, proxy or fixture file is used.

### 2.3 gsl_adapt.hp — `--strict --sanitize=none`

```
$ ./build/c+ build tests/stdlib/method_tests/net_http/methods_gsl_adapt.cp --strict --sanitize=none -o /tmp/mt_gsl
$ echo $?
0
$ timeout 30 /tmp/mt_gsl
gsl_adapt.hp gsl_adapt_refuse_null PASS
gsl_adapt.hp gsl_adapt_refuse_range PASS
gsl_adapt.hp gsl_adapt_refuse_bounds PASS
gsl_adapt.hp gsl_narrow_i64_i8 PASS
gsl_adapt.hp gsl_narrow_i64_u8 PASS
gsl_adapt.hp gsl_narrow_i64_i16 PASS
gsl_adapt.hp gsl_narrow_i64_u16 PASS
gsl_adapt.hp gsl_narrow_i64_i32 PASS
gsl_adapt.hp gsl_narrow_i64_u32 PASS
gsl_adapt.hp gsl_narrow_i64_i64 PASS
gsl_adapt.hp gsl_narrow_i64_u64 PASS
gsl_adapt.hp gsl_narrow_u64_i64 PASS
gsl_adapt.hp gsl_narrow_u64_u8 PASS
gsl_adapt.hp gsl_narrow_u64_u32 PASS
gsl_adapt.hp gsl_not_null_i32_make PASS
gsl_adapt.hp gsl_not_null_i32_get PASS
gsl_adapt.hp gsl_not_null_i32_swap PASS
gsl_adapt.hp gsl_not_null_void_make PASS
gsl_adapt.hp gsl_not_null_void_get PASS
gsl_adapt.hp gsl_span_i32_view PASS
gsl_adapt.hp gsl_span_i32_make PASS
gsl_adapt.hp gsl_span_i32_size PASS
gsl_adapt.hp gsl_span_i32_size_bytes PASS
gsl_adapt.hp gsl_span_i32_empty PASS
gsl_adapt.hp gsl_span_i32_data PASS
gsl_adapt.hp gsl_span_i32_at PASS
gsl_adapt.hp gsl_span_i32_first PASS
gsl_adapt.hp gsl_span_i32_last PASS
gsl_adapt.hp gsl_span_i32_subspan PASS
METHOD_TEST gsl_adapt.hp 29 29 0 0
$ echo $?
0
```

### 2.4 lifetime_adapt.hp — `--strict --sanitize=none`

```
$ ./build/c+ build tests/stdlib/method_tests/net_http/methods_lifetime_adapt.cp --strict --sanitize=none -o /tmp/mt_life
$ echo $?
0
$ timeout 30 /tmp/mt_life
lifetime_adapt.hp lifetime_owner_init PASS
lifetime_adapt.hp lifetime_check PASS
lifetime_adapt.hp lifetime_borrow_from PASS
lifetime_adapt.hp lifetime_borrow_is_live PASS
lifetime_adapt.hp lifetime_write PASS
lifetime_adapt.hp lifetime_read PASS
lifetime_adapt.hp lifetime_owner_release PASS
lifetime_adapt.hp lifetime_transfer PASS
METHOD_TEST lifetime_adapt.hp 8 8 0 0
$ echo $?
0
```

Stale-token refusals are checked without ever dereferencing the stale pointer;
results come from the metadata table (`stdlib_alloc_generation`).

### 2.5 http.hp — required `--strict` attempt FAILED, documented deviation

The exact requested command was run first:

```
$ ./build/c+ build tests/stdlib/http_test.cp --strict --sanitize=none -o /tmp/strict_probe
./stdlib/http.hp:106:30: error: [CPLUS-ARITH] division by zero cannot be excluded
      }
$ echo $?
1
```

The same error occurs for the new test file (the diagnostic is emitted while
lowering the included header, before the test body is lowered). The offending
source is `stdlib/http.hp:106`:

```c
    if (size != 0 && nmemb > SIZE_MAX / size) {
```

The guard `size != 0 &&` is present in the header, but the strict checker
cannot exclude the divisor being zero. Per the task's file-ownership rule the
header must not be edited, so this is a header-level false positive and not a
failure of any method under test. The repository's own harness builds http.hp
the same way — `tests/stdlib/run_stdlib_tests.sh:135-137` adds `pkg-config
--libs libcurl` and line 169 invokes the C+ driver **without** `--strict`.

Therefore the http suite is built in the default checker mode with libcurl:

```
$ ./build/c+ build tests/stdlib/method_tests/net_http/methods_http.cp --default --sanitize=none -lcurl -o /tmp/mt_http
./stdlib/http.hp:106:30: warning: [CPLUS-ARITH] division by zero cannot be excluded
      }
clang: warning: -lcurl: 'linker' input unused [-Wunused-command-line-argument]
$ echo $?
0
$ timeout 30 /tmp/mt_http
http.hp http.http PASS
http.hp http.valid PASS
http.hp http_version PASS
http.hp http_headers.http_headers PASS
http.hp http_headers.valid PASS
http.hp http_headers.native PASS
http.hp http_headers.add PASS
http.hp http_headers.drop PASS
http.hp http_sink_write PASS
http.hp http_client.http_client PASS
http.hp http_client.valid PASS
http.hp http_client.set_timeout PASS
http.hp http_client.follow_redirects PASS
http.hp http_client.header_lines PASS
http.hp http_client.set_headers PASS
http.hp http_client.request PASS
http.hp http_client.get PASS
http.hp http_client.post PASS
http.hp http_client.status PASS
http.hp http_client.bytes PASS
http.hp http_client.drop PASS
http.hp http.drop PASS
METHOD_TEST http.hp 22 22 0 0
$ echo $?
0
```

The `-lcurl` "linker input unused" clang warning is benign: `nm -u` on the
resulting binary shows the curl symbols (e.g. `_curl_easy_perform`), and the
binary runs successfully. Offline evidence: the three transfers target a
`fork()`ed 127.0.0.1 echo server on an ephemeral port; all other http methods
exercise in-process libcurl handle/slist/getinfo paths and the header's own
`http_sink_write` buffer. No external network host is contacted.

## 3. Additional observations

* During development the net test initially failed to compile because the C+
  lowering resolves same-named methods by *variable name*; reusing the name
  `borrowed` for both a `connection` and a `listener` produced a call to
  `connection_descriptor(&listener)`. Renaming the listener local fixed it.
  This is a compiler-lowering quirk, not a header defect, and the final source
  uses unique variable names per class.
* `lifetime_transfer(&dst, &moved_from_src, ...)` is refused by
  `lifetime_check` with `STDLIB_CATEGORY_LIFECYCLE` (not `OWNERSHIP`); the test
  asserts the actual header behavior.
* All tests are self-checking, use no stdin, no wall-clock dependency, and
  bound every run with `timeout 30`.

## 4. Totals

| Header | Total | Passed | Failed | Skipped |
|---|---:|---:|---:|---:|
| `net.hp` | 14 | 14 | 0 | 0 |
| `http.hp` | 22 | 22 | 0 | 0 |
| `gsl_adapt.hp` | 29 | 29 | 0 | 0 |
| `lifetime_adapt.hp` | 8 | 8 | 0 | 0 |
| **All** | **73** | **73** | **0** | **0** |

No method was skipped: every callable in these headers can be exercised
offline in this environment (loopback sockets and libcurl's file/buffer paths
are available). The driver exits 0.

METHOD_TEST net_http 73 73 0 0
