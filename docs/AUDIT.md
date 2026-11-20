# Safety audit and reference-project decision

## Scope of the bug list

The task refers to "54 classic C bugs" and asks for the original list. I
searched the web and could not locate a canonical 54-item list. The
conversation supplied **25 explicitly numbered bugs** (1–25), so this audit
covers exactly those 25. Items 26–54 are **not invented** here; they need the
actual source list before any claim can be made about them.

## Bug status (1–41)

The detailed table lives in [LIMITATIONS.md](LIMITATIONS.md). Summary:

- **Prevented at compile time:** 1, 3, 5 (C+ allocations), 7, 10–16, 18–20,
  22, 24 (direct global writes in thread entry functions), 26–34, 36–41.
- **Aborted safely at run time:** 6, 9, 21, 25, and dynamic forms of 1.
- **Prevented by RAII design:** 4, 5, 14 (when C+ resources are used).
- **Not automatically detected:** 2 (raw UAF), 8 (null terminator), 17
  (macro double evaluation), 23 (flexible array member copy), and general
  data races beyond the conservative direct-global check.

Every item has a named test under `tests/`; undetected items also have raw
reference programs in `tests/known_gaps/`. The original 54-item list was not
found, so no claim is made about items 42–54.

## Reference projects: legal and technical audit

| project | license / constraint | finding |
| --- | --- | --- |
| [Fil-C](https://github.com/pizlonator/fil-c) | Apache-2.0 with LLVM exceptions (no explicit AI-training clause found in repo) | **Not integrated.** It is a full Clang fork + concurrent GC runtime, Linux/x86-64 only. Adopting it would violate the requirements to reuse the existing toolchain, keep native C ABI, and avoid mandatory GC. No Fil-C code was copied. |
| [Checked C](https://github.com/checkedc/checkedc-clang) | LLVM-style Apache-2.0 (fork tree) | **Not integrated.** Checked pointers require a forked Clang frontend and source annotations; the host Clang 16 has no checked-pointer support. Copying a fork would duplicate the compiler we already reuse. |
| [Clang Bounds Safety](https://github.com/swiftlang/llvm-project) | Apache-2.0 with LLVM exceptions | **Blocked by toolchain.** `-fbounds-safety` is not accepted by `/usr/bin/clang` (Apple clang 16), and the locally installed LLVM 23 `clang` fails at startup because `libLLVM.23.1.dylib` is missing. Our token-level bounds pass + UBSan are the justified alternative. |
| [SafeC](https://github.com/safec-org/SafeC) | MIT | **Not integrated.** License is compatible, but its ownership/region analyzer is a separate whole-program analysis; integrating it would duplicate the existing lowering/RAII pipeline rather than extend it. Ideas such as explicit ownership contracts are already implemented in C+ form. |
| [Rust](https://github.com/rust-lang/rust) | MIT / Apache-2.0 | **No code copied.** Its borrow-checking *ideas* informed the small C+ borrow subset (escape and use-after-move), but Rust's type system is explicitly out of scope. |
| [Cake](https://github.com/thradams/cake) | GPLv3 + addendum forbidding AI training/reference use | **Rejected.** Copying or using it as implementation reference is legally incompatible with this MIT project and with the addendum. |

## What is actually reused

- the host **Clang driver** for C parsing, type checking, optimisation,
  codegen and linking;
- Clang diagnostics promoted to errors (`-Werror=...`) for unsequenced
  modifications, shift overflow, sign comparison, string-literal writes,
  `sizeof` decay, format security, stack-address returns, uninitialised
  reads, array bounds and more;
- **UBSan** (`undefined,bounds,null,alignment`) with
  `-fno-sanitize-recover=all` for run-time failures;
- **LLVM Support** libraries in the `c+` driver itself;
- **libc and POSIX** underneath the `.hp` standard-library wrappers.

No third-party source code was copied into this repository in this work.

## Rationale for not rewriting

The spec requires C+ to remain a small extension over C, preserve native
compilation and C ABI, avoid mandatory GC, and not duplicate existing
functionality. Replacing the backend with Fil-C or Checked C would produce a
different language implementation rather than an upgrade to this one. The
incremental path taken here is: promote compiler diagnostics, add small
token-level static checks, and keep deterministic RAII plus a managed
allocator as the C+-specific layer.

## Remaining blockers (verified, not assumed)

- **Use-after-free:** AddressSanitizer aborts inside its own runtime on this
  host (`sanitizer_malloc_mac.inc:189`). TSan also fails with "Interceptors
  are not working". No automatic stale-dereference detection is possible
  here. `cplus_check_live()` provides an explicit check.
- **Clang bounds safety:** unsupported by the available clang binary.
- **Full data-race prevention:** only direct global writes in thread entry
  functions are diagnosed; atomics, aliases and helper-thread patterns are
  not fully analysed.
