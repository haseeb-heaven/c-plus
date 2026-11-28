// Goose implementation of the shared six-way workload. The checksums must
// match portable.c, portable.rs and the Fil-C build of portable.c.
// Wall-clock timing is measured by the runner; Goose prints exact checksums.
fn mix(h: u64, v: u64) -> u64 {
    return (h ^ v) * 0x100000001b3;
}

fn main() {
    // The runner replaces the literal below to match CPLUS_SIX_SCALE.
    var scale: i64 = 100;

    var state: u64 = 0x9e3779b97f4a7c15;
    var stream: u64 = 0xcbf29ce484222325;
    var total: i64 = 5000000 * scale;
    var i: i64 = 0;
    while (i < total) {
        state = state + 0x9e3779b97f4a7c15;
        var z: u64 = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        var value: u64 = z ^ (z >> 31);
        stream = mix(stream, value);
        i = i + 1;
    }
    print("PHASE stream");
    print(stream);

    var buckets: i64[>..] = [];
    var keys: u64[>..] = [];
    var values: u64[>..] = [];
    var next: i64[>..] = [];
    i = 0;
    while (i < 8192) { buckets.push(-1); i = i + 1; }

    var items: i64 = 200000 * scale;
    i = 0;
    while (i < items) {
        state = state + 0x9e3779b97f4a7c15;
        var z: u64 = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        var key: u64 = z ^ (z >> 31);
        var b: i64 = (key % 8192) as i64;
        keys.push(key);
        values.push(i as u64);
        next.push(buckets[b]);
        buckets[b] = i;
        i = i + 1;
    }
    var hash: u64 = 0x84222325cbf29ce4;
    i = 0;
    while (i < items) {
        var cursor: i64 = buckets[(keys[i] % 8192) as i64];
        while (cursor != -1 && keys[cursor] != keys[i]) {
            cursor = next[cursor];
        }
        if (cursor != -1) { hash = mix(hash, values[cursor]); }
        i = i + 1;
    }
    print("PHASE hash");
    print(hash);

    var arena_bytes: u8[>..] = [];
    i = 0;
    while (i < 1048576) { arena_bytes.push(0); i = i + 1; }
    var arena: u64 = 0x517cc1b727220a95;
    var rounds: i64 = 200 * scale;
    var r: i64 = 0;
    while (r < rounds) {
        var off: i64 = 0;
        while (off + 64 <= 1048576) {
            state = state + 0x9e3779b97f4a7c15;
            var z: u64 = state;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
            z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
            var fill: u8 = (z ^ (z >> 31)) as u8;
            var k: i64 = 0;
            while (k < 64) { arena_bytes[off + k] = fill; k = k + 1; }
            arena = mix(arena, arena_bytes[off]);
            off = off + 64;
        }
        r = r + 1;
    }
    print("PHASE arena");
    print(arena);
    print("TOTAL");
    print(stream);
    print(hash);
    print(arena);
}
