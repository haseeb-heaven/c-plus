# Secure vault example (`apps/legacy_examples/secure_apps/vault/`)

## Explicit scope statement

`vault.cp` is a **teaching application, not a production vault**. It is a
deterministic, in-memory model whose only purpose is to make checked-stdlib
**refusals observable at runtime**. It has no encryption, no authentication,
no persistence, no clock, no randomness, no prompts, no files, no network and
no real credentials. All secrets are synthetic argv tokens invented by the
test runner (for example `S3cr3t-A`). There are no hard-coded user paths.

The refusals reported below are **runtime evidence that specific checks fired
on specific inputs. They are not a security proof** of the program, of the C+
compiler, or of any deployment. Treat the transcript as a regression fixture.

## What the program does

- Fixed-capacity credential table: at most **8** credentials, service name
  **<= 16 bytes**, secret **<= 32 bytes**.
- Length is measured with `stdlib/string.hp` (`string::size`); copies go
  through `string::substr` into fixed-size arrays. Every refusal also sets a
  typed `error` value from `stdlib/error.hp` (`code` + stable `category`).
- Audit log: **16 fixed-format 31-byte records** in a **512-byte** in-memory
  buffer. A record is `"%04u %-8s %-16s"` (sequence, op, bounded service-name
  projection). It never contains a secret. A 17th append is refused with
  `CPLUS_EXIT_RESOURCE` (107, category `memory`) and the buffer is left
  unchanged — it is never silently truncated or dropped.
- Fail-closed rule: `add`, `delete`, `get` and `debug` are audited **before**
  they run. If the audit append fails, the operation does not execute.
  `list`, `peek` and `audit` are read-only and do not append, so the audit log
  remains readable even when full.
- A 64-byte canary pattern around the state is checked at every exit and
  printed in the summary (`canary ok`).

### Command set (argv only; `--` chains commands in one process)

| command | effect | emits secret? |
| --- | --- | --- |
| `add <service> <secret>` | store a credential | no |
| `get <service>` | retrieve a credential | **yes — the only place** |
| `list` | index + service names | no |
| `peek <index>` | service name at index | no |
| `delete <service>` | remove by service name | no |
| `debug <service>` | **refused** (`secret-logging-forbidden`) | no |
| `audit` | print the bounded audit records | no |

### Refusals and typed errors

| reason | code | category |
| --- | --- | --- |
| `duplicate-service`, `empty-name`, `empty-secret`, `missing-service`, `bad-args`, `unknown-command`, `usage`, `secret-logging-forbidden` | 108 | `invalid` |
| `oversized-name`, `oversized-secret`, `vault-full`, `index-out-of-range` | 101 | `range` |
| `audit-overflow` | 107 | `memory` |

Exit status: **0** when every operation succeeded, **1** when any operation
was refused, **2** for the no-argument usage case.

## Exact build command and output (from the repository root)

```sh
$ ./build/c+ build apps/legacy_examples/secure_apps/vault/vault.cp --strict --sanitize=none -I. -o apps/legacy_examples/secure_apps/vault/vault
$ echo $?
0
```

The command succeeds with **no diagnostics** on stdout or stderr.
`run.sh` uses the same flags with the output path `apps/legacy_examples/secure_apps/vault/.run/vault`.

## Exact runner command and output

```sh
$ apps/legacy_examples/secure_apps/vault/run.sh
BUILD ok vault
PASS happy
PASS duplicate
PASS empty-secret
PASS empty-name
PASS oversized-secret
PASS oversized-name
PASS missing
PASS index
PASS debug
PASS usage
PASS unknown
PASS audit-overflow
SECURE_VAULT 12 12 0 15
```

`run.sh` exit status: **0**. Every individual program invocation is wrapped in
`timeout 30`. `SECURE_VAULT <scenarios> <passed> <failed> <refused>` means:
12 scenarios, 12 passed, 0 failed, and 15 `VAULT refuse` lines were counted in
the captured transcripts. The runner was executed twice and produced
byte-identical output (`cmp -s`); the `happy` transcript was also byte-identical
across the two runs. `apps/legacy_examples/secure_apps/vault/.run/build.log` is 0 bytes.

## Exact scenario commands

Each line is one process (the `--` tokens are separators). `LONG_NAME` is 17
`N` characters; `LONG_SECRET` is 33 `X` characters.

| scenario | argv | expected rc |
| --- | --- | --- |
| happy | `add alpha S3cr3t-A -- add beta S3cr3t-B -- get alpha -- list -- peek 0 -- delete alpha -- list -- get beta -- audit` | 0 |
| duplicate | `add alpha S3cr3t-A -- add alpha S3cr3t-B` | 1 |
| empty-secret | `add gamma ""` | 1 |
| empty-name | `add "" sekrit` | 1 |
| oversized-secret | `add delta "$LONG_SECRET"` | 1 |
| oversized-name | `add "$LONG_NAME" sekrit` | 1 |
| missing | `get nope -- delete nope` | 1 |
| index | `add alpha S3cr3t-A -- peek 5 -- peek -1 -- peek x` | 1 |
| debug | `add alpha S3cr3t-A -- debug alpha` | 1 |
| usage | *(no arguments)* | 2 |
| unknown | `frobnicate` | 1 |
| audit-overflow | 9 x `add c<i> OVERFLOW-SECRET -- delete c<i>` chained with `--`, then `-- audit` | 1 |

### Representative exact transcripts

`happy` (the only scenario where secrets appear, and only via `get`):

```text
VAULT ok add alpha
VAULT ok add beta
VAULT get alpha S3cr3t-A
VAULT list 2
VAULT entry 0 alpha
VAULT entry 1 beta
VAULT peek 0 alpha
VAULT ok delete alpha
VAULT list 1
VAULT entry 0 beta
VAULT get beta S3cr3t-B
VAULT audit bytes 155 cap 512
VAULT audit-line 0000 add      alpha           
VAULT audit-line 0001 add      beta            
VAULT audit-line 0002 get      alpha           
VAULT audit-line 0003 delete   alpha           
VAULT audit-line 0004 get      beta            
VAULT summary ops 9 refused 0 audit-bytes 155 canary ok
```

`debug` — the debug-log attempt is refused and the stored secret does not
appear anywhere in the output:

```text
VAULT ok add alpha
VAULT refuse secret-logging-forbidden code 108 cat invalid
VAULT summary ops 2 refused 1 audit-bytes 62 canary ok
```

`audit-overflow` (tail) — the 17th and 18th audited operations are refused,
the audit buffer stays at 496/512 bytes, and 16 records survive:

```text
VAULT refuse audit-overflow code 107 cat memory
VAULT refuse audit-overflow code 107 cat memory
VAULT audit bytes 496 cap 512
VAULT summary ops 19 refused 2 audit-bytes 496 canary ok
```

A cross-check over every captured transcript found the secret tokens
`S3cr3t-A`, `S3cr3t-B` and `OVERFLOW-SECRET` only on `VAULT get ...` lines and
nowhere else.

## Threat model

### What is protected (demonstrated by the transcripts)

- **Bounded input**: oversized names/secrets are measured and refused before
  any copy; fixed-size destination arrays are used with checked `substr`.
- **No duplicate overwrite**: a second `add` for an existing service is
  refused; the original value survives.
- **No silent loss**: missing services and out-of-range indices are refused
  with typed errors instead of returning stale or adjacent data.
- **No secret in routine output**: `add`, `list`, `peek`, `delete`, `audit`
  and the refused `debug` path never print a secret. Only explicit `get` does.
- **No unaudited sensitive operation**: `add`/`delete`/`get`/`debug` are
  recorded first; an audit-buffer overflow refuses the operation rather than
  dropping the record.
- **No silent audit truncation/drop**: a full audit buffer yields a typed
  `audit-overflow` refusal; the recorded bytes remain unchanged.
- **No out-of-bounds write in these paths**: the 64-byte canary reads `ok`
  after every scenario, including the refusal-heavy ones.

### What is NOT protected

- **No confidentiality at rest or in memory.** Secrets are plaintext in
  ordinary arrays. Core dumps, swap, `/proc/<pid>/mem`, a debugger, or any
  process with sufficient privileges can read them. There is no encryption,
  key management, zeroization, or constant-time comparison.
- **argv exposure.** Secrets are passed on the command line; on a real system
  that is visible to process listings and audit shells. This is a deliberate
  teaching simplification, not a secret-delivery mechanism.
- **No authentication, authorization, or isolation.** Any caller in the same
  process can invoke any command. There is no user model or ACL.
- **No tamper-evident or persistent audit.** The audit buffer is in-memory,
  lost at exit, and can be overwritten by in-process code. Names are stored as
  a bounded 16-byte projection; the full name is not guaranteed to be recorded
  (no secret is ever recorded).
- **No concurrency safety.** The model is single-threaded; there are no locks
  and no guarantees under races.
- **No host compromise defence.** A malicious kernel, injected library,
  `ptrace`, or memory scanner defeats every property above.
- **No general C safety.** The checked stdlib helps only where it is used.
  Raw C APIs, unchecked casts, or future code outside these paths are not
  covered.
- **`get` intentionally emits secrets.** Redirecting stdout, logging that
  output, or a shell history entry can leak them. The program cannot prevent
  misuse of its own output.
- **Not a proof.** `--strict` produces compiler diagnostics for the patterns
  it understands; the canary and the refusal transcript are empirical checks
  on a fixed input set. Neither constitutes formal verification, fuzzing
  coverage, or a security guarantee for other inputs or builds.
