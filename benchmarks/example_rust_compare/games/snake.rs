// Safe-Rust model of the deterministic core of examples/games/snake.c.
// Board is 20x20, directions use the same constants, Logic() is ported
// statement-for-statement and the fruit RNG is the same fixed-seed LCG.
// No unsafe, no FFI, no terminal I/O.

const DET_SEED: u32 = 12345;
const WIDTH: i32 = 20;
const HEIGHT: i32 = 20;
const UP: i32 = 72;
const DOWN: i32 = 80;
const LEFT: i32 = 75;
const RIGHT: i32 = 77;

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

struct Snake {
    gameover: bool,
    score: i32,
    tail_x: [i32; 100],
    tail_y: [i32; 100],
    tail_length: usize,
    fruit_x: i32,
    fruit_y: i32,
    head_x: i32,
    head_y: i32,
    dir: i32,
}

impl Snake {
    fn logic(&mut self, rng: &mut DetRng) {
        let mut prev_x = self.tail_x[0];
        let mut prev_y = self.tail_y[0];
        self.tail_x[0] = self.head_x;
        self.tail_y[0] = self.head_y;

        for i in 1..self.tail_length {
            let prev2_x = self.tail_x[i];
            let prev2_y = self.tail_y[i];
            self.tail_x[i] = prev_x;
            self.tail_y[i] = prev_y;
            prev_x = prev2_x;
            prev_y = prev2_y;
        }

        match self.dir {
            LEFT => self.head_x -= 1,
            RIGHT => self.head_x += 1,
            UP => self.head_y -= 1,
            DOWN => self.head_y += 1,
            _ => {}
        }

        if self.head_x < 0 || self.head_x >= WIDTH || self.head_y < 0 || self.head_y >= HEIGHT {
            self.gameover = true;
        }

        for i in 0..self.tail_length {
            if self.tail_x[i] == self.head_x && self.tail_y[i] == self.head_y {
                self.gameover = true;
            }
        }

        if self.head_x == self.fruit_x && self.head_y == self.fruit_y {
            self.score += 1;
            self.tail_length += 1;
            rng.reseed(0); // C: srand(time(NULL)) -> det_seed() discards value.
            self.fruit_x = rng.next() % WIDTH;
            self.fruit_y = rng.next() % HEIGHT;
        }
    }

    fn print_tail(&self) {
        for i in 0..self.tail_length {
            if i != 0 {
                print!(";");
            }
            print!("{},{}", self.tail_x[i], self.tail_y[i]);
        }
    }
}

fn main() {
    let script = "dddwwaaaaaaaaaaaa";
    let mut rng = DetRng::fresh();

    // Setup() with deterministic rand(): srand(0); rand()%WIDTH, rand()%HEIGHT
    rng.reseed(0);
    let setup_fx = rng.next() % WIDTH;
    let setup_fy = rng.next() % HEIGHT;

    let mut s = Snake {
        gameover: false,
        score: 0,
        tail_x: [0; 100],
        tail_y: [0; 100],
        tail_length: 0,
        fruit_x: WIDTH / 2 + 3,
        fruit_y: HEIGHT / 2,
        head_x: WIDTH / 2,
        head_y: HEIGHT / 2,
        dir: RIGHT,
    };

    println!("SEED {}", DET_SEED);
    println!("SETUP_FRUIT {},{}", setup_fx, setup_fy);
    println!(
        "START head={},{} fruit={},{} dir={}",
        s.head_x, s.head_y, s.fruit_x, s.fruit_y, s.dir
    );

    for c in script.bytes() {
        if s.gameover {
            break;
        }
        s.dir = match c {
            b'a' => LEFT,
            b'd' => RIGHT,
            b'w' => UP,
            b's' => DOWN,
            _ => s.dir,
        };
        s.logic(&mut rng);
        print!("STEP {} head={},{} tail=", c as char, s.head_x, s.head_y);
        s.print_tail();
        println!(
            " score={} fruit={},{} over={}",
            s.score,
            s.fruit_x,
            s.fruit_y,
            if s.gameover { 1 } else { 0 }
        );
    }
    println!(
        "FINAL score={} over={}",
        s.score,
        if s.gameover { 1 } else { 0 }
    );
}
