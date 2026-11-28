//! Rust equivalent of `benchmarks/pointer_chase.c`.
//!
//! Deterministic stdout: `chase 24999975000000`
//!
//! SAFETY / WHY `unsafe`: this benchmark exists to measure pointer chasing,
//! so it uses a real linked ring of raw pointers rather than an index-based
//! (bounds-checked) Vec walk, which would measure something different. All
//! dereferences below are sound:
//!   * `nodes` is filled to capacity before any address is taken and is never
//!     resized afterwards, so element addresses remain valid for the whole run.
//!   * every `next` is built from a reference to a live element of `nodes`.
//!   * traversal only follows those in-range pointers around the ring.

use std::ptr;

const NODES: usize = 1_000_000;
const ROUNDS: usize = 50;

struct Node {
    next: *const Node,
    value: u32,
}

fn main() {
    let mut nodes: Vec<Node> = Vec::with_capacity(NODES);
    for i in 0..NODES {
        nodes.push(Node {
            next: ptr::null(),
            value: i as u32,
        });
    }

    for i in 0..NODES {
        let next_idx = (i + 1) % NODES;
        // The cast to a raw pointer ends the shared borrow, allowing the
        // mutable write to nodes[i].next without a borrow-checker conflict.
        let next_ptr: *const Node = &nodes[next_idx] as *const Node;
        nodes[i].next = next_ptr;
    }

    let mut p: *const Node = &nodes[0] as *const Node;
    let mut sum: u64 = 0;
    for _ in 0..ROUNDS {
        for _ in 0..NODES {
            // SAFETY: `p` always points to a live element of `nodes`, and
            // `next` always points to another live element of that same,
            // never-resized Vec (the ring wraps via `% NODES`).
            unsafe {
                sum += (*p).value as u64;
                p = (*p).next;
            }
        }
    }

    println!("chase {}", sum);
}
