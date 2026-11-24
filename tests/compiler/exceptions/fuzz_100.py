#!/usr/bin/env python3
"""100 deterministic C+ exception variants: compile, run, verify exact output.

Ten templates x ten value variations. Each case is built with
`c+ build --default --sanitize=none` and executed. A case passes only when the
process exit code AND stdout (and, for the negative template, stderr marker)
match the expected outcome. No `|| true`, hard 60s timeouts.
"""
import json, os, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(OUT, "fuzz_work")
os.makedirs(WORK, exist_ok=True)
H = '#include "cplus/cplus.h"\n'


def cases():
    out = []
    for k in range(10):
        n = 10 + k * 7
        msg = "m%d" % k
        # 0: throw integer, one catch
        out.append((f"int_throw_{k}", H + f'''int main(void){{ try {{ print("a"); throw {n}; print("bad"); }} catch (Exception e) {{ print("c", e.code); }} print("done"); return 0; }}
''', f"a\nc {n}\ndone\n", 0, None))
        # 1: throw string, read message
        out.append((f"str_throw_{k}", H + f'''int main(void){{ try {{ throw "{msg}"; }} catch (Exception e) {{ print("c", e.message); }} return 0; }}
''', f"c {msg}\n", 0, None))
        # 2: try with no throw
        out.append((f"no_throw_{k}", H + f'''int main(void){{ try {{ print("v", {n}); }} catch (Exception e) {{ print("bad"); }} print("end"); return 0; }}
''', f"v {n}\nend\n", 0, None))
        # 3: nested try, inner throws
        out.append((f"nested_{k}", H + f'''int main(void){{ try {{ try {{ throw {n}; }} catch (Exception e) {{ print("in", e.code); }} print("mid"); }} catch (Exception e) {{ print("out"); }} print("end"); return 0; }}
''', f"in {n}\nmid\nend\n", 0, None))
        # 4: throw from a helper called inside try
        out.append((f"helper_{k}", H + f'''static void boom(void){{ throw {n}; }}
int main(void){{ try {{ boom(); print("bad"); }} catch (Exception e) {{ print("h", e.code); }} return 0; }}
''', f"h {n}\n", 0, None))
        # 5: two sequential tries
        out.append((f"seq_{k}", H + f'''int main(void){{ try {{ throw {n}; }} catch (Exception e) {{ print("x", e.code); }} try {{ throw "{msg}"; }} catch (Exception e) {{ print("y", e.message); }} return 0; }}
''', f"x {n}\ny {msg}\n", 0, None))
        # 6: conditional throw
        out.append((f"cond_{k}", H + f'''int main(void){{ int c = {k % 2}; try {{ if (c) throw {n}; print("no"); }} catch (Exception e) {{ print("yes", e.code); }} return 0; }}
''', (f"yes {n}\n" if k % 2 else "no\n"), 0, None))
        # 7: loop with a throw after N iterations
        out.append((f"loop_{k}", H + f'''int main(void){{ for (int i=0;i<{k+2};i++) {{ if (i=={k}) throw {n}; }} return 0; }}
''', "", 108, "unhandled C+ exception"))
        # 8: throw inside switch
        out.append((f"switch_{k}", H + f'''int main(void){{ int x={k%3}; try {{ switch(x) {{ case 0: print("z"); break; case 1: throw {n}; default: break; }} }} catch (Exception e) {{ print("s", e.code); }} return 0; }}
''', (f"s {n}\n" if k % 3 == 1 else ("z\n" if k % 3 == 0 else "")), 0, None))
        # 9: throw with no active try (negative)
        out.append((f"unhandled_{k}", H + f'''int main(void){{ throw {n}; return 0; }}
''', "", 108, "unhandled C+ exception"))
    return out


def main():
    rows, fails = [], []
    for name, src, want_out, want_rc, want_err in cases():
        path = os.path.join(WORK, name + ".cp")
        exe = os.path.join(WORK, name + ".bin")
        open(path, "w").write(src)
        b = subprocess.run([BIN, "build", "--default", "--sanitize=none", path,
                            "-o", exe], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60)
        rec = {"id": name, "build_rc": b.returncode,
               "expected_rc": want_rc, "expected_stdout": want_out,
               "expected_stderr": want_err}
        if b.returncode:
            rec.update(status="FAIL", detail=b.stderr.decode(errors="replace")[:300])
            rows.append(rec); fails.append(name)
            continue
        r = subprocess.run([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=60)
        out = r.stdout.decode(errors="replace")
        err = r.stderr.decode(errors="replace")
        rec.update(run_rc=r.returncode, stdout=out[:400], stderr=err[:400])
        ok = (r.returncode == want_rc and out == want_out and
              (want_err is None or want_err in err))
        rec["status"] = "PASS" if ok else "FAIL"
        rows.append(rec)
        if not ok:
            fails.append(name)
        print("%-18s %s" % (name, rec["status"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "count": len(rows), "failures": fails, "results": rows}
    json.dump(payload, open(os.path.join(OUT, "fuzz_100.json"), "w"), indent=2)
    with open(os.path.join(OUT, "fuzz_100.txt"), "w") as f:
        f.write("100 C+ exception variants (DEFAULT --sanitize=none)\n")
        for r in rows:
            f.write("%-18s %s\n" % (r["id"], r["status"]))
        f.write("\nfailures: %d\n" % len(fails))
        for n in fails:
            f.write("  " + n + "\n")
    print("fuzz100: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
