// Safe-Rust model of examples/expanded/games/snake/snake_core.cp (standalone).
// No unsafe, no FFI. Deterministic Snake logic core + its self-test.
// Build: rustc -O --edition 2021 snake_core.rs -o snake_core

const W: usize = 20;
const H: usize = 20;
const CELLS: i32 = (W * H) as i32;
const INITIAL_LEN: usize = 5;

const UP: i32 = 0;
const DOWN: i32 = 1;
const LEFT: i32 = 2;
const RIGHT: i32 = 3;

const REASON_NONE: i32 = 0;
const REASON_WALL: i32 = 1;
const REASON_SELF: i32 = 2;
const REASON_FULL: i32 = 3;

fn pack(x: i32, y: i32) -> i32 { y * W as i32 + x }

struct SnakeCore {
    body: Vec<i32>,
    dir: i32,
    food: i32,
    score: i32,
    game_over: i32,
    reason: i32,
    steps: i32,
    rng: u32,
    err_failed: bool,
}

impl SnakeCore {
    fn new(seed: u32) -> Self {
        let mut c = Self {
            body: Vec::new(), dir: RIGHT, food: -1, score: 0,
            game_over: 0, reason: REASON_NONE, steps: 0,
            rng: seed, err_failed: false,
        };
        for i in 0..INITIAL_LEN {
            c.body.push(pack(10 - i as i32, 10));
        }
        c.place_food();
        c
    }

    fn next_rand(&mut self) -> u32 {
        self.rng = self.rng.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        self.rng
    }

    fn occupied(&self, cell: i32) -> bool {
        self.body.iter().any(|&v| v == cell)
    }

    fn place_food(&mut self) {
        let start = (self.next_rand() % CELLS as u32) as i32;
        for k in 0..CELLS {
            let cell = (start + k) % CELLS;
            if !self.occupied(cell) {
                self.food = cell;
                return;
            }
        }
        self.food = -1;
        self.game_over = 1;
        self.reason = REASON_FULL;
        self.err_failed = true;
    }

    fn set_food(&mut self, x: i32, y: i32) {
        if x < 0 || x >= W as i32 || y < 0 || y >= H as i32 {
            self.err_failed = true;
            return;
        }
        if self.occupied(pack(x, y)) {
            self.err_failed = true;
            return;
        }
        self.food = pack(x, y);
    }

    fn step(&mut self, dir: i32) -> i32 {
        if self.game_over != 0 { self.err_failed = true; return -1; }
        if dir < UP || dir > RIGHT { self.err_failed = true; return -1; }
        if (dir == LEFT && self.dir == RIGHT) || (dir == RIGHT && self.dir == LEFT)
            || (dir == UP && self.dir == DOWN) || (dir == DOWN && self.dir == UP)
        {
            self.err_failed = true;
            return -1;
        }
        let head = match self.body.first() { Some(&h) => h, None => { self.err_failed = true; return -1; } };
        self.dir = dir;

        let mut nx = head % W as i32;
        let mut ny = head / W as i32;
        if dir == UP { ny -= 1; }
        else if dir == DOWN { ny += 1; }
        else if dir == LEFT { nx -= 1; }
        else { nx += 1; }

        if nx < 0 || nx >= W as i32 || ny < 0 || ny >= H as i32 {
            self.game_over = 1;
            self.reason = REASON_WALL;
            self.err_failed = true;
            return -1;
        }

        let new_head = pack(nx, ny);
        let growing = new_head == self.food;
        let mut limit = self.body.len();
        if !growing && limit > 0 { limit -= 1; }
        for &segment in self.body.iter().take(limit) {
            if segment == new_head {
                self.game_over = 1;
                self.reason = REASON_SELF;
                self.err_failed = true;
                return -1;
            }
        }

        self.body.insert(0, new_head);
        if growing {
            self.score += 1;
            self.place_food();
        } else {
            self.body.pop();
        }
        self.steps += 1;
        0
    }

    fn render(&self) -> String {
        let head = self.body.first().copied().unwrap_or(-1);
        let mut out = String::with_capacity(H * (W + 1));
        for y in 0..H {
            for x in 0..W {
                let cell = pack(x as i32, y as i32);
                let mut ch = '.';
                if cell == self.food { ch = 'F'; }
                for &segment in self.body.iter().skip(1) {
                    if segment == cell { ch = 'o'; break; }
                }
                if head == cell { ch = 'O'; }
                out.push(ch);
            }
            out.push('\n');
        }
        out
    }
}

fn main() {
    let mut checks = 0;
    let mut failed = 0;
    macro_rules! check {
        ($cond:expr) => {{ checks += 1; if !($cond) { failed += 1; } }};
    }

    let mut c = SnakeCore::new(12345);
    check!(c.body.len() == INITIAL_LEN && c.score == 0 && c.game_over == 0 && !c.err_failed);
    let buf = c.render();
    check!(buf.len() == H * (W + 1) && buf.as_bytes()[0] == b'.'
           && buf.as_bytes()[H * (W + 1) - 1] == b'\n');

    c.set_food(11, 10);
    check!(c.step(RIGHT) == 0 && c.score == 1 && c.body.len() == INITIAL_LEN + 1 && c.game_over == 0);
    check!(c.step(LEFT) == -1 && c.err_failed && c.game_over == 0);

    let mut c = SnakeCore::new(12345);
    c.set_food(0, 0);
    for _ in 0..10 {
        if c.game_over != 0 { break; }
        c.step(RIGHT);
    }
    check!(c.game_over == 1 && c.reason == REASON_WALL && c.score == 0);

    let mut c = SnakeCore::new(12345);
    c.set_food(0, 0);
    c.step(RIGHT);
    c.step(UP);
    c.step(LEFT);
    c.step(DOWN);
    check!(c.game_over == 1 && c.reason == REASON_SELF && c.score == 0);

    println!("SNAKE_CORE_SELFTEST {} {}", checks, failed);
    std::process::exit(if failed == 0 { 0 } else { 1 });
}
