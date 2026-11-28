#!/usr/bin/env python3
"""Inventory libc API usage in the corpus vs compiler-visible contract coverage.

This measures SURFACE coverage only. It does not claim semantic completeness
of any contract, nor that uncovered functions are unsafe.
"""
import json
import pathlib
import re
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
contracts = json.loads(
    (ROOT / "compiler/Analysis/libc_contracts.json").read_text())
covered = set(contracts["functions"])

used = Counter()
for path in (ROOT / "tests").rglob("*"):
    if not path.is_file() or path.suffix not in {".c", ".cp"}:
        continue
    if "out" in path.parts:
        continue
    text = path.read_text(errors="ignore")
    for name in set(re.findall(r"\b([a-z][a-z0-9_]{2,})\s*\(", text)):
        used[name] += 1

common = {"malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset",
          "memcmp", "strcpy", "strncpy", "strcat", "strncat", "strlen",
          "strnlen", "strcmp", "strncmp", "strchr", "strrchr", "strstr",
          "strdup", "strerror", "fopen", "fclose", "fread", "fwrite",
          "fgets", "fputs", "fprintf", "printf", "sprintf", "snprintf",
          "scanf", "sscanf", "fscanf", "puts", "putchar", "getchar",
          "exit", "abort", "open", "close", "read", "write", "socket",
          "pthread_create", "pthread_join", "pthread_mutex_lock",
          "pthread_mutex_unlock", "setjmp", "longjmp", "qsort", "bsearch",
          "getenv", "setenv", "putenv", "strtol", "strtoul", "strtod",
          "atoi", "atol", "rand", "srand", "time", "localtime", "gmtime"}
surface = {n: c for n, c in used.items() if n in common}
uncovered = sorted(((n, c) for n, c in surface.items() if n not in covered),
                   key=lambda x: -x[1])

lines = [
    "# libc API surface coverage (evidence, not a safety claim)",
    "",
    f"Compiler contract functions: {len(covered)}",
    f"Distinct libc functions appearing in corpus sources: {len(surface)}",
    f"Frequently used but WITHOUT a compiler contract: {len(uncovered)}",
    "",
    "| function | corpus occurrences |",
    "| --- | ---: |",
]
for name, count in uncovered[:40]:
    lines.append(f"| {name} | {count} |")
lines += ["",
          "This table measures contract-JSON surface only: several listed "
          "functions (printf, scanf, fgets, ...) have dedicated name-specific "
          "AST checks instead. Absence from the JSON is not a safety claim. "
          "Extending this "
          "list requires agreed semantics with Agent 2 (issue #23) and "
          "positive/negative tests before any new diagnostic is enabled."]
out = ROOT / "docs/LIBC_API_GAP.md"
out.write_text("\n".join(lines) + "\n")
print("\n".join(lines[:8]))
