fn main() {
    var sum: i64 = 0;
    var i: i64 = 0;
    while (i < 200000000) {
        sum = sum + i;
        i = i + 1;
    }
    print(sum);
}
