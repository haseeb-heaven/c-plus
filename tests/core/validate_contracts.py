#!/usr/bin/env python3
"""Validate the compiler-visible libc contract schema."""
import json, pathlib, sys
p = pathlib.Path(__file__).parents[2] / "compiler/Analysis/libc_contracts.json"
doc = json.loads(p.read_text())
required = {"malloc","calloc","realloc","free","memcpy","memmove","memset",
            "strcpy","strncpy","strcat","strlen","fopen","fclose","fread",
            "fwrite","strcpy_s","memcpy_s","malloc_s","free_s"}
missing = sorted(required - set(doc["functions"]))
if missing:
    print("missing contracts:", ", ".join(missing)); sys.exit(1)
for name, spec in doc["functions"].items():
    for key in ("params","ownership","effects","diagnostics","failure","paramTypes","pointerBounds","allocationSize","ownershipTransfer","borrowLifetime","readEffects","writeEffects","freeEffects","returnState","threadAssumptions"):
        if key not in spec:
            print(f"{name}: missing {key}"); sys.exit(1)
    if not spec["diagnostics"].startswith("CPLUS-SAFE-"):
        print(f"{name}: bad diagnostic category"); sys.exit(1)
print(f"contract schema: ok ({len(doc['functions'])} functions)")
