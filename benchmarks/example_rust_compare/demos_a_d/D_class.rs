// Safe-Rust equivalent of demos/D_class.c (lightweight struct-based class).
//
// C+ lowers its `class` to a struct plus free functions with an implicit
// `self`.  Rust's `struct` + `impl` is the idiomatic analogue: the same state
// transition and method dispatch, expressed with safe borrows instead of a raw
// `Player *` view.  A `&mut Player` borrow enforces exclusivity at compile
// time; the C+ raw pointer does not (see RESULTS.md, non-equivalence note).
struct Player {
    health: i32,
    #[allow(dead_code)]
    score: i32,
}

impl Player {
    fn damage(&mut self, amount: i32) {
        self.health -= amount;
    }

    fn heal(&mut self, amount: i32) {
        self.health += amount;
    }

    fn get_health(&self) -> i32 {
        self.health
    }
}

fn main() {
    let mut player = Player { health: 100, score: 0 };
    player.damage(30);
    println!("after damage: {}", player.get_health());
    player.heal(10);
    println!("after heal: {}", player.get_health());

    let view = &mut player;
    view.damage(5);
    println!("through pointer: {}", view.get_health());
}
