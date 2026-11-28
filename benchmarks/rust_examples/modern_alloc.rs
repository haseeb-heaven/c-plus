/* Safe-Rust model of the C+ Goose-inspired memory model.
 * No unsafe FFI: models the OBSERVABLE outcomes (bump growth, scope rewind,
 * slot reuse, stale/double-free refusal) with safe Rust primitives.
 * MATCH means outcome parity on these fixed inputs, NOT equivalent guarantees.
 */
use std::collections::VecDeque;

#[derive(Clone, Copy)]
struct SlotRef {
    index: usize,
    generation: u64,
}

struct BumpStack {
    data: Vec<u8>,
    top: usize,
    generation: u64,
}

impl BumpStack {
    fn new(reserve: usize) -> Self {
        // Vec is the safe-Rust stand-in for mmap reservation; capacity == reserve.
        let mut v = Vec::with_capacity(reserve);
        v.resize(reserve, 0);
        BumpStack { data: v, top: 0, generation: 1 }
    }
    fn alloc(&mut self, n: usize, align: usize) -> Option<usize> {
        if n == 0 || !align.is_power_of_two() { return None; }
        let p = self.top;
        let pad = (align - (p & (align - 1))) & (align - 1);
        let start = p.checked_add(pad)?;
        let end = start.checked_add(n)?;
        if end > self.data.len() { return None; }
        self.data[start..end].fill(0);
        self.top = end;
        Some(start)
    }
    fn realloc_in_place(&mut self, ptr: usize, old: usize, new: usize) -> bool {
        if ptr + old != self.top || new < old { return false; }
        self.top = ptr + new;
        true
    }
    fn rewind(&mut self, saved: usize, gen: u64) -> bool {
        if gen != self.generation { return false; }
        self.top = saved;
        self.generation = self.generation.wrapping_add(1);
        true
    }
}

struct SlotPool {
    items: Vec<Vec<u8>>,
    free: VecDeque<usize>,
    generations: Vec<u64>,
}

impl SlotPool {
    fn new(slot_size: usize, initial: usize) -> Self {
        let mut items = Vec::with_capacity(initial);
        let mut generations = Vec::with_capacity(initial);
        for _ in 0..initial { items.push(vec![0u8; slot_size]); generations.push(0); }
        SlotPool { items, free: VecDeque::new(), generations }
    }
    fn alloc_ref(&mut self) -> SlotRef {
        let idx = if let Some(i) = self.free.pop_front() {
            i
        } else {
            let i = self.items.len();
            self.items.push(vec![0u8; self.items[0].len()]);
            self.generations.push(0);
            i
        };
        let g = self.generations[idx].wrapping_add(1);
        self.generations[idx] = if g == 0 { 1 } else { g };
        self.items[idx].fill(0);
        SlotRef { index: idx, generation: g }
    }
    fn get(&self, r: &SlotRef) -> Option<&[u8]> {
        if r.index >= self.items.len() || self.generations[r.index] != r.generation {
            return None; // stale refusal (103 analogue)
        }
        Some(&self.items[r.index])
    }
    fn free_ref(&mut self, r: &SlotRef) -> i32 {
        if r.index >= self.items.len() || self.generations[r.index] != r.generation {
            return 104; // double-free refusal
        }
        self.free.push_back(r.index);
        self.generations[r.index] = self.generations[r.index].wrapping_add(1);
        self.items[r.index].fill(0);
        0
    }
}

fn main() {
    let mut ds = BumpStack::new(32 * 1024 * 1024);
    let saved = ds.top;
    let gen = ds.generation;
    let a = ds.alloc(4096, 16);
    let b = ds.alloc(2 * 1024 * 1024, 8);
    let in_place = match b { Some(p) => ds.realloc_in_place(p, 2 * 1024 * 1024,
                                                            2 * 1024 * 1024 + 64),
                             None => false };
    let rewound = ds.rewind(saved, gen);
    println!("VM reserve={} committed_on_alloc={} align16={} grow={} in_place={} rewind={}",
             ds.data.len(), ds.top, a.map(|p| p & 15 == 0).unwrap_or(false),
             b.is_some(), in_place, rewound && ds.top == 0);

    let mut pool = SlotPool::new(32, 4);
    let r1 = pool.alloc_ref();
    let r2 = pool.alloc_ref();
    let get_ok = pool.get(&r1).is_some();
    let free_rc = pool.free_ref(&r1);
    let stale = SlotRef { index: r1.index, generation: r1.generation };
    let stale_rc = pool.get(&stale).is_none();
    let double_rc = pool.free_ref(&stale);
    let r3 = pool.alloc_ref();
    let reused = r3.index == r1.index && r3.generation != r1.generation;
    println!("SLOT idx={},{} get={} free={} stale_refused={} double={} reuse={}",
             r1.index, r2.index, get_ok as i32, free_rc, stale_rc as i32, double_rc,
             reused as i32);
    println!("RUST_MODEL_OK");
}
