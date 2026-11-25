# Network/IO utility examples — verification notes

## Scope and honesty statement

**These five programs are offline validators, not network clients.** None of
them opens a socket, resolves a hostname, sends or receives a packet, reads a
user-specific path, or contacts any host. They parse fixed strings supplied on
the command line plus a built-in suite of fixed hostile strings. The words
"URL", "email", "IP", "port" and "HTTP" describe the *grammar being validated*,
not any live I/O.

**Refusals are runtime evidence, not a safety proof.** A `REFUSED` line shows
that, for that exact input on this run, an explicit bounds/format check in the
C+ code (or a checked `stdlib` API) returned a stable refusal code. There is no
formal proof, no model checking, no fuzzing campaign and no sanitizer involved:
all builds use `--sanitize=none`. Passing these fixed cases must not be read as
a guarantee about untested inputs. The built-in hostile cases have expected
codes asserted at runtime; a wrong code or an unexpected acceptance increments
the `failed` field of the summary and makes the process exit 1.

## Files (all under `apps/legacy_examples/utilities/net_tools/`)

| file | purpose |
| --- | --- |
| `urlcheck.cp` | validates `scheme://host[:port][/path]`, reports parsed components |
| `emailcheck.cp` | validates `local@domain` with bounded local/domain/label rules |
| `ipcheck.cp` | validates IPv4 dotted quads, refuses octets > 255 and bad shapes |
| `portcheck.cp` | overflow-safe parse of ports 0..65535 |
| `httpline.cp` | validates an HTTP request line `METHOD path VERSION` |
| `run.sh` | builds all five, runs fixed inputs twice, prints `UTIL_NET ...` |
| `results/` | generated binaries, build logs, stdout/stderr captures (not source) |

Every tool uses `stdlib/string.hp` (owned `string`, `find`, `substr`, `field`,
`count_fields`, `at`) and `stdlib/error.hp` (`error`, stable categories,
`stdlib_category_name`, `stdlib_alloc_fail_reset`). All lengths are bounded
before inspection; no raw game/network offset and no unchecked pointer
arithmetic on FFI data is used.

## Exact build commands and result

Run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch
`agent/modern-stdlib`. `results/` was created before building.

```sh
./build/c+ build apps/legacy_examples/utilities/net_tools/urlcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/urlcheck
./build/c+ build apps/legacy_examples/utilities/net_tools/emailcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/emailcheck
./build/c+ build apps/legacy_examples/utilities/net_tools/ipcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/ipcheck
./build/c+ build apps/legacy_examples/utilities/net_tools/portcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/portcheck
./build/c+ build apps/legacy_examples/utilities/net_tools/httpline.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/httpline
```

All five builds produced empty stdout/stderr and exit code 0. (An initial
`--strict` build of `urlcheck.cp` failed with `CPLUS-LIFETIME: variable
'host_len' is uninitialized`; the checker caught a real missing assignment in
the no-port branch, which was fixed before the successful build above.)

## Exact per-tool commands and outputs

### urlcheck

```sh
timeout 30 ./apps/legacy_examples/utilities/net_tools/results/urlcheck https://good.example:443/x ftp://bad/
```

```
URL v_http VALID http example.com 80 / default
URL v_https_port VALID https example.com 8443 /a/b explicit
URL v_ip_port VALID http 127.0.0.1 8080 / explicit
URL h_bad_scheme REFUSED 108 invalid bad-scheme
URL h_empty_auth REFUSED 108 invalid empty-authority
URL h_no_scheme REFUSED 108 invalid no-scheme
URL h_space_host REFUSED 108 invalid bad-host-char
URL h_big_port REFUSED 101 bounds port-overflow
URL h_alpha_port REFUSED 108 invalid bad-port
URL h_empty_port REFUSED 108 invalid empty-port
URL h_bad_edge REFUSED 108 invalid bad-host-edge
URL h_traversal REFUSED 108 invalid dot-dot-path
URL h_oversized REFUSED 101 bounds too-long
URL argv VALID https good.example 443 /x explicit
URL argv REFUSED 108 invalid bad-scheme
URL_SUMMARY 15 4 11 0
```

Exit 0, stderr empty. Hostile cases refused include a bad scheme, empty
authority, a space in the host, a port of 99999, a non-numeric port, an empty
port, a leading-dot host, a `..` path and an oversized input.

### emailcheck

```sh
timeout 30 ./apps/legacy_examples/utilities/net_tools/results/emailcheck good@site.org bad@@x.com
```

```
EM v_simple VALID alice example.com
EM v_plus_tag VALID a.b+tag sub.example.co.uk
EM v_subdomain VALID x y.io
EM h_no_at REFUSED 108 invalid no-at
EM h_double_at REFUSED 108 invalid multiple-at
EM h_empty_local REFUSED 108 invalid empty-local
EM h_empty_dom REFUSED 108 invalid empty-domain
EM h_dotdot REFUSED 108 invalid consecutive-dot
EM h_leading_dot REFUSED 108 invalid bad-local-edge
EM h_no_dot REFUSED 108 invalid no-dot
EM h_short_tld REFUSED 108 invalid short-tld
EM h_bad_label REFUSED 108 invalid bad-label-edge
EM h_long_local REFUSED 101 bounds local-too-long
EM h_long_total REFUSED 101 bounds too-long
EM argv VALID good site.org
EM argv REFUSED 108 invalid multiple-at
EM_SUMMARY 16 4 12 0
```

Exit 0, stderr empty. Hostile cases include missing/duplicate `@`, empty
local/domain, consecutive dots, a dot-led local, a domain without a dot, a
one-character TLD, a dash-led label, an over-long local and an over-long total.

### ipcheck

```sh
timeout 30 ./apps/legacy_examples/utilities/net_tools/results/ipcheck 10.0.0.1 256.1.1.1
```

```
IP v_max VALID 255 255 255 255
IP v_zero VALID 0 0 0 0
IP v_loopback VALID 127 0 0 1
IP h_256 REFUSED 101 bounds octet-overflow
IP h_3fields REFUSED 108 invalid wrong-field-count
IP h_5fields REFUSED 108 invalid wrong-field-count
IP h_empty_oct REFUSED 108 invalid empty-octet
IP h_leading_zero REFUSED 108 invalid leading-zero
IP h_999 REFUSED 101 bounds octet-overflow
IP h_long REFUSED 101 bounds too-long
IP h_alpha REFUSED 108 invalid non-digit
IP h_negative REFUSED 108 invalid non-digit
IP h_empty REFUSED 108 invalid empty
IP argv VALID 10 0 0 1
IP argv REFUSED 101 bounds octet-overflow
IP_SUMMARY 15 4 11 0
```

Exit 0, stderr empty. Octet accumulation refuses before the multiply can pass
255; no C division or `inet_*` call is used.

### portcheck

```sh
timeout 30 ./apps/legacy_examples/utilities/net_tools/results/portcheck 80 65536
```

```
PORT v_min VALID 0
PORT v_max VALID 65535
PORT v_https VALID 443
PORT h_negative REFUSED 108 invalid non-digit
PORT h_too_big REFUSED 101 bounds overflow
PORT h_huge REFUSED 101 bounds too-long
PORT h_not_num REFUSED 108 invalid non-digit
PORT h_trailing REFUSED 108 invalid non-digit
PORT h_empty REFUSED 108 invalid empty
PORT h_plus REFUSED 108 invalid non-digit
PORT h_space REFUSED 108 invalid non-digit
PORT h_leading_zero REFUSED 108 invalid leading-zero
PORT argv VALID 80
PORT argv REFUSED 101 bounds overflow
PORT_SUMMARY 14 4 10 0
```

Exit 0, stderr empty. Overflow safety: a running value above 6553 is refused
before `*10 + digit`, so `65536` and a 20-digit number are both refused without
executing signed/unsigned overflow.

### httpline

```sh
timeout 30 ./apps/legacy_examples/utilities/net_tools/results/httpline 'GET /a HTTP/1.1' 'BREW /x HTTP/1.1'
```

```
HTTP v_get VALID GET /index.html HTTP/1.1
HTTP v_post VALID POST /submit HTTP/1.0
HTTP v_head VALID HEAD / HTTP/1.1
HTTP h_bad_method REFUSED 108 invalid bad-method
HTTP h_bad_ver REFUSED 108 invalid bad-version
HTTP h_two_fields REFUSED 108 invalid wrong-field-count
HTTP h_four_fields REFUSED 108 invalid wrong-field-count
HTTP h_double_space REFUSED 108 invalid wrong-field-count
HTTP h_missing_path REFUSED 108 invalid empty-field
HTTP h_relative REFUSED 108 invalid path-not-absolute
HTTP h_traversal REFUSED 108 invalid dot-dot-path
HTTP h_trailing_space REFUSED 108 invalid wrong-field-count
HTTP h_oversized REFUSED 101 bounds too-long
HTTP argv VALID GET /a HTTP/1.1
HTTP argv REFUSED 108 invalid bad-method
HTTP_SUMMARY 15 4 11 0
```

Exit 0, stderr empty. Only GET/POST/PUT/DELETE/HEAD/OPTIONS/PATCH and
HTTP/1.0 or HTTP/1.1 are accepted; the path must be absolute and printable.

## Aggregate runner — exact command and output

```sh
sh apps/legacy_examples/utilities/net_tools/run.sh
```

The runner rebuilds each tool, runs it twice under `timeout 30` with the fixed
argv shown above, compares the two stdout files with `cmp` (byte-identical),
verifies empty stderr on both runs and exit 0, requires the exact expected
summary line, and sums the refused field (4th field) of each summary.

Observed stdout:

```
=== BUILD urlcheck ===
=== RUN urlcheck ===
URL v_http VALID http example.com 80 / default
URL v_https_port VALID https example.com 8443 /a/b explicit
URL v_ip_port VALID http 127.0.0.1 8080 / explicit
URL h_bad_scheme REFUSED 108 invalid bad-scheme
URL h_empty_auth REFUSED 108 invalid empty-authority
URL h_no_scheme REFUSED 108 invalid no-scheme
URL h_space_host REFUSED 108 invalid bad-host-char
URL h_big_port REFUSED 101 bounds port-overflow
URL h_alpha_port REFUSED 108 invalid bad-port
URL h_empty_port REFUSED 108 invalid empty-port
URL h_bad_edge REFUSED 108 invalid bad-host-edge
URL h_traversal REFUSED 108 invalid dot-dot-path
URL h_oversized REFUSED 101 bounds too-long
URL argv VALID https good.example 443 /x explicit
URL argv REFUSED 108 invalid bad-scheme
URL_SUMMARY 15 4 11 0
=== BUILD emailcheck ===
=== RUN emailcheck ===
EM v_simple VALID alice example.com
EM v_plus_tag VALID a.b+tag sub.example.co.uk
EM v_subdomain VALID x y.io
EM h_no_at REFUSED 108 invalid no-at
EM h_double_at REFUSED 108 invalid multiple-at
EM h_empty_local REFUSED 108 invalid empty-local
EM h_empty_dom REFUSED 108 invalid empty-domain
EM h_dotdot REFUSED 108 invalid consecutive-dot
EM h_leading_dot REFUSED 108 invalid bad-local-edge
EM h_no_dot REFUSED 108 invalid no-dot
EM h_short_tld REFUSED 108 invalid short-tld
EM h_bad_label REFUSED 108 invalid bad-label-edge
EM h_long_local REFUSED 101 bounds local-too-long
EM h_long_total REFUSED 101 bounds too-long
EM argv VALID good site.org
EM argv REFUSED 108 invalid multiple-at
EM_SUMMARY 16 4 12 0
=== BUILD ipcheck ===
=== RUN ipcheck ===
IP v_max VALID 255 255 255 255
IP v_zero VALID 0 0 0 0
IP v_loopback VALID 127 0 0 1
IP h_256 REFUSED 101 bounds octet-overflow
IP h_3fields REFUSED 108 invalid wrong-field-count
IP h_5fields REFUSED 108 invalid wrong-field-count
IP h_empty_oct REFUSED 108 invalid empty-octet
IP h_leading_zero REFUSED 108 invalid leading-zero
IP h_999 REFUSED 101 bounds octet-overflow
IP h_long REFUSED 101 bounds too-long
IP h_alpha REFUSED 108 invalid non-digit
IP h_negative REFUSED 108 invalid non-digit
IP h_empty REFUSED 108 invalid empty
IP argv VALID 10 0 0 1
IP argv REFUSED 101 bounds octet-overflow
IP_SUMMARY 15 4 11 0
=== BUILD portcheck ===
=== RUN portcheck ===
PORT v_min VALID 0
PORT v_max VALID 65535
PORT v_https VALID 443
PORT h_negative REFUSED 108 invalid non-digit
PORT h_too_big REFUSED 101 bounds overflow
PORT h_huge REFUSED 101 bounds too-long
PORT h_not_num REFUSED 108 invalid non-digit
PORT h_trailing REFUSED 108 invalid non-digit
PORT h_empty REFUSED 108 invalid empty
PORT h_plus REFUSED 108 invalid non-digit
PORT h_space REFUSED 108 invalid non-digit
PORT h_leading_zero REFUSED 108 invalid leading-zero
PORT argv VALID 80
PORT argv REFUSED 101 bounds overflow
PORT_SUMMARY 14 4 10 0
=== BUILD httpline ===
=== RUN httpline ===
HTTP v_get VALID GET /index.html HTTP/1.1
HTTP v_post VALID POST /submit HTTP/1.0
HTTP v_head VALID HEAD / HTTP/1.1
HTTP h_bad_method REFUSED 108 invalid bad-method
HTTP h_bad_ver REFUSED 108 invalid bad-version
HTTP h_two_fields REFUSED 108 invalid wrong-field-count
HTTP h_four_fields REFUSED 108 invalid wrong-field-count
HTTP h_double_space REFUSED 108 invalid wrong-field-count
HTTP h_missing_path REFUSED 108 invalid empty-field
HTTP h_relative REFUSED 108 invalid path-not-absolute
HTTP h_traversal REFUSED 108 invalid dot-dot-path
HTTP h_trailing_space REFUSED 108 invalid wrong-field-count
HTTP h_oversized REFUSED 101 bounds too-long
HTTP argv VALID GET /a HTTP/1.1
HTTP argv REFUSED 108 invalid bad-method
HTTP_SUMMARY 15 4 11 0
UTIL_NET 5 5 0 55
```

Harness exit code 0. `55` is the sum of the refused fields
(11 + 12 + 11 + 10 + 11). No time-out was counted as a pass.

## Strict-mode findings reflected in the code

* `argv[i]` directly is rejected by `--strict` (`CPLUS-FFI: unverifiable
  extent`); every tool copies the slot with
  `__builtin_memcpy(&v, argv + i, sizeof(v))` before reading it.
* The strict checker rejects variable-divisor `/`. No tool divides by a
  computed value; port and octet parsing use an overflow-safe
  "refuse if `v > limit/10`-equivalent, then `v = v*10 + digit`" pattern with
  precomputed integer thresholds (6553 for ports, 25 for octets), so no
  division or overflow is executed.
* `string::find` returns a signed index; tools check `< 0` before casting to
  `size_t`. `substr` / `field` into fixed local buffers are checked and their
  return codes honoured; buffer sizes exceed the documented maxima so no
  truncation is relied upon for a valid case.
* `error` records are created per refusal so the stable category name and
  message can be printed; codes are `101` (`CPLUS_EXIT_BOUNDS`, range/length/
  overflow) and `108` (`CPLUS_EXIT_UNSUPPORTED`, malformed grammar).
* The `--strict` lifetime checker caught one genuine uninitialized variable in
  `urlcheck.cp` during development (see above); it was fixed, not suppressed.
