// Safe-Rust equivalent of demos/K_state_class.c.
//
// Models the observable behavior of the C+ `class State` with methods that
// lower to State_add(&state, n) / State_get(&state), and `auto` inference of
// the zero-initialized struct. Rust has no `class`/`auto`; the idiomatic
// analogue is a struct with inherent methods taking &mut self / &self.
// Output for the three adds (4, 8, 12) is "count= 3 average= 8".
//
// NOTE: the original C+ source is BLOCKED under the prescribed strict build
// (`build/c+ build ... --strict --sanitize=none`): the C+ checker rejects
// demos/K_state_class.c:17:30 with
//   error: [CPLUS-ARITH] division by zero cannot be excluded
// because it cannot prove the divisor `self->count` non-zero behind the
// ternary guard. The Rust build has no such restriction and compiles/runs.
// See RESULTS.md for exact evidence and the non-strict legacy observation.
//
// rustc -O K_state_class.rs -o K_state_class

struct State {
    count: i32,
    total: i32,
}

impl State {
    fn new() -> Self {
        State { count: 0, total: 0 } // equivalent of (State){0}
    }

    fn add(&mut self, value: i32) {
        self.count += 1;
        self.total += value;
    }

    fn average(&self) -> i32 {
        if self.count != 0 {
            self.total / self.count
        } else {
            0
        }
    }
}

fn main() {
    let mut state = State::new();
    state.add(4);
    state.add(8);
    state.add(12);
    println!("count= {} average= {}", state.count, state.average());
}
