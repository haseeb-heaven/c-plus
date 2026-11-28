// Goose equivalent of modern.cp, using Goose's owned dynamic arrays as the
// closest syntax-permitted analogue of C+ vector / string:
//   vector<i32> -> i32[>..]   (push, indexed read)
//   string      -> u8[>..]    (byte buffer, push/append, .len)
// Same N/M and the same checksum as modern.cp / modern.rs.
fn main() {
    var v: i32[>..] = [];
    var i: i64 = 0;
    while (i < 2000000) {
        v.push(i as i32);
        i = i + 1;
    }
    var vsum: i64 = 0;
    i = 0;
    while (i < 2000000) {
        vsum = vsum + v[i];
        i = i + 1;
    }

    var s: u8[>..] = [];
    s.push(97); // 'a' -> initial size 1, matching string("a")
    var ssize_sum: i64 = 0;
    i = 0;
    while (i < 200000) {
        s.append("bbbbbbbbbb");
        ssize_sum = ssize_sum + s.len;
        i = i + 1;
    }

    print(vsum + ssize_sum);
}
