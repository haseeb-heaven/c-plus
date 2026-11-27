// Identical workload to bench.cp / bench.rs: 2M iterations of integer mix
// plus bounded updates over a two-field struct. Compiled with -O ReleaseFast.
// Expected checksum: 16776346274400.
const std = @import("std");

const P = struct { x: i64, y: i64 };

pub fn main() void {
    const N: i64 = 2_000_000;
    var p = P{ .x = 1, .y = 2 };
    var acc: u64 = 0;

    const t0 = std.time.nanoTimestamp();
    var i: i64 = 0;
    while (i < N) : (i += 1) {
        p.x = (p.x * 3 + p.y) & 0xffffff;
        p.y = (p.y + i * 7) & 0xffffff;
        acc +%= @as(u64, @intCast(p.x ^ p.y));
    }
    const t1 = std.time.nanoTimestamp();

    const out = std.io.getStdOut().writer();
    out.print("ZIG {d} {d}\n", .{ t1 - t0, acc }) catch {};
    if (acc == 0) std.process.exit(1);
}
