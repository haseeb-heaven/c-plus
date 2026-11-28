fn main() {
    var acc: i64 = 0;
    var i: i64 = 0;
    while (i < 4000) {
        var j: i64 = 0;
        while (j < 4000) {
            acc = acc + i * j - j * i + 1;
            j = j + 1;
        }
        i = i + 1;
    }
    print(acc);
}
