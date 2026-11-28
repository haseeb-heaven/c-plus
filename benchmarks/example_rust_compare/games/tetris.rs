// Safe-Rust model of the deterministic rules core of examples/games/tetris.c.
// Shapes, 7-bag randomiser, collision, wall kicks, line clears, scoring and
// gravity are ported statement-for-statement.  No unsafe, no FFI, no curses.

const DET_SEED: u32 = 12345;
const BOARD_W: usize = 10;
const BOARD_H: usize = 20;
const BOX: usize = 4;
const NUM_PIECES: usize = 7;
const NUM_ROTS: usize = 4;

const SHAPE_SRC: [&[&str; BOX]; NUM_PIECES] = [
    &["....", "XXXX", "....", "...."], // I
    &["X...", "XXX.", "....", "...."], // J
    &["..X.", "XXX.", "....", "...."], // L
    &[".XX.", ".XX.", "....", "...."], // O
    &[".XX.", "XX..", "....", "...."], // S
    &[".X..", "XXX.", "....", "...."], // T
    &["XX..", ".XX.", "....", "...."], // Z
];

const KICKS: [(i32, i32); 10] = [
    (0, 0),
    (-1, 0),
    (1, 0),
    (-2, 0),
    (2, 0),
    (0, -1),
    (-1, -1),
    (1, -1),
    (-2, -1),
    (2, -1),
];

// MODES[0] ("marathon") -- the mode used by the deterministic driver.
const START_LEVEL: i32 = 1;
const LINES_PER_LEVEL: i32 = 10;
const BASE_GRAVITY_MS: i32 = 800;
const GRAVITY_STEP_MS: i32 = 70;
const MIN_GRAVITY_MS: i32 = 80;

const KEY_UP: i32 = 259;
const KEY_LEFT: i32 = 260;
const KEY_RIGHT: i32 = 261;
const KEY_DOWN: i32 = 258;

struct DetRng {
    state: u32,
}

impl DetRng {
    fn fresh() -> Self {
        Self { state: DET_SEED }
    }
    fn reseed(&mut self, _seed: u32) {
        self.state = DET_SEED;
    }
    fn next(&mut self) -> i32 {
        self.state = self
            .state
            .wrapping_mul(1103515245)
            .wrapping_add(12345);
        ((self.state >> 16) & 0x7fff) as i32
    }
}

type Shape = [[[[bool; BOX]; BOX]; NUM_ROTS]; NUM_PIECES];

fn init_shapes() -> Shape {
    let mut shape = [[[[false; BOX]; BOX]; NUM_ROTS]; NUM_PIECES];
    for p in 0..NUM_PIECES {
        for r in 0..BOX {
            for c in 0..BOX {
                shape[p][0][r][c] = SHAPE_SRC[p][r].as_bytes()[c] == b'X';
            }
        }
        for rot in 1..NUM_ROTS {
            for r in 0..BOX {
                for c in 0..BOX {
                    shape[p][rot][r][c] = shape[p][rot - 1][BOX - 1 - c][r];
                }
            }
        }
    }
    shape
}

struct Bag {
    pieces: [usize; NUM_PIECES],
    pos: usize,
}

impl Bag {
    fn fresh() -> Self {
        Self {
            pieces: [0; NUM_PIECES],
            pos: NUM_PIECES,
        }
    }
    fn next(&mut self, rng: &mut DetRng) -> usize {
        if self.pos >= NUM_PIECES {
            for i in 0..NUM_PIECES {
                self.pieces[i] = i;
            }
            for i in (1..NUM_PIECES).rev() {
                let j = (rng.next() as usize) % (i + 1);
                self.pieces.swap(i, j);
            }
            self.pos = 0;
        }
        let p = self.pieces[self.pos];
        self.pos += 1;
        p
    }
}

struct Game {
    board: [[i32; BOARD_W]; BOARD_H],
    piece: usize,
    rot: usize,
    x: i32,
    y: i32,
    next: usize,
    score: i32,
    lines: i32,
    level: i32,
    over: bool,
    running: bool,
    paused: bool,
    to_menu: bool,
    last_drop: i64,
}

impl Game {
    fn new() -> Self {
        Self {
            board: [[0; BOARD_W]; BOARD_H],
            piece: 0,
            rot: 0,
            x: 0,
            y: 0,
            next: 0,
            score: 0,
            lines: 0,
            level: START_LEVEL,
            over: false,
            running: true,
            paused: false,
            to_menu: false,
            last_drop: 0,
        }
    }

    fn collides(&self, shape: &Shape, piece: usize, rot: usize, px: i32, py: i32) -> bool {
        for r in 0..BOX {
            for c in 0..BOX {
                if !shape[piece][rot][r][c] {
                    continue;
                }
                let bx = px + c as i32;
                let by = py + r as i32;
                if bx < 0 || bx >= BOARD_W as i32 || by >= BOARD_H as i32 {
                    return true;
                }
                if by >= 0 && self.board[by as usize][bx as usize] != 0 {
                    return true;
                }
            }
        }
        false
    }

    fn spawn(&mut self, shape: &Shape, bag: &mut Bag, rng: &mut DetRng) {
        self.piece = self.next;
        self.next = bag.next(rng);
        self.rot = 0;
        self.x = ((BOARD_W - BOX) / 2) as i32;
        self.y = 0;
        if self.collides(shape, self.piece, self.rot, self.x, self.y) {
            self.over = true;
        }
    }

    fn try_move(&mut self, shape: &Shape, dx: i32, dy: i32) -> bool {
        if self.collides(shape, self.piece, self.rot, self.x + dx, self.y + dy) {
            return false;
        }
        self.x += dx;
        self.y += dy;
        true
    }

    fn try_rotate(&mut self, shape: &Shape) {
        let nr = (self.rot + 1) % NUM_ROTS;
        for &(kx, ky) in &KICKS {
            let nx = self.x + kx;
            let ny = self.y + ky;
            if !self.collides(shape, self.piece, nr, nx, ny) {
                self.rot = nr;
                self.x = nx;
                self.y = ny;
                return;
            }
        }
    }

    fn clear_lines(&mut self) {
        const POINTS: [i32; 5] = [0, 100, 300, 500, 800];
        let mut cleared = 0usize;
        let mut row: i32 = BOARD_H as i32 - 1;
        while row >= 0 {
            let r = row as usize;
            let full = self.board[r].iter().all(|&v| v != 0);
            if full {
                for rr in (1..=r).rev() {
                    self.board[rr] = self.board[rr - 1];
                }
                self.board[0] = [0; BOARD_W];
                cleared += 1;
                row += 1; // re-test the row just refilled
            }
            row -= 1;
        }
        if cleared > 0 {
            self.score += POINTS[cleared] * self.level;
            self.lines += cleared as i32;
            self.level = START_LEVEL + self.lines / LINES_PER_LEVEL;
        }
    }

    fn lock_piece(&mut self, shape: &Shape, bag: &mut Bag, rng: &mut DetRng) {
        for r in 0..BOX {
            for c in 0..BOX {
                if shape[self.piece][self.rot][r][c] {
                    let bx = self.x + c as i32;
                    let by = self.y + r as i32;
                    if by >= 0 && by < BOARD_H as i32 && bx >= 0 && bx < BOARD_W as i32 {
                        self.board[by as usize][bx as usize] = self.piece as i32 + 1;
                    }
                }
            }
        }
        self.clear_lines();
        self.spawn(shape, bag, rng);
    }

    fn hard_drop(&mut self, shape: &Shape, bag: &mut Bag, rng: &mut DetRng) {
        let mut dist = 0;
        while !self.collides(shape, self.piece, self.rot, self.x, self.y + 1) {
            self.y += 1;
            dist += 1;
        }
        self.score += dist * 2;
        self.lock_piece(shape, bag, rng);
    }

    fn drop_interval_ms(&self) -> i32 {
        let ms = BASE_GRAVITY_MS - (self.level - 1) * GRAVITY_STEP_MS;
        if ms < MIN_GRAVITY_MS {
            MIN_GRAVITY_MS
        } else {
            ms
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn handle_input(
        &mut self,
        shape: &Shape,
        ch: i32,
        now: i64,
        bag: &mut Bag,
        rng: &mut DetRng,
    ) {
        match ch {
            113 | 81 => self.running = false, // q Q
            109 | 77 => { // m M
                self.to_menu = true;
                self.running = false;
            }
            112 | 80 => { // p P
                self.paused = !self.paused;
                self.last_drop = now;
            }
            KEY_LEFT => {
                if !self.paused { self.try_move(shape, -1, 0); }
            }
            KEY_RIGHT => {
                if !self.paused { self.try_move(shape, 1, 0); }
            }
            KEY_UP => {
                if !self.paused { self.try_rotate(shape); }
            }
            KEY_DOWN => {
                if !self.paused && self.try_move(shape, 0, 1) {
                    self.score += 1;
                    self.last_drop = now;
                }
            }
            32 => { // space
                if !self.paused {
                    self.hard_drop(shape, bag, rng);
                }
            }
            _ => {}
        }
    }
}

fn print_board(g: &Game) {
    for (row, cells) in g.board.iter().enumerate() {
        if row != 0 {
            print!("/");
        }
        for v in cells {
            print!("{}", v);
        }
    }
    println!();
}

fn print_state(op: &str, g: &Game) {
    println!(
        "OP {} piece={} rot={} x={} y={} next={} score={} lines={} level={} over={}",
        op, g.piece, g.rot, g.x, g.y, g.next, g.score, g.lines, g.level,
        if g.over { 1 } else { 0 }
    );
    print_board(g);
}

fn main() {
    let script: [(i32, &str); 10] = [
        (KEY_UP, "ROT"),
        (KEY_LEFT, "LEFT"),
        (KEY_DOWN, "SOFT"),
        (KEY_DOWN, "SOFT"),
        (32, "DROP"),
        (KEY_UP, "ROT"),
        (KEY_RIGHT, "RIGHT"),
        (32, "DROP"),
        (KEY_DOWN, "SOFT"),
        (32, "DROP"),
    ];

    let mut rng = DetRng::fresh();
    rng.reseed(0);
    let shape = init_shapes();

    println!("SEED {}", DET_SEED);
    for p in 0..NUM_PIECES {
        for r in 0..NUM_ROTS {
            print!("SHAPE {} {} ", p, r);
            for i in 0..BOX {
                for j in 0..BOX {
                    print!("{}", if shape[p][r][i][j] { 1 } else { 0 });
                }
            }
            println!();
        }
    }

    let mut bag = Bag::fresh();
    print!("BAG ");
    for i in 0..2 * NUM_PIECES {
        if i != 0 {
            print!(",");
        }
        print!("{}", bag.next(&mut rng));
    }
    println!();

    let mut g = Game::new();
    bag = Bag::fresh();
    g.next = bag.next(&mut rng);
    g.spawn(&shape, &mut bag, &mut rng);
    print_state("START", &g);

    for &(ch, name) in &script {
        g.handle_input(&shape, ch, 0, &mut bag, &mut rng);
        print_state(name, &g);
    }

    g.board = [[0; BOARD_W]; BOARD_H];
    for c in 0..BOARD_W {
        g.board[BOARD_H - 1][c] = 1;
    }
    g.clear_lines();
    print!("CLEAR1 score={} lines={} level={} ", g.score, g.lines, g.level);
    print_board(&g);

    g.lines = 9;
    for c in 0..BOARD_W {
        g.board[BOARD_H - 1][c] = 1;
    }
    g.clear_lines();
    print!("CLEAR2 score={} lines={} level={} ", g.score, g.lines, g.level);
    print_board(&g);

    for level in 1..=12 {
        g.level = level;
        println!("GRAV {} {}", level, g.drop_interval_ms());
    }
}
