// Safe-Rust model of the deterministic core of examples/games/tiles.c.
// Grid is 16 values (row-major), slide/merge rules and spawn RNG match the C
// source exactly.  No unsafe, no FFI.

const DET_SEED: u32 = 12345;

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

fn spawn(grid: &mut [i32; 16], rng: &mut DetRng) {
    let mut empty = [0usize; 16];
    let mut n = 0usize;
    for i in 0..16 {
        if grid[i] == 0 {
            empty[n] = i;
            n += 1;
        }
    }
    if n == 0 {
        return;
    }
    let p = empty[(rng.next() as usize) % n];
    grid[p] = if rng.next() % 10 == 0 { 4 } else { 2 };
}

/* slide+merge one row toward index 0, exactly as tiles.c does */
fn slide(row: &mut [i32; 4], score: &mut i32) -> i32 {
    let mut moved = 0;
    let mut tmp = [0i32; 4];
    let mut k = 0usize;
    for i in 0..4 {
        if row[i] != 0 {
            tmp[k] = row[i];
            k += 1;
        }
    }
    for i in 0..3 {
        if tmp[i] != 0 && tmp[i] == tmp[i + 1] {
            tmp[i] *= 2;
            *score += tmp[i];
            tmp[i + 1] = 0;
        }
    }
    let mut out = [0i32; 4];
    let mut k = 0usize;
    for i in 0..4 {
        if tmp[i] != 0 {
            out[k] = tmp[i];
            k += 1;
        }
    }
    for i in 0..4 {
        if row[i] != out[i] {
            moved = 1;
        }
        row[i] = out[i];
    }
    moved
}

/* 0=left 1=right 2=up 3=down */
fn do_move(grid: &mut [i32; 16], dir: usize, score: &mut i32) -> i32 {
    let mut moved = 0;
    for i in 0..4 {
        let mut r = [0i32; 4];
        for j in 0..4 {
            r[j] = match dir {
                0 => grid[i * 4 + j],
                1 => grid[i * 4 + (3 - j)],
                2 => grid[j * 4 + i],
                _ => grid[(3 - j) * 4 + i],
            };
        }
        if slide(&mut r, score) != 0 {
            moved = 1;
        }
        for j in 0..4 {
            let val = r[j];
            match dir {
                0 => grid[i * 4 + j] = val,
                1 => grid[i * 4 + (3 - j)] = val,
                2 => grid[j * 4 + i] = val,
                _ => grid[(3 - j) * 4 + i] = val,
            }
        }
    }
    moved
}

fn print_grid(grid: &[i32; 16]) {
    let s: Vec<String> = grid.iter().map(|v| v.to_string()).collect();
    println!("{}", s.join(","));
}

fn main() {
    let script = [0usize, 2, 1, 3, 0, 0, 1, 2];
    let mut rng = DetRng::fresh();
    let mut grid = [0i32; 16];
    let mut score = 0i32;

    spawn(&mut grid, &mut rng);
    spawn(&mut grid, &mut rng);

    println!("SEED {}", DET_SEED);
    print!("INIT ");
    print_grid(&grid);

    for &dir in &script {
        let moved = do_move(&mut grid, dir, &mut score);
        if moved != 0 {
            spawn(&mut grid, &mut rng);
        }
        print!("MOVE {} {} {} ", dir, moved, score);
        print_grid(&grid);
    }
    println!("FINAL {}", score);
}
