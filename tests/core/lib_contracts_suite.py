#!/usr/bin/env python3
"""Compile-time libc/stdlib contract regression suite (Agent 1, issue #25).

For every rule below there is a negative case that must be rejected with an
exact diagnostic substring and a positive counterpart that must be accepted.
Contract-loader failures are exercised through CPLUS_CONTRACT_PATH.
"""
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
CPLUS_CLANG = os.environ.get("CPLUS_CLANG", "")
OUT = ROOT / "tests/out/core/lib_contracts"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
if CPLUS_CLANG:
    ENV["CPLUS_CLANG"] = CPLUS_CLANG

HEAD = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"

# name, source, exact expected diagnostic substring
NEGATIVE = {
    "format_nonliteral": (
        HEAD + 'int main(void){char s[16];fgets(s,sizeof s,stdin);printf(s);return 0;}\n',
        "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
    "format_fn_pointer": (
        HEAD + 'int main(void){char s[16];fgets(s,sizeof s,stdin);'
               'int(*f)(const char*,...)=printf;f(s);return 0;}\n',
        "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
    "format_percent_n": (
        HEAD + 'int main(void){int n=0;printf("n=%n",&n);return n;}\n',
        "'%n' in printf performs an unverifiable write through a variadic argument"),
    "format_missing_arg": (
        HEAD + 'int main(void){printf("%d %s\\n",42);return 0;}\n',
        "more '%' conversions than data arguments"),
    "format_swapped_width": (
        HEAD + 'int main(void){printf("%*s\\n","admin",16);return 0;}\n',
        "field width should have type"),
    "scanf_unbounded_s": (
        HEAD + 'int main(void){char w[8];fscanf(stdin,"%s",w);return 0;}\n',
        "unbounded '%s' in fscanf writes into fixed-size array 'w'; add a field width"),
    "unterminated_char_array": (
        HEAD + "int main(void){char t[4]={'A','B','C','D'};printf(\"%s\",t);return 0;}\n",
        "char array 't' has no NUL terminator; %s will over-read"),
    "gets_unbounded": (
        HEAD + 'int main(void){char b[8];gets(b);return 0;}\n',
        "gets() performs an unbounded write; use fgets with an explicit capacity"),
    "sprintf_unbounded": (
        HEAD + 'int main(void){char b[8];sprintf(b,"long string");return 0;}\n',
        "sprintf() writes without a destination capacity; use snprintf or cplus_sprintf_s"),
    "calloc_overflow": (
        HEAD + 'int main(void){void*p=calloc(2,(size_t)-1);(void)p;return 0;}\n',
        "calloc count * size overflows size_t; the allocation is smaller than requested"),
    "fgets_size_overflow": (
        HEAD + 'int main(void){char s[8];fgets(s,64,stdin);return 0;}\n',
        "fgets reads 64 bytes into destination 's' of 8 bytes"),
    "fgets_sizeof_pointer": (
        HEAD + 'void f(char*b){fgets(b,sizeof(b),stdin);}\nint main(void){char x[8];f(x);return 0;}\n',
        "sizeof(char*) is the pointer size, not the buffer size"),
    "fgets_ignored_result": (
        HEAD + 'int main(void){char b[32];fgets(b,sizeof b,stdin);printf("%s",b);return 0;}\n',
        "ignored fgets return value leaves 'b' uninitialized on EOF; check the result"),
    "fgets_null_stream": (
        HEAD + 'int main(void){char b[8];fgets(b,sizeof b,NULL);return 0;}\n',
        "null FILE* passed to fgets"),
    "fread_overflow": (
        HEAD + 'int main(void){char b[64];size_t n=(size_t)-1/4+10;'
               'size_t r=fread(b,4,n,stdin);(void)r;return 0;}\n',
        "size * count overflows size_t in fread; the requested transfer wraps to a small value"),
    "fread_dest_overflow": (
        HEAD + 'int main(void){char b[8];size_t r=fread(b,1,64,stdin);(void)r;return 0;}\n',
        "fread reads 64 bytes into destination 'b' of 8 bytes"),
    "fread_zero_uninit": (
        HEAD + 'int main(void){int r[4];size_t n=fread(r,sizeof r,1,stdin);'
               'if(n==0)return r[0];return 0;}\n',
        "read of uninitialized array 'r'; fread returned zero and wrote nothing"),
    "fclose_alias": (
        HEAD + 'int main(void){FILE*f=fopen("t","w");if(!f)return 1;'
               'FILE*g=f;fclose(f);f=NULL;fprintf(g,"x");return 0;}\n',
        "use of closed FILE* 'g'"),
    "snprintf_index": (
        HEAD + 'int main(void){char b[8];int n=snprintf(b,sizeof b,"too long");'
               'b[n]=0;return 0;}\n',
        "snprintf return value used as index into its destination 'b'; it may exceed the buffer capacity on truncation"),
    "strerror_static": (
        '#include <errno.h>\n#include <string.h>\n' + HEAD +
        'int main(void){const char*a=strerror(ENOENT);const char*b=strerror(ENOMEM);'
        'printf("%s|%s",a,b);return 0;}\n',
        "'strerror' keeps static state; a second call invalidates earlier results"),
    "getenv_setenv": (
        HEAD + 'int main(void){setenv("K","v",1);const char*p=getenv("K");'
               'setenv("X","y",1);printf("%s",p);return 0;}\n',
        "getenv result 'p' may dangle after setenv reallocates environ"),
    "checked_capacity_lying": (
        '#include "cplus/cplus.h"\n' + HEAD +
        'int main(void){char d[4];int n=cplus_strcpy(d,100,"abcdefgh");'
        '(void)n;return 0;}\n',
        "the _s argument does not validate the buffer size"),
    "snprintf_capacity_lying": (
        HEAD + 'int main(void){char d[8];int n=snprintf(d,100,"abcdefghijk");'
               '(void)n;return 0;}\n',
        "format write of 100 bytes exceeds destination 'd'"),
}

# name, source, exact expected diagnostic substring
POSITIVE = {
    "format_literal_ok": HEAD + 'int main(void){printf("ok %d\\n",42);return 0;}\n',
    "percent_n_ok": HEAD + 'int main(void){printf("ok\\n");return 0;}\n',
    "scanf_width_ok": HEAD + 'int main(void){char w[8];fscanf(stdin,"%7s",w);return 0;}\n',
    "gets_replaced_ok": HEAD + 'int main(void){char b[8];fgets(b,sizeof b,stdin);return 0;}\n',
    "sprintf_replaced_ok": HEAD + 'int main(void){char b[8];snprintf(b,sizeof b,"x");return 0;}\n',
    "calloc_ok": HEAD + 'int main(void){void*p=calloc(4,sizeof(int));if(!p)return 1;free(p);return 0;}\n',
    "fgets_ok": HEAD + 'int main(void){char b[8];if(fgets(b,sizeof b,stdin))printf("%s",b);return 0;}\n',
    "fread_ok": HEAD + 'int main(void){char b[64];size_t n=fread(b,1,sizeof b,stdin);if(n==0)return 1;return 0;}\n',
    "fclose_ok": HEAD + 'int main(void){FILE*f=fopen("t","w");if(!f)return 1;fclose(f);return 0;}\n',
    "snprintf_index_guard_ok": HEAD + 'int main(void){char b[8];int n=snprintf(b,sizeof b,"x");if(n<8)b[n]=0;return 0;}\n',
    "strerror_ok": '#include <errno.h>\n' + HEAD +
                   'int main(void){printf("%s",strerror(ENOENT));return 0;}\n',
    "checked_capacity_ok": '#include "cplus/cplus.h"\n' + HEAD +
        'int main(void){char d[8];int n=cplus_strcpy(d,sizeof d,"abc");'
        '(void)n;return 0;}\n',
}

CONTRACT_FILES = {
    "contract_missing_file": ("/tmp/cplus_missing_contract.json", None,
        "libc contract file not found"),
    "contract_malformed_json": (None, "{ bad json\n",
        "libc contract file is not valid JSON"),
    "contract_bad_version": (None, '{"version":1,"functions":{}}\n',
        "unsupported libc contract version (expected 2)"),
    "contract_unknown_field": (None,
        json.dumps({"version": 2, "functions": {"malloc": {"magicField": 1}}}),
        "unsupported contract field 'magicField'"),
}


def run(src, mode="--strict", contract=None):
    p = OUT / "case.c"
    p.write_text(src)
    env = dict(ENV)
    env.pop("CPLUS_CONTRACT_PATH", None)
    if contract:
        path, content, _ = contract
        if content is not None:
            path = str(OUT / (os.path.basename(path) if path
                              else "contract_override.json"))
            pathlib.Path(path).write_text(content)
        env["CPLUS_CONTRACT_PATH"] = path
    r = subprocess.run([CC, "check", mode, str(p)], capture_output=True,
                       text=True, env=env, timeout=120)
    return r.returncode, r.stderr + r.stdout


results = []
for name, (src, needle) in NEGATIVE.items():
    rc, diag = run(src)
    results.append((name, "negative", rc != 0 and needle in diag, needle,
                    diag.strip().splitlines()[:1]))
for name, src in POSITIVE.items():
    rc, diag = run(src)
    results.append((name, "positive", rc == 0 and "error:" not in diag,
                    "accepted", diag.strip().splitlines()[:1]))
for name, (path, content, needle) in CONTRACT_FILES.items():
    rc, diag = run(HEAD + 'int main(void){void*p=malloc(4);free(p);return 0;}\n',
                   contract=(path, content, needle))
    results.append((name, "contract", rc != 0 and needle in diag, needle,
                    diag.strip().splitlines()[:1]))

# Cross-TU: contracts are enforced in each translation unit independently.
# (True interprocedural ownership across TUs is NOT claimed.)
TU_A = HEAD + 'void tu_a(void){void*p=malloc(8);if(!p)return;free(p);}\n'
TU_B = HEAD + 'void tu_b(void){char s[16];fgets(s,sizeof s,stdin);printf(s);}\n'
pa, pb = OUT / "tu_a.c", OUT / "tu_b.c"
pa.write_text(TU_A)
pb.write_text(TU_B)
for name, p, want_reject, needle in [
    ("cross_tu_safe_tu", pa, False, ""),
    ("cross_tu_violating_tu", pb, True,
     "format string in printf is not a string literal (CWE-134; attacker-controlled format)"),
]:
    r = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                       text=True, env=ENV, timeout=120)
    diag = r.stderr + r.stdout
    ok = (r.returncode != 0 and needle in diag) if want_reject else (
        r.returncode == 0 and "error:" not in diag)
    results.append((name, "cross_tu", ok, needle or "accepted",
                    diag.strip().splitlines()[:1]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
lines = [f"lib contracts suite: {passed}/{len(results)} passed"]
for name, kind, ok, needle, diag in results:
    lines.append(f"  {'ok  ' if ok else 'FAIL'} {kind:8} {name:28} "
                 f"expect={needle[:55]!r} got={diag[0][-70:] if diag else ''}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(l for l in lines[1:] if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
