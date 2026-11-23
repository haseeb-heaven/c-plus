/* Safe-Rust equivalent of examples/expanded/database/table_query.cp.
 *
 * Rows are kept in an owned Vec<[i32;3]> with a HashSet id index; every query
 * goes through slice/iterator access, and SUM uses i32::checked_add (the
 * equivalent of the C+ overflow guard). Refusals carry stable category/code/
 * message strings. No unsafe, no FFI, no raw pointers.
 */

use std::collections::HashSet;

const FIELDS: usize = 3;
const COL_ID: usize = 0;
const COL_GRP: usize = 1;
const COL_AMT: usize = 2;

#[derive(Clone, Copy)]
struct Refusal {
    category: &'static str,
    code: i32,
    msg: &'static str,
}

fn refuse(category: &'static str, code: i32, msg: &'static str) -> Refusal {
    Refusal { category, code, msg }
}

type R<T> = Result<T, Refusal>;

struct Counters {
    ops: i32,
    ok: i32,
    refused: i32,
    failed: i32,
}

impl Counters {
    fn new() -> Self {
        Counters { ops: 0, ok: 0, refused: 0, failed: 0 }
    }
}

fn field_index(name: &str) -> i32 {
    match name {
        "id" => COL_ID as i32,
        "grp" => COL_GRP as i32,
        "amt" => COL_AMT as i32,
        _ => -1,
    }
}

fn parse_int(text: &str) -> Option<i32> {
    text.parse::<i32>().ok()
}

fn in_range(row: &[i32; FIELDS], field: usize, lo: i32, hi: i32) -> bool {
    lo <= row[field] && row[field] <= hi
}

fn count_matching(rows: &[[i32; FIELDS]], field: i32, lo: i32, hi: i32) -> R<i32> {
    if !(0..FIELDS as i32).contains(&field) {
        return Err(refuse("invalid", 108, "unknown field"));
    }
    if lo > hi {
        return Err(refuse("range", 101, "empty range lo>hi"));
    }
    let f = field as usize;
    Ok(rows.iter().filter(|r| in_range(r, f, lo, hi)).count() as i32)
}

fn sum_matching(rows: &[[i32; FIELDS]], field: i32, lo: i32, hi: i32) -> R<i32> {
    if !(0..FIELDS as i32).contains(&field) {
        return Err(refuse("invalid", 108, "unknown field"));
    }
    if lo > hi {
        return Err(refuse("range", 101, "empty range lo>hi"));
    }
    let f = field as usize;
    let mut total = 0i32;
    for row in rows {
        if !in_range(row, f, lo, hi) {
            continue;
        }
        match total.checked_add(row[COL_AMT]) {
            Some(next) => total = next,
            None => return Err(refuse("range", 101, "sum overflow")),
        }
    }
    Ok(total)
}

fn list_matching(rows: &[[i32; FIELDS]], field: usize, lo: i32, hi: i32) {
    for row in rows {
        if in_range(row, field, lo, hi) {
            println!("TQ row {} {} {}", row[COL_ID], row[COL_GRP], row[COL_AMT]);
        }
    }
}

fn command(
    rows: &mut Vec<[i32; FIELDS]>,
    ids: &mut HashSet<i32>,
    cmd: &str,
    a: Option<&str>,
    b: Option<&str>,
    c: Option<&str>,
    ctr: &mut Counters,
) -> i32 {
    if cmd == "row" {
        ctr.ops += 1;
        let (x, y, want) = match (a, b, c) {
            (Some(a), Some(b), Some(c)) => {
                match (parse_int(a), parse_int(b), parse_int(c)) {
                    (Some(x), Some(y), Some(want)) => (x, y, want),
                    _ => {
                        ctr.refused += 1;
                        println!("TQ row REFUSED 108 row needs id grp amt integers");
                        return 108;
                    }
                }
            }
            _ => {
                ctr.refused += 1;
                println!("TQ row REFUSED 108 row needs id grp amt integers");
                return 108;
            }
        };
        if ids.contains(&x) {
            ctr.refused += 1;
            println!("TQ row {} REFUSED 108 invalid duplicate id", x);
            return 108;
        }
        ids.insert(x);
        rows.push([x, y, want]);
        ctr.ok += 1;
        println!("TQ row {} OK", x);
        return 0;
    }

    let predicate_cmds = matches!(
        cmd,
        "eq" | "range"
            | "count"
            | "sum"
            | "expect_count"
            | "expect_sum"
            | "expect_sum_refusal"
    );
    if predicate_cmds {
        ctr.ops += 1;
        let field = a.map(field_index).unwrap_or(-1);
        let x = b.and_then(parse_int);
        let want = c.and_then(parse_int);
        let needs_c = cmd == "range" || cmd == "expect_sum_refusal";
        let (x, want) = if field < 0 || x.is_none() || (needs_c && want.is_none()) {
            ctr.refused += 1;
            println!("TQ {} REFUSED 108 bad field or predicate operand", cmd);
            return 108;
        } else {
            (x.unwrap(), want)
        };
        let f = field;
        let y = if cmd == "range" { want.unwrap() } else { x };

        if cmd == "eq" || cmd == "range" {
            match count_matching(rows, f, x, y) {
                Ok(matched) => {
                    ctr.ok += 1;
                    if cmd == "eq" {
                        println!("TQ eq {} {} {}", a.unwrap(), x, matched);
                    } else {
                        println!("TQ range {} {} {} {}", a.unwrap(), x, y, matched);
                    }
                    list_matching(rows, f as usize, x, y);
                    0
                }
                Err(e) => {
                    ctr.refused += 1;
                    println!("TQ {} REFUSED {} {} {}", cmd, e.code, e.category, e.msg);
                    e.code
                }
            }
        } else if cmd == "count" {
            match count_matching(rows, f, x, y) {
                Ok(value) => {
                    ctr.ok += 1;
                    println!("TQ count {} {} {}", a.unwrap(), x, value);
                    0
                }
                Err(e) => {
                    ctr.refused += 1;
                    println!("TQ count REFUSED {} {} {}", e.code, e.category, e.msg);
                    e.code
                }
            }
        } else if cmd == "sum" {
            match sum_matching(rows, f, x, y) {
                Ok(value) => {
                    ctr.ok += 1;
                    println!("TQ sum {} {} {}", a.unwrap(), x, value);
                    0
                }
                Err(e) => {
                    ctr.refused += 1;
                    println!("TQ sum REFUSED {} {} {}", e.code, e.category, e.msg);
                    e.code
                }
            }
        } else if cmd == "expect_count" || cmd == "expect_sum" {
            let want = match want {
                Some(w) => w,
                None => {
                    ctr.failed += 1;
                    println!("TQ {} BAD_INPUT", cmd);
                    return 108;
                }
            };
            let result = if cmd == "expect_count" {
                count_matching(rows, f, x, y)
            } else {
                sum_matching(rows, f, x, y)
            };
            match result {
                Ok(value) if value == want => {
                    ctr.ok += 1;
                    println!("TQ {} {} {} OK", cmd, a.unwrap(), x);
                    0
                }
                Ok(value) => {
                    ctr.failed += 1;
                    println!("TQ {} {} {} FAIL {} {}", cmd, a.unwrap(), x, value, want);
                    101
                }
                Err(e) => {
                    ctr.failed += 1;
                    println!("TQ {} ERROR {}", cmd, e.code);
                    e.code
                }
            }
        } else {
            /* expect_sum_refusal */
            let want = want.unwrap();
            let code = match sum_matching(rows, f, x, y) {
                Ok(_) => 0,
                Err(e) => e.code,
            };
            if code == want {
                ctr.ok += 1;
                println!("TQ expect_sum_refusal {} {} OK", a.unwrap(), x);
                0
            } else {
                ctr.failed += 1;
                println!("TQ expect_sum_refusal {} {} FAIL {} {}", a.unwrap(), x, code, want);
                101
            }
        }
    } else if cmd == "check" {
        ctr.ops += 1;
        let want = match a.and_then(parse_int) {
            Some(w) => w,
            None => {
                ctr.failed += 1;
                println!("TQ check BAD_INPUT");
                return 108;
            }
        };
        let value = rows.len() as i32;
        if value == want {
            ctr.ok += 1;
            println!("TQ check {} OK", want);
            0
        } else {
            ctr.failed += 1;
            println!("TQ check {} FAIL {}", want, value);
            101
        }
    } else {
        ctr.ops += 1;
        ctr.refused += 1;
        println!("TQ {} REFUSED 108 unknown command", cmd);
        108
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut rows: Vec<[i32; FIELDS]> = Vec::new();
    let mut ids: HashSet<i32> = HashSet::new();
    let mut ctr = Counters::new();

    let mut idx = 0usize;
    while idx < args.len() {
        let cmd = args[idx].as_str();
        let mut used = if cmd == "row"
            || cmd == "range"
            || cmd == "expect_count"
            || cmd == "expect_sum"
            || cmd == "expect_sum_refusal"
        {
            4
        } else if cmd == "eq" || cmd == "count" || cmd == "sum" {
            3
        } else if cmd == "check" {
            2
        } else {
            1
        };
        if idx + used > args.len() {
            used = 1;
        }
        command(
            &mut rows,
            &mut ids,
            cmd,
            args.get(idx + 1).map(|s| s.as_str()),
            args.get(idx + 2).map(|s| s.as_str()),
            args.get(idx + 3).map(|s| s.as_str()),
            &mut ctr,
        );
        idx += used;
    }

    println!("TQ_SUMMARY {} {} {} {}", ctr.ops, ctr.ok, ctr.refused, ctr.failed);
    std::process::exit(if ctr.failed == 0 { 0 } else { 1 });
}
