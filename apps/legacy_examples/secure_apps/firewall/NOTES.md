# Secure input-sanitizer "firewall" (`examples/secure_apps/firewall`)

## Teaching model — NOT a security proof

`firewall.cp` is a **deterministic teaching model of an allow-list input
firewall, not a security product and not a proof of safety.** It contains no
real parser, no real regex, no filesystem access, no SQL, no network, no
shell, no wall clock, no randomness and no interactive input. Every run is a
pure function of its two argv tokens and a fixed set of rules.

The "checked layer refuses" result is **observed runtime evidence for the 11
listed hostile tokens on this toolchain**, not a guarantee:

- refusals are ordinary C+ conditionals plus checked stdlib operations
  (`stdlib/string.hp::at/find/substr`, `stdlib/error.hp::set_category`); they
  are not a proof engine, model checker, fuzzer or formally verified contract;
- passing this harness does **not** establish absence of vulnerabilities for
  arbitrary inputs, nor any claim that a real firewall, WAF, SQL layer or OS
  path resolver is safe;
- the "unchecked path" is a **documented contrast computed at runtime**, not
  an actual exploit. It derives the consequence of a naive `strcpy`/format/
  concatenate from the *actual* bounded input of the same run. It does not
  execute an overflow, spawn a subprocess, or corrupt memory.

No `git add`, commit, or edit to any file outside
`examples/secure_apps/firewall/` was made by this work.

## Threat model

The protected resource is a hypothetical downstream consumer that trusts the
firewall's output: a fixed-size C buffer, a path join under `/srv/app/data`,
an SQL string literal, and a terminal/log line. The attacker controls exactly
one argv token. The model assumes:

1. The token is delivered as a NUL-terminated C string (argv), so an actual
   embedded `0x00` cannot be represented. **NUL injection is modelled with
   the textual markers `%00`, `\0` and `\x00`**, the usual textual stand-ins
   for a null-byte attack. The CONTRAST line shows the prefix an OS C-string
   API would see if the marker were a real NUL.
2. The firewall is a single fixed validation gate; no state is carried
   between runs, so bypasses via encoding layering, Unicode normalisation,
   double decoding, TOCTOU or canonicalisation drift are out of scope.
3. Size is the first structural defence: inputs longer than **256 bytes** are
   refused before any deeper processing.

Policies select the allow-list/pattern for the token:

| policy | accepts |
| --- | --- |
| `token` | exactly one of `alpha`, `beta`, `gamma`, `report`, `status` |
| `pathseg` | a single path segment: ASCII alnum, `_`, `-`, `.` (no separators) |
| `sqlsafe` | ASCII alnum, `_` and interior spaces only |
| `text` | printable ASCII `0x20..0x7E` |

### Ordered checks (first fired rule wins, so output is deterministic)

| step | rule | code | meaning |
| --- | --- | --- | --- |
| 0 | `storage` | `E_MEMORY` | checked `string` could not be created |
| 1 | `no-empty` | `E_EMPTY` | zero-length input |
| 2 | `max-len` | `E_SIZE` | length > 256 |
| 3 | `no-nul` | `E_NUL` | `%00`, `\0`, `\x00`, `%u0000` |
| 4 | `no-control` | `E_CONTROL` | byte < `0x20` or `0x7F` |
| 5 | `no-traversal` | `E_TRAVERSAL` | `../`, `..\`, `/..`, `\..`, `..` |
| 6 | `no-sqli` | `E_SQLI` | `' OR 1=1`, `'`, `"`, `;`, `--`, `/*`, `*/`, `union select`, `drop table` (case-folded where applicable) |
| 7 | `allowlist` / `pattern` / `usage` | `E_NOTALLOWED` / `E_POLICY` | per-policy character/token rule |

All scans are bounded by the constant `FW_SCAN_CAP = 512`; the input is copied
into a fixed local buffer with a sized builtin copy before any checked-string
operation. Loops run at most 512 iterations. There are no variable-length
allocations driven by input length.

## Runtime evidence for the unchecked path

Every run prints two kinds of `CONTRAST` line before the verdict:

```
CONTRAST policy=<p> naive=strcpy32 writes=<n+1> buffer=32 overflow=<n+1-32>
CONTRAST flavor=<...> unchecked=accept|corrupts ...
```

`writes` and `overflow` are computed from the real bounded length of the
current token. For the 300-byte input, the naive `char buf[32]; strcpy(buf,
input);` path would write 301 bytes into 32 → **269 bytes of overrun**, while
the firewall refuses with `REFUSED max-len E_SIZE`. For the traversal token,
the naive path join produces `/srv/app/data/../etc/passwd`; for the SQL token
it produces `SELECT * FROM users WHERE name='' OR 1=1'`. Both are built at
runtime with bounded `snprintf` and printed, then the firewall refuses. This
is the "documented contrast" half of the demonstration.

## Exact commands (from the repository root)

Build:

```
mkdir -p examples/secure_apps/firewall/bin
timeout 30 ./build/c+ build examples/secure_apps/firewall/firewall.cp --strict --sanitize=none -I. -o examples/secure_apps/firewall/bin/firewall
```

Build exit code: `0`, with **zero bytes** on stdout and stderr.

Harness (builds again, then runs all 19 fixed scenarios, each under
`timeout 30`, and compares every scenario against a second identical run):

```
timeout 120 sh examples/secure_apps/firewall/run.sh
```

Exit code: `0`. Final line, exactly:

```
SECURE_FIREWALL 19 8 0 11
```

Meaning: 19 scenarios = 8 accepted + 11 hostile; 8 accepted passed, 11 hostile
correctly refused, 0 failures. Every scenario also printed a `CONTRAST` line
and produced byte-identical stdout on its second run.

## Exact run outputs (representative)

Accepted — leading/trailing spaces are trimmed by the checked `substr()`:

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall text '  padded  '
CONTRAST policy=text naive=strcpy32 writes=11 buffer=32 overflow=-21
CONTRAST flavor=normal unchecked=accept safe_for_this_input=1
OK padded
DETAIL policy=text message=accepted errcode=0
FIREWALL verdict=OK rule=- code=- errcode=0
```
(exit 0)

Hostile — traversal:

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall pathseg '../etc/passwd'
CONTRAST policy=pathseg naive=strcpy32 writes=14 buffer=32 overflow=-18
CONTRAST flavor=path-concat unchecked=accept escapes_base=1 path=/srv/app/data/../etc/passwd
REFUSED no-traversal E_TRAVERSAL
DETAIL policy=pathseg message=path traversal sequence errcode=108
FIREWALL verdict=REFUSED rule=no-traversal code=E_TRAVERSAL errcode=108
```
(exit 2)

Hostile — NUL marker (modelled):

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall pathseg 'report%00.txt'
CONTRAST policy=pathseg naive=strcpy32 writes=14 buffer=32 overflow=-18
CONTRAST flavor=nul-truncate unchecked=accept visible=report os_sees_only_prefix=1
REFUSED no-nul E_NUL
DETAIL policy=pathseg message=NUL byte injection marker errcode=108
FIREWALL verdict=REFUSED rule=no-nul code=E_NUL errcode=108
```
(exit 2)

Hostile — 300-byte oversize (the corruption case):

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall text "<300 x 'A'>"
CONTRAST policy=text naive=strcpy32 writes=301 buffer=32 overflow=269
CONTRAST flavor=unbounded-copy unchecked=corrupts overrun_bytes=269
REFUSED max-len E_SIZE
DETAIL policy=text message=input exceeds 256 bytes errcode=101
FIREWALL verdict=REFUSED rule=max-len code=E_SIZE errcode=101
```
(exit 2)

Hostile — control byte (a real TAB generated by `printf '\t'`):

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall text "line<TAB>tabs"
CONTRAST policy=text naive=strcpy32 writes=10 buffer=32 overflow=-22
CONTRAST flavor=log-passthrough unchecked=accept control_bytes=1 emitted_to_terminal=1
REFUSED no-control E_CONTROL
DETAIL policy=text message=control character errcode=108
FIREWALL verdict=REFUSED rule=no-control code=E_CONTROL errcode=108
```
(exit 2)

Hostile — SQL injection classic:

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall sqlsafe "' OR 1=1"
CONTRAST policy=sqlsafe naive=strcpy32 writes=9 buffer=32 overflow=-23
CONTRAST flavor=sql-format unchecked=accept always_true=1 query=SELECT * FROM users WHERE name='' OR 1=1'
REFUSED no-sqli E_SQLI
DETAIL policy=sqlsafe message=SQL injection marker errcode=108
FIREWALL verdict=REFUSED rule=no-sqli code=E_SQLI errcode=108
```
(exit 2)

Hostile — empty token:

```
$ timeout 30 examples/secure_apps/firewall/bin/firewall token ''
CONTRAST policy=token naive=strcpy32 writes=1 buffer=32 overflow=-31
CONTRAST flavor=policy unchecked=accept unknown_input_would_pass=1
REFUSED no-empty E_EMPTY
DETAIL policy=token message=input is empty errcode=108
FIREWALL verdict=REFUSED rule=no-empty code=E_EMPTY errcode=108
```
(exit 2)

`errcode` is the checked-stdlib error code: `108` = `CPLUS_EXIT_UNSUPPORTED`
(invalid input), `101` = `CPLUS_EXIT_BOUNDS` (oversize), `0` = `CPLUS_EXIT_OK`.

### Negative control (the harness is not a rubber stamp)

Making one expectation wrong in a temporary copy of `run.sh`
(`E_TRAVERSAL` → `E_WRONG` for the traversal cases) and running it produced:

```
REFUSE FAIL bad01 missing 'REFUSED no-traversal E_WRONG'
REFUSE FAIL bad01 missing matching FIREWALL verdict
REFUSE FAIL bad02 missing 'REFUSED no-traversal E_WRONG'
REFUSE FAIL bad02 missing matching FIREWALL verdict
SECURE_FIREWALL 19 8 2 9
```

with exit code `1`. The unmodified harness then produced
`SECURE_FIREWALL 19 8 0 11` with exit code `0`. The temporary copy was removed.

## Full scenario matrix

| name | policy | input | expected |
| --- | --- | --- | --- |
| ok01 | token | `alpha` | `OK alpha` |
| ok02 | token | `report` | `OK report` |
| ok03 | pathseg | `notes_2024` | `OK notes_2024` |
| ok04 | pathseg | `data-1` | `OK data-1` |
| ok05 | sqlsafe | `alice` | `OK alice` |
| ok06 | sqlsafe | `john doe` | `OK john doe` |
| ok07 | text | `hello world` | `OK hello world` |
| ok08 | text | `  padded  ` | `OK padded` |
| bad01 | pathseg | `../etc/passwd` | `REFUSED no-traversal E_TRAVERSAL` |
| bad02 | pathseg | `..\windows` | `REFUSED no-traversal E_TRAVERSAL` |
| bad03 | pathseg | `report%00.txt` | `REFUSED no-nul E_NUL` |
| bad04 | token | `safe\0evil` | `REFUSED no-nul E_NUL` |
| bad05 | text | 300 × `A` | `REFUSED max-len E_SIZE` |
| bad06 | text | `line<TAB>tabs` | `REFUSED no-control E_CONTROL` |
| bad07 | sqlsafe | `' OR 1=1` | `REFUSED no-sqli E_SQLI` |
| bad08 | sqlsafe | `admin'--` | `REFUSED no-sqli E_SQLI` |
| bad09 | token | `unknown` | `REFUSED allowlist E_NOTALLOWED` |
| bad10 | pathseg | `bad/name` | `REFUSED pattern E_NOTALLOWED` |
| bad11 | token | empty string | `REFUSED no-empty E_EMPTY` |

## What this deliberately does not demonstrate

- No real exploit is executed; the CONTRAST lines are computed consequences.
- No subprocess is spawned; the contrast is a documented, in-process model.
- No claim about real-world UTF-8/IDN/percent-decoding bypasses, canonicalisation,
  or any downstream system that consumes the sanitized output.
- `--sanitize=none` is intentional: the safety shown is the explicit check
  layer plus checked stdlib calls, not a sanitizer result.
