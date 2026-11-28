/* Safe-Rust equivalent of examples/expanded/database/kv_store.cp.
 *
 * The C+ original packs fixed-width records into a stdlib::vector of ints and
 * routes every access through at_checked/erase/reserve.  This model uses an
 * owned Vec<(String, String)> (insertion order preserved so `list` matches),
 * with Result-based refusals carrying the same stable category/code/message.
 * No unsafe, no FFI, no raw pointers.
 */

const KEY_MAX: usize = 16;
const VAL_MAX: usize = 24;

#[derive(Clone, Copy)]
struct Refusal {
    category: &'static str,
    code: i32,
    msg: &'static str,
}

type R<T> = Result<T, Refusal>;

fn refuse(category: &'static str, code: i32, msg: &'static str) -> Refusal {
    Refusal { category, code, msg }
}

fn check_key(key: &str) -> R<()> {
    if key.is_empty() {
        return Err(refuse("invalid", 108, "empty or invalid key"));
    }
    if key.len() > KEY_MAX {
        return Err(refuse("range", 101, "key exceeds KV_KEY_MAX"));
    }
    Ok(())
}

fn check_value(value: &str) -> R<()> {
    if value.len() > VAL_MAX {
        return Err(refuse("range", 101, "value exceeds KV_VAL_MAX"));
    }
    Ok(())
}

struct Counters {
    ops: i32,
    ok: i32,
    refused: i32,
    failed: i32,
}

impl Counters {
    fn new() -> Self {
        Counters {
            ops: 0,
            ok: 0,
            refused: 0,
            failed: 0,
        }
    }
}

type Store = Vec<(String, String)>;

fn find(store: &Store, key: &str) -> Option<usize> {
    store.iter().position(|(k, _)| k == key)
}

fn put(store: &mut Store, key: &str, value: &str) -> R<()> {
    check_key(key)?;
    check_value(value)?;
    if find(store, key).is_some() {
        return Err(refuse("invalid", 108, "duplicate key"));
    }
    store.push((key.to_string(), value.to_string()));
    Ok(())
}

fn get(store: &Store, key: &str) -> R<String> {
    check_key(key)?;
    match find(store, key) {
        Some(i) => Ok(store[i].1.clone()),
        None => Err(refuse("invalid", 108, "no such key")),
    }
}

fn update(store: &mut Store, key: &str, value: &str) -> R<()> {
    check_key(key)?;
    check_value(value)?;
    match find(store, key) {
        Some(i) => {
            store[i].1 = value.to_string();
            Ok(())
        }
        None => Err(refuse("invalid", 108, "no such key")),
    }
}

fn delete(store: &mut Store, key: &str) -> R<()> {
    check_key(key)?;
    match find(store, key) {
        Some(i) => {
            store.remove(i);
            Ok(())
        }
        None => Err(refuse("invalid", 108, "no such key")),
    }
}

fn list(store: &Store) {
    for (i, (k, v)) in store.iter().enumerate() {
        println!("KV item {} {} {}", i, k, v);
    }
    println!("KV list {}", store.len());
}

fn parse_int(text: &str) -> Option<i32> {
    text.parse::<i32>().ok()
}

/* Execute one command from argv, mirroring kv_command()'s counters/printing. */
fn command(store: &mut Store, args: &[String], idx: usize, used: usize, c: &mut Counters) -> i32 {
    let cmd = args[idx].as_str();
    let a = args.get(idx + 1).map(|s| s.as_str());
    let b = args.get(idx + 2).map(|s| s.as_str());

    let _ = used; // consumption is performed by the caller

    if cmd == "insert" || cmd == "update" {
        c.ops += 1;
        let (a, b) = match (a, b) {
            (Some(a), Some(b)) => (a, b),
            _ => {
                let e = refuse("invalid", 108, "missing key or value operand");
                c.refused += 1;
                println!("KV {} REFUSED {} {}", cmd, e.code, e.msg);
                return e.code;
            }
        };
        let result = if cmd == "insert" {
            put(store, a, b)
        } else {
            update(store, a, b)
        };
        match result {
            Ok(()) => {
                c.ok += 1;
                println!("KV {} OK {}", cmd, a);
                0
            }
            Err(e) => {
                c.refused += 1;
                println!("KV {} REFUSED {} {} {}", cmd, e.code, e.category, e.msg);
                e.code
            }
        }
    } else if cmd == "get" || cmd == "delete" {
        c.ops += 1;
        let a = match a {
            Some(a) => a,
            None => {
                let e = refuse("invalid", 108, "missing key operand");
                c.refused += 1;
                println!("KV {} REFUSED {} {}", cmd, e.code, e.msg);
                return e.code;
            }
        };
        if cmd == "get" {
            match get(store, a) {
                Ok(value) => {
                    c.ok += 1;
                    println!("KV get {} {}", a, value);
                    0
                }
                Err(e) => {
                    c.refused += 1;
                    println!("KV get REFUSED {} {} {}", e.code, e.category, e.msg);
                    e.code
                }
            }
        } else {
            match delete(store, a) {
                Ok(()) => {
                    c.ok += 1;
                    println!("KV delete {} OK", a);
                    0
                }
                Err(e) => {
                    c.refused += 1;
                    println!("KV delete REFUSED {} {} {}", e.code, e.category, e.msg);
                    e.code
                }
            }
        }
    } else if cmd == "list" {
        c.ops += 1;
        c.ok += 1;
        list(store);
        0
    } else if cmd == "expect_size" || cmd == "size" {
        c.ops += 1;
        let actual = store.len() as i32;
        if cmd == "size" {
            c.ok += 1;
            println!("KV size {}", actual);
            return 0;
        }
        let want = match a.and_then(parse_int) {
            Some(w) => w,
            None => {
                c.failed += 1;
                println!("KV expect_size BAD_INPUT {}", actual);
                return 108;
            }
        };
        if want == actual {
            c.ok += 1;
            println!("KV expect_size {} OK", want);
            0
        } else {
            c.failed += 1;
            println!("KV expect_size {} FAIL {}", want, actual);
            101
        }
    } else {
        c.ops += 1;
        c.refused += 1;
        let e = refuse("invalid", 108, "unknown command");
        println!("KV {} REFUSED {} {}", cmd, e.code, e.msg);
        e.code
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut store: Store = Vec::new();
    let mut counters = Counters::new();

    let mut idx = 0usize;
    while idx < args.len() {
        let cmd = args[idx].as_str();
        let mut used = if cmd == "insert" || cmd == "update" {
            3
        } else if cmd == "get"
            || cmd == "delete"
            || cmd == "expect_size"
            || cmd == "size"
        {
            2
        } else {
            1
        };
        if idx + used > args.len() {
            used = 1;
        }
        command(&mut store, &args, idx, used, &mut counters);
        idx += used;
    }

    println!(
        "KV_SUMMARY {} {} {} {}",
        counters.ops, counters.ok, counters.refused, counters.failed
    );
    std::process::exit(if counters.failed == 0 { 0 } else { 1 });
}
