/* Safe-Rust equivalent of examples/expanded/engine/resource_cache.cp.
 *
 * The C+ original uses per-slot state + generation metadata and generation-
 * tagged integer handles to refuse double-release / use-after-release.  This
 * model uses owned Vec<Asset> slots plus a HashMap id->slot index; the heap
 * payload is an owned [u8;8] value, so no raw pointer or unsafe code is needed.
 * Refusals carry the same category/code/message as the error* API.
 */

use std::collections::HashMap;

const MAX_ASSETS: usize = 8;
const ID_MAX: usize = 32;
const PAYLOAD_BYTES: usize = 8;
const HANDLE_STRIDE: i32 = 16;

const STATE_EMPTY: i32 = 0;
const STATE_LIVE: i32 = 1;
const STATE_RELEASED: i32 = 2;

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

struct Asset {
    id: String,
    state: i32,
    refs: i32,
    gen: i32,
    payload: [u8; PAYLOAD_BYTES],
}

impl Asset {
    fn empty() -> Self {
        Asset {
            id: String::new(),
            state: STATE_EMPTY,
            refs: 0,
            gen: 0,
            payload: [0; PAYLOAD_BYTES],
        }
    }
}

struct Cache {
    index: HashMap<String, usize>,
    assets: Vec<Asset>,
    used: usize,
}

impl Cache {
    fn new() -> Self {
        Cache {
            index: HashMap::new(),
            assets: (0..MAX_ASSETS).map(|_| Asset::empty()).collect(),
            used: 0,
        }
    }

    fn valid_id(id: &str) -> R<()> {
        if id.is_empty() || id.len() >= ID_MAX {
            return Err(refuse("range", 101, "asset id length out of range"));
        }
        Ok(())
    }

    fn load(&mut self, id: &str) -> R<usize> {
        Self::valid_id(id)?;
        if self.index.contains_key(id) {
            return Err(refuse("invalid", 106, "asset id is already loaded"));
        }
        let slot = match self.assets.iter().position(|a| a.state == STATE_EMPTY) {
            Some(s) => s,
            None => return Err(refuse("range", 107, "cache is full")),
        };
        let length = id.len() as u8;
        let payload = {
            let mut p = [0u8; PAYLOAD_BYTES];
            for (k, byte) in p.iter_mut().enumerate() {
                *byte = length.wrapping_add((k as u8).wrapping_mul(7));
            }
            p
        };
        let a = &mut self.assets[slot];
        a.payload = payload;
        a.id = id.to_string();
        a.state = STATE_LIVE;
        a.refs = 0;
        a.gen = 0;
        if slot >= self.used {
            self.used = slot + 1;
        }
        self.index.insert(id.to_string(), slot);
        println!("LOAD id= {} slot= {}", id, slot);
        Ok(slot)
    }

    fn acquire(&mut self, id: &str) -> R<i32> {
        Self::valid_id(id)?;
        let slot = match self.index.get(id) {
            Some(s) => *s,
            None => return Err(refuse("invalid", 106, "asset id is not loaded")),
        };
        let a = &mut self.assets[slot];
        if a.state != STATE_LIVE {
            return Err(refuse("lifecycle", 103, "asset metadata is not live"));
        }
        if a.gen >= HANDLE_STRIDE - 1 {
            return Err(refuse("range", 101, "handle generation exhausted"));
        }
        a.refs += 1;
        a.gen += 1;
        let handle = slot as i32 * HANDLE_STRIDE + a.gen;
        println!("ACQUIRE id= {} handle= {} refs= {}", id, handle, a.refs);
        Ok(handle)
    }

    fn resolve(&self, handle: i32, op: &'static str) -> R<usize> {
        let slot = (handle / HANDLE_STRIDE) as usize;
        let gen = handle % HANDLE_STRIDE;
        if slot >= self.used {
            return Err(refuse("invalid", 106, "handle slot is outside the cache"));
        }
        let a = &self.assets[slot];
        if a.state == STATE_EMPTY {
            return Err(refuse("invalid", 106, "handle was never loaded"));
        }
        if a.state == STATE_RELEASED {
            return Err(refuse("lifecycle", 103, op));
        }
        if gen != a.gen {
            return Err(refuse("lifecycle", 103, "stale handle generation"));
        }
        Ok(slot)
    }

    fn use_asset(&self, handle: i32) -> R<()> {
        let slot = self.resolve(handle, "use after release")?;
        let a = &self.assets[slot];
        println!("USE id= {} byte0= {}", a.id, a.payload[0]);
        Ok(())
    }

    fn release(&mut self, handle: i32) -> R<()> {
        let slot = self.resolve(handle, "double release")?;
        let a = &mut self.assets[slot];
        if a.refs <= 0 {
            return Err(refuse("ownership", 106, "reference count underflow"));
        }
        a.refs -= 1;
        if a.refs > 0 {
            println!("RELEASE id= {} handle= {} refs= {}", a.id, handle, a.refs);
            return Ok(());
        }
        a.state = STATE_RELEASED;
        a.payload = [0; PAYLOAD_BYTES];
        self.index.remove(&a.id);
        println!("UNLOAD id= {} slot= {}", a.id, slot);
        Ok(())
    }
}

fn report(label: &str, err: Refusal) {
    println!(
        "REFUSE {} ok= 0 category= {} code= {} msg= {}",
        label, err.category, err.code, err.msg
    );
}

fn main() {
    let mut cache = Cache::new();
    let derived = format!("asset_{}", "01");

    println!("CACHE max= 8 payload= 8 derived= {}", derived);
    if cache.load("tex").is_err()
        || cache.load("snd").is_err()
        || cache.load(&derived).is_err()
    {
        println!("FATAL: load failed");
        std::process::exit(2);
    }
    if let Err(e) = cache.load("tex") {
        report("duplicate-load", e);
    }

    let h1 = match cache.acquire("tex") {
        Ok(h) => h,
        Err(e) => {
            println!("FATAL: acquire failed: {}", e.msg);
            std::process::exit(2);
        }
    };
    let h2 = match cache.acquire("tex") {
        Ok(h) => h,
        Err(e) => {
            println!("FATAL: acquire failed: {}", e.msg);
            std::process::exit(2);
        }
    };

    if let Err(e) = cache.release(h1) {
        report("stale-handle", e);
    }
    let _ = cache.use_asset(h2);
    let _ = cache.release(h2);
    let _ = cache.release(h2);

    if let Err(e) = cache.release(h2) {
        report("double-release", e);
    }
    if let Err(e) = cache.use_asset(h2) {
        report("use-after-release", e);
    }
    if let Err(e) = cache.acquire("tex") {
        report("not-loaded", e);
    }
    if let Err(e) = cache.acquire("missing") {
        report("unknown-id", e);
    }

    if let Err(e) = cache.load("tex") {
        println!("FATAL: reload failed: {}", e.msg);
        std::process::exit(2);
    }
    let h3 = match cache.acquire("tex") {
        Ok(h) => h,
        Err(e) => {
            println!("FATAL: reacquire failed: {}", e.msg);
            std::process::exit(2);
        }
    };
    let _ = cache.use_asset(h3);
    let _ = cache.release(h3);

    println!("SUMMARY used= {} index= {}", cache.used, cache.index.len());
}
