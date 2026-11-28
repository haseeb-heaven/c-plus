fn free_it(owned: Box<i32>) { drop(owned); }

fn main() {
    let arr = [1i32, 2, 3];
    let p: *mut i32 = &arr[1] as *const i32 as *mut i32;
    free_it(p);
}
