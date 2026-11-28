// Same 2M-iteration integer/struct workload as bench.cp, bench.rs and bench.zig.
import std;
import os;

let N = 2000000;

fn main() {
    var x: i64 = 1;
    var y: i64 = 2;
    var acc: u64 = 0;
    let t0 = clock();
    for i in 0..N {
        x = (x * 3 + y) & 0xffffff;
        y = (y + i * 7) & 0xffffff;
        acc = acc + ((x ^ y) as u64);
    }
    let t1 = clock();
    let ns = ((t1 - t0) * 1000000000.0) as i64;
    print("GOOSE ", ns, " ", acc);
}
