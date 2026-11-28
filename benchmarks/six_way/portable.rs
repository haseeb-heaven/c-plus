// Rust implementation of the shared six-way workload. The checksums must
// match portable.c, portable.goose and the Fil-C build of portable.c.
use std::env;
use std::time::Instant;

const SCALE_DEFAULT: u64 = 100;
const STREAM_PER_SCALE: u64 = 5_000_000;
const ITEMS_PER_SCALE: u64 = 200_000;
const BUCKETS: usize = 8192;
const ARENA_BYTES: usize = 1 << 20;
const RECORD_BYTES: usize = 64;
const ARENA_ROUNDS_PER_SCALE: u64 = 200;

struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9e3779b97f4a7c15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
        z ^ (z >> 31)
    }
}

fn mix(h: u64, v: u64) -> u64 {
    (h ^ v).wrapping_mul(0x100000001b3)
}

fn main() {
    let scale = env::var("CPLUS_SIX_SCALE")
        .ok().and_then(|s| s.parse::<u64>().ok()).unwrap_or(SCALE_DEFAULT);
    let mut rng = Rng(0x9e3779b97f4a7c15);

    let t0 = Instant::now();
    let mut stream = 0xcbf29ce484222325u64;
    for _ in 0..STREAM_PER_SCALE * scale {
        stream = mix(stream, rng.next());
    }
    println!("PHASE stream {} {}", stream, t0.elapsed().as_nanos());

    let t0 = Instant::now();
    let items = (ITEMS_PER_SCALE * scale) as usize;
    let mut buckets = vec![u32::MAX; BUCKETS];
    let mut keys: Vec<u64> = Vec::with_capacity(items);
    let mut values: Vec<u64> = Vec::with_capacity(items);
    let mut next: Vec<u32> = Vec::with_capacity(items);
    for i in 0..items {
        let key = rng.next();
        let b = (key % BUCKETS as u64) as usize;
        keys.push(key);
        values.push(i as u64);
        next.push(buckets[b]);
        buckets[b] = i as u32;
    }
    let mut hash = 0x84222325cbf29ce4u64;
    for i in 0..items {
        let mut cursor = buckets[(keys[i] % BUCKETS as u64) as usize];
        while cursor != u32::MAX && keys[cursor as usize] != keys[i] {
            cursor = next[cursor as usize];
        }
        if cursor != u32::MAX {
            hash = mix(hash, values[cursor as usize]);
        }
    }
    println!("PHASE hash {} {}", hash, t0.elapsed().as_nanos());

    let t0 = Instant::now();
    let mut arena = 0x517cc1b727220a95u64;
    let mut block = vec![0u8; ARENA_BYTES];
    for _ in 0..ARENA_ROUNDS_PER_SCALE * scale {
        for chunk in block.chunks_mut(RECORD_BYTES) {
            let fill = rng.next() as u8;
            for b in chunk.iter_mut() { *b = fill; }
            arena = mix(arena, chunk[0] as u64);
        }
    }
    println!("PHASE arena {} {}", arena, t0.elapsed().as_nanos());
    println!("TOTAL {} {} {}", stream, hash, arena);
}
