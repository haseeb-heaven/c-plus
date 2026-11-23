/* Safe-Rust equivalent of examples/expanded/database/safety_demo.cp.
 *
 * The original feeds hostile keys/values into fixed-width vector records and
 * checks a 32-byte canary around each refused operation.  This model uses an
 * owned Vec<(String, String)> and a guarded canary array; i32::checked_add
 * refuses the overflowing SUM before it happens. No unsafe, no FFI.
 *
 * The canary here is a modelled invariant: Rust's owned containers cannot be
 * overrun by these operations, so the check is an explicit representation of
 * the C+ demo's observation, not a real guard page. See RESULTS.md.
 */

const KEY_MAX: usize = 16;
const VAL_MAX: usize = 16;
const CANARY_LEN: usize = 32;

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

type Store = Vec<(String, String)>;

fn find(store: &Store, key: &str) -> Option<usize> {
    store.iter().position(|(k, _)| k == key)
}

fn put(store: &mut Store, key: Option<&str>, value: Option<&str>) -> R<()> {
    let key = key.ok_or_else(|| refuse("invalid", 108, "null key"))?;
    let value = value.ok_or_else(|| refuse("invalid", 108, "null value"))?;
    if key.is_empty() || key.len() > KEY_MAX {
        return Err(refuse("range", 101, "key length out of bounds"));
    }
    if value.len() > VAL_MAX {
        return Err(refuse("range", 101, "value length out of bounds"));
    }
    if find(store, key).is_some() {
        return Err(refuse("invalid", 108, "duplicate key"));
    }
    store.push((key.to_string(), value.to_string()));
    Ok(())
}

fn delete(store: &mut Store, key: Option<&str>) -> R<()> {
    let key = key.ok_or_else(|| refuse("invalid", 108, "null key"))?;
    if key.is_empty() || key.len() > KEY_MAX {
        return Err(refuse("range", 101, "key length out of bounds"));
    }
    match find(store, key) {
        Some(i) => {
            store.remove(i);
            Ok(())
        }
        None => Err(refuse("invalid", 108, "no such key")),
    }
}

fn sum(amounts: &[i32]) -> R<()> {
    let mut sum = 0i32;
    for &amt in amounts {
        match sum.checked_add(amt) {
            Some(next) => sum = next,
            None => return Err(refuse("range", 101, "sum overflow")),
        }
    }
    Ok(())
}

fn get(store: &Store, key: &str) -> R<String> {
    match find(store, key) {
        Some(i) => Ok(store[i].1.clone()),
        None => Err(refuse("invalid", 108, "no such key")),
    }
}

fn expected(i: usize) -> u8 {
    0xA5u8 ^ i as u8
}

fn canary_ok(canary: &[u8; CANARY_LEN]) -> bool {
    canary.iter().enumerate().all(|(i, b)| *b == expected(i))
}

struct Report {
    cases: i32,
    refused: i32,
    canary_ok_count: i32,
    failed: i32,
}

impl Report {
    fn new() -> Self {
        Report { cases: 0, refused: 0, canary_ok_count: 0, failed: 0 }
    }
    fn emit(&mut self, op: &str, expected_code: i32, err: Refusal, canary: &[u8; CANARY_LEN]) {
        self.cases += 1;
        if err.code == expected_code {
            self.refused += 1;
            println!("SAFETY {} REFUSED {}", op, err.code);
            println!("SAFETY {} DETAIL {} {}", op, err.category, err.msg);
        } else {
            self.failed += 1;
            println!("SAFETY {} FAIL {} {} {}", op, err.code, expected_code, err.msg);
        }
        if canary_ok(canary) {
            self.canary_ok_count += 1;
            println!("SAFETY {} CANARY ok", op);
        } else {
            self.failed += 1;
            println!("SAFETY {} CANARY broken", op);
        }
    }
}

fn main() {
    let mut store: Store = Vec::new();
    let mut canary = [0u8; CANARY_LEN];
    for (i, b) in canary.iter_mut().enumerate() {
        *b = expected(i);
    }
    let mut rep = Report::new();

    /* 1. oversized key */
    let big = "0123456789012345678901234567890123456789";
    if let Err(e) = put(&mut store, Some(big), Some("v")) {
        rep.emit("oversized_key", 101, e, &canary);
    }

    /* 2. NULL value */
    if let Err(e) = put(&mut store, Some("good"), None) {
        rep.emit("null_value", 108, e, &canary);
    }

    /* 3. overflowing SUM: INT_MAX + 1 */
    if let Err(e) = sum(&[1, i32::MAX, 1, 1]) {
        rep.emit("overflowing_sum", 101, e, &canary);
    }

    /* 4. duplicate key */
    if put(&mut store, Some("dup"), Some("one")).is_ok() {
        if let Err(e) = put(&mut store, Some("dup"), Some("two")) {
            rep.emit("duplicate_key", 108, e, &canary);
        }
    }

    /* 5. delete-missing */
    if let Err(e) = delete(&mut store, Some("absent")) {
        rep.emit("delete_missing", 108, e, &canary);
    }

    match get(&store, "dup") {
        Ok(value) if value == "one" => println!("SAFETY FINAL_VALUE dup one"),
        Ok(value) => {
            println!("SAFETY FINAL_VALUE FAIL 0 {}", value);
            rep.failed += 1;
        }
        Err(e) => {
            println!("SAFETY FINAL_VALUE FAIL {} ", e.code);
            rep.failed += 1;
        }
    }
    if canary_ok(&canary) {
        println!("SAFETY FINAL_CANARY ok");
    } else {
        println!("SAFETY FINAL_CANARY broken");
        rep.failed += 1;
    }
    println!("SAFETY FINAL_STORE {}", store.len());
    println!(
        "SAFETY_SUMMARY {} {} {} {}",
        rep.cases, rep.refused, rep.canary_ok_count, rep.failed
    );
    std::process::exit(if rep.failed == 0 { 0 } else { 1 });
}
