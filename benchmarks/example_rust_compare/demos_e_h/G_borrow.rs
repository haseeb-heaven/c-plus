// Safe-Rust equivalent of demos/G_borrow.c.
//
// Observable behavior:
//   borrowed value: 42
//   owner still alive
//   drop slot 42
//
// No unsafe and no FFI.  The borrow is an ordinary shared reference `&i32`
// whose lifetime the compiler checks against the owner; the destructor is an
// ordinary `Drop` impl that runs at scope exit.
//
// MECHANISM NON-EQUIVALENCE: C+ implements the destructor with *runtime*
// metadata (a compiler-generated cleanup record pushed onto the scope's
// cplus_cleanup list, dispatched by cplus_scope_cleanup).  Rust enforces the
// borrow statically (zero-cost) and runs Drop by static drop elaboration.
// The observable stdout is identical; the enforcement/dispatch mechanisms are
// not.

struct Slot {
    value: i32,
}

impl Drop for Slot {
    fn drop(&mut self) {
        if self.value != -1 {
            println!("drop slot {}", self.value);
            self.value = -1; // mirrors Slot_drop's poison value
        }
    }
}

fn read_slot(p: &i32) {
    println!("borrowed value: {}", p);
}

fn main() {
    let slot = Slot { value: 42 };
    let borrow: &i32 = &slot.value; // valid borrow; cannot outlive `slot`
    read_slot(borrow);
    println!("owner still alive");
    // `slot` drops here -> "drop slot 42".
}
