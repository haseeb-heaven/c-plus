// Safe-Rust model of the deterministic core of examples/games/2048.c.
// No unsafe, no FFI.  Board cells are exponents of two (2^cell), exactly as in
// the C source; rand()/srand() are replaced by the same fixed-seed LCG used by
// the C driver in 2048_harness.c.

const SIZE: usize = 4;
const DET_SEED: u32 = 12345;

struct DetRng {
    state: u32,
    seeded: bool,
}

impl DetRng {
    fn fresh() -> Self {
        Self {
            state: DET_SEED,
            seeded: false,
        }
    }
    fn seed_once(&mut self) {
        // 2048.c seeds inside addRandom() only once via `static initialized`.
        // As in the C harness, the runtime seed value is discarded.
        if !self.seeded {
            self.state = DET_SEED;
            self.seeded = true;
        }
    }
    fn next(&mut self) -> i32 {
        self.state = self
            .state
            .wrapping_mul(1103515245)
            .wrapping_add(12345);
        ((self.state >> 16) & 0x7fff) as i32
    }
}

type Board = [[u8; SIZE]; SIZE];

fn find_target(array: &[u8; SIZE], x: usize, stop: usize) -> usize {
    if x == 0 {
        return x;
    }
    let mut t = x - 1;
    loop {
        if array[t] != 0 {
            if array[t] != array[x] {
                return t + 1;
            }
            return t;
        }
        if t == stop {
            return t;
        }
        t -= 1;
    }
}

fn slide_array(array: &mut [u8; SIZE], score: &mut u32) -> bool {
    let mut success = false;
    let mut stop = 0usize;
    for x in 0..SIZE {
        if array[x] != 0 {
            let t = find_target(array, x, stop);
            if t != x {
                if array[t] == 0 {
                    array[t] = array[x];
                } else if array[t] == array[x] {
                    array[t] += 1;
                    *score += 1u32 << array[t];
                    stop = t + 1;
                }
                array[x] = 0;
                success = true;
            }
        }
    }
    success
}

fn rotate_board(board: &mut Board) {
    let n = SIZE;
    for i in 0..n / 2 {
        for j in i..n - i - 1 {
            let tmp = board[i][j];
            board[i][j] = board[j][n - i - 1];
            board[j][n - i - 1] = board[n - i - 1][n - j - 1];
            board[n - i - 1][n - j - 1] = board[n - j - 1][i];
            board[n - j - 1][i] = tmp;
        }
    }
}

fn move_up(board: &mut Board, score: &mut u32) -> bool {
    let mut success = false;
    for x in 0..SIZE {
        // |= in C evaluates every column; Rust's |= on bool does too.
        success |= slide_array(&mut board[x], score);
    }
    success
}

fn move_left(board: &mut Board, score: &mut u32) -> bool {
    rotate_board(board);
    let success = move_up(board, score);
    rotate_board(board);
    rotate_board(board);
    rotate_board(board);
    success
}

fn move_down(board: &mut Board, score: &mut u32) -> bool {
    rotate_board(board);
    rotate_board(board);
    let success = move_up(board, score);
    rotate_board(board);
    rotate_board(board);
    success
}

fn move_right(board: &mut Board, score: &mut u32) -> bool {
    rotate_board(board);
    rotate_board(board);
    rotate_board(board);
    let success = move_up(board, score);
    rotate_board(board);
    success
}

fn find_pair_down(board: &Board) -> bool {
    for x in 0..SIZE {
        for y in 0..SIZE - 1 {
            if board[x][y] == board[x][y + 1] {
                return true;
            }
        }
    }
    false
}

fn count_empty(board: &Board) -> usize {
    let mut count = 0;
    for x in 0..SIZE {
        for y in 0..SIZE {
            if board[x][y] == 0 {
                count += 1;
            }
        }
    }
    count
}

fn game_ended(board: &mut Board) -> bool {
    if count_empty(board) > 0 {
        return false;
    }
    if find_pair_down(board) {
        return false;
    }
    rotate_board(board);
    let mut ended = true;
    if find_pair_down(board) {
        ended = false;
    }
    rotate_board(board);
    rotate_board(board);
    rotate_board(board);
    ended
}

fn add_random(board: &mut Board, rng: &mut DetRng) {
    rng.seed_once(); // C: srand(time(NULL)) runs only on the first call.
    let mut list: Vec<(usize, usize)> = Vec::new();
    for x in 0..SIZE {
        for y in 0..SIZE {
            if board[x][y] == 0 {
                list.push((x, y));
            }
        }
    }
    if !list.is_empty() {
        let r = (rng.next() as usize) % list.len();
        let (x, y) = list[r];
        let n = ((rng.next() % 10) / 9 + 1) as u8;
        board[x][y] = n;
    }
}

fn init_board(rng: &mut DetRng) -> Board {
    let mut board = [[0u8; SIZE]; SIZE];
    add_random(&mut board, rng);
    add_random(&mut board, rng);
    board
}

fn test_succeed() -> bool {
    // data holds per line: 4x IN, 4x OUT, 1x POINTS (exponents, base 2).
    const DATA: [u8; 13 * 9] = [
        0, 0, 0, 1, 1, 0, 0, 0, 0,
        0, 0, 1, 1, 2, 0, 0, 0, 4,
        0, 1, 0, 1, 2, 0, 0, 0, 4,
        1, 0, 0, 1, 2, 0, 0, 0, 4,
        1, 0, 1, 0, 2, 0, 0, 0, 4,
        1, 1, 1, 0, 2, 1, 0, 0, 4,
        1, 0, 1, 1, 2, 1, 0, 0, 4,
        1, 1, 0, 1, 2, 1, 0, 0, 4,
        1, 1, 1, 1, 2, 2, 0, 0, 8,
        2, 2, 1, 1, 3, 2, 0, 0, 12,
        1, 1, 2, 2, 2, 3, 0, 0, 12,
        3, 0, 1, 1, 3, 2, 0, 0, 4,
        2, 0, 1, 1, 2, 2, 0, 0, 4,
    ];
    let tests = DATA.len() / (2 * SIZE + 1);
    let mut success = true;
    for t in 0..tests {
        let base = t * (2 * SIZE + 1);
        let mut array = [0u8; SIZE];
        array.copy_from_slice(&DATA[base..base + SIZE]);
        let mut score = 0u32;
        slide_array(&mut array, &mut score);
        if array != DATA[base + SIZE..base + 2 * SIZE] {
            success = false;
        }
        if score != DATA[base + 2 * SIZE] as u32 {
            success = false;
        }
        if !success {
            break;
        }
    }
    if success {
        println!("All {} tests executed successfully", tests);
    }
    success
}

fn print_board(board: &Board) {
    let mut s = String::new();
    for x in 0..SIZE {
        for y in 0..SIZE {
            if x != 0 || y != 0 {
                s.push(',');
            }
            s.push_str(&board[x][y].to_string());
        }
    }
    println!("{}", s);
}

fn main() {
    let mut rng = DetRng::fresh();
    let mut board = init_board(&mut rng);
    let mut score = 0u32;

    println!("SEED {}", DET_SEED);
    print!("INIT ");
    print_board(&board);

    for c in "adwsadsw".bytes() {
        let ok = match c {
            b'a' => move_left(&mut board, &mut score),
            b'd' => move_right(&mut board, &mut score),
            b'w' => move_up(&mut board, &mut score),
            b's' => move_down(&mut board, &mut score),
            _ => false,
        };
        if ok {
            add_random(&mut board, &mut rng);
        }
        print!("MOVE {} {} {} ", c as char, if ok { 1 } else { 0 }, score);
        print_board(&board);
    }

    let pair_before = find_pair_down(&board);
    let ended = game_ended(&mut board);
    println!(
        "ENDED {} {} {}",
        if ended { 1 } else { 0 },
        count_empty(&board),
        if pair_before { 1 } else { 0 }
    );
    println!("TESTS_OK {}", if test_succeed() { 1 } else { 0 });
}
