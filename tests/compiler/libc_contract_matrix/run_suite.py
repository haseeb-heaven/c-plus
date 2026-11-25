#!/usr/bin/env python3
"""Table-driven libc contract assertion matrix (issue #25, criterion 5).

Each contract in compiler/Analysis/libc_contracts.json is listed. Contracts
with compiler-enforced semantics get an executable positive case (must be
accepted) and negative case (must be rejected with the asserted diagnostic).
The `_s` capability-layer entries are declared PARTIAL: their JSON metadata is
asserted, but no compiler diagnostic is claimed because their exact signatures
belong to the Agent 2 capability layer.

Exact commands, exit codes, stdout and stderr are recorded. No skips.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/libc_contract_matrix/run_suite.py
"""
import datetime
import json
import os
import platform
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASE_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASE_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)
CONTRACTS = os.path.join(ROOT, "compiler", "Analysis", "libc_contracts.json")

ALLOC = "#include <stdio.h>\n#include <stdlib.h>\n"
STR = "#include <stdio.h>\n#include <string.h>\n"
FILE = "#include <stdio.h>\n"

# name: (status, positive source, negative source, expected diagnostic)
CASES = {
 "malloc": ("assert", ALLOC+"int main(void){ int *p=malloc(16); if(p){p[0]=1; free(p);} return 0; }\n",
   ALLOC+"int main(void){ int *p=malloc(4); p[9]=1; free(p); return 0; }\n", "exceeds heap allocation"),
 "calloc": ("assert", ALLOC+"int main(void){ int *p=calloc(4,4); if(p){p[0]=1; free(p);} return 0; }\n",
   ALLOC+"int main(void){ int *p=calloc(1,4); p[9]=1; free(p); return 0; }\n", "exceeds heap allocation"),
 "realloc": ("assert", ALLOC+"int main(void){ int *p=malloc(4); if(!p)return 1; int *q=realloc(p,64); if(!q)return 1; q[9]=1; free(q); return 0; }\n",
   ALLOC+"int main(void){ int *p=malloc(4); if(!p)return 1; int *q=realloc(p,4); if(!q)return 1; q[9]=1; free(q); return 0; }\n", "exceeds heap allocation"),
 "free": ("assert", ALLOC+"int main(void){ int *p=malloc(16); if(p)free(p); return 0; }\n",
   ALLOC+"int main(void){ int *p=malloc(16); if(!p)return 1; free(p+1); return 0; }\n", "interior"),
 "memcpy": ("assert", STR+"int main(void){ char a[8],b[8]; memcpy(a,b,8); return a[0]; }\n",
   STR+"int main(void){ char a[4],b[8]; memcpy(a,b,9); return a[0]; }\n", "overflow"),
 "memmove": ("assert", STR+"int main(void){ char a[8],b[8]; memmove(a,b,8); return a[0]; }\n",
   STR+"int main(void){ char a[4],b[8]; memmove(a,b,9); return a[0]; }\n", "overflow"),
 "memset": ("assert", STR+"int main(void){ char a[8]; memset(a,0,8); return a[0]; }\n",
   STR+"int main(void){ char a[4]; memset(a,0,9); return a[0]; }\n", "overflow"),
 "strcpy": ("assert", STR+"int main(void){ char a[8]; strcpy(a,\"ok\"); return a[0]; }\n",
   STR+"int main(void){ char a[2]; strcpy(a,\"overflow\"); return a[0]; }\n", "overflow"),
 "strncpy": ("assert", STR+"int main(void){ char a[8]; strncpy(a,\"ok\",8); return a[0]; }\n",
   STR+"int main(void){ char a[2]; strncpy(a,\"overflow\",9); return a[0]; }\n", "overflow"),
 "strcat": ("assert", STR+"int main(void){ char a[8]=\"x\"; strcat(a,\"y\"); return a[0]; }\n",
   STR+"int main(void){ char a[2]=\"x\"; strcat(a,\"overflow\"); return a[0]; }\n", "overflow"),
 "strlen": ("assert", STR+"int main(void){ return strlen(\"ok\")==2?0:1; }\n",
   STR+"int main(void){ char *p=0; return strlen(p); }\n", "null pointer"),
 "fopen": ("assert", FILE+"int main(void){ FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"w\"); if(!f)return 1; fclose(f); return 0; }\n",
   FILE+"int main(void){ FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"w\"); if(!f)return 1; fclose(f); fputc('x',f); return 0; }\n", "use of closed FILE"),
 "fclose": ("assert", FILE+"int main(void){ FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"w\"); if(!f)return 1; fclose(f); return 0; }\n",
   FILE+"int main(void){ FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"w\"); if(!f)return 1; fclose(f); fclose(f); return 0; }\n", "double close"),
 "fread": ("assert", FILE+"int main(void){ char b[4]; FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"rb\"); if(!f)return 1; size_t n=fread(b,1,4,f); fclose(f); return n==4?0:1; }\n",
   FILE+"int main(void){ char b[4]; FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"rb\"); if(!f)return 1; size_t n=fread(b,1,9,f); fclose(f); return n==4?0:1; }\n", "fread reads"),
 "fwrite": ("assert", FILE+"int main(void){ char b[4]={0}; FILE *f=fopen(\"/tmp/cplus_matrix.txt\",\"wb\"); if(!f)return 1; size_t n=fwrite(b,1,4,f); fclose(f); return n==4?0:1; }\n",
   FILE+"int main(void){ char b[4]={0}; FILE *f=0; size_t n=fwrite(b,1,4,f); return n==4?0:1; }\n", "null pointer argument"),
}
PARTIAL = ["strcpy_s","strncpy_s","strcat_s","memcpy_s","memmove_s","memset_s",
           "strnlen_s","sprintf_s","snprintf_s","fopen_s","malloc_s","calloc_s",
           "realloc_s","free_s"]


def run(argv, timeout=60):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None, "stdout": "", "stderr": "", "timed_out": True}


def main():
    contracts = json.load(open(CONTRACTS))["functions"]
    rows, fails = [], []
    for name in sorted(CASES):
        status, pos, neg, diag = CASES[name]
        pp = os.path.join(CASE_DIR, name + ".pos.c")
        np = os.path.join(CASE_DIR, name + ".neg.c")
        open(pp, "w").write(pos)
        open(np, "w").write(neg)
        a = run([BIN, "check", "--default", "--sanitize=none", pp])
        b = run([BIN, "check", "--default", "--sanitize=none", np])
        ok = a["rc"] == 0 and b["rc"] not in (0, None) and diag in b["stderr"]
        rows.append({"contract": name, "status": "PASS" if ok else "FAIL",
                     "kind": "assert", "expected": diag, "steps": [a, b]})
        if not ok:
            fails.append(name)
        print("%-12s %s" % (name, "PASS" if ok else "FAIL"))
    for name in PARTIAL:
        entry = contracts.get(name)
        ok = bool(entry) and "checked API" in str(entry.get("params", ""))
        rows.append({"contract": name, "status": "PASS" if ok else "FAIL",
                     "kind": "partial", "expected": "JSON metadata present",
                     "steps": []})
        if not ok:
            fails.append(name)
        print("%-12s %s" % (name, "PASS" if ok else "FAIL"))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "contracts_total": len(contracts), "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("libc contract matrix (issue #25)\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-12s %-6s %s\n" % (r["contract"], r["kind"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:120],
                         s["stderr"][:300]))
        f.write("\ncontracts=%d cases=%d passed=%d failed=%d\n" %
                (len(contracts), len(rows), len(rows)-len(fails), len(fails)))
    print("libc_contract_matrix: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
