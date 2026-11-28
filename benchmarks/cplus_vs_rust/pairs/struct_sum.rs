// Paired benchmark: struct traversal with an opaque stride (Rust side).
use std::hint::black_box;

const NODES: usize = 64;
const ITERATIONS: i64 = 20_000_000;

#[derive(Clone)]
#[repr(C)]
struct Node {
    value: i64,
    pad: [i64; 7], // make each node one cache line
}

fn main() {
    let seed = black_box(1usize);
    let stride = seed | 1; // odd stride keeps the ring traversal full-cycle
    let mut nodes = vec![
        Node {
            value: 0,
            pad: [0; 7],
        };
        NODES
    ];
    for (i, node) in nodes.iter_mut().enumerate() {
        node.value = (i * 7) as i64;
    }
    let mut total: i64 = 0;
    let mut at = 0usize;
    for _ in 0..ITERATIONS {
        total += nodes[at].value;
        at = (at + stride) % NODES;
    }
    println!("struct_sum {}", total);
}
