// Safe-Rust model of examples/expanded/games_3/nim.cp
//
// Models ONLY the deterministic observable behavior (stdout + exit status) for
// fixed argv inputs. No unsafe, no FFI, no randomness. The three heaps are fixed
// static data; both the optimal and take-away strategies are deterministic.

use std::env;

const NH: usize = 3;
const MAX_MOVES: i32 = 256;
const INITIAL_HEAPS: [i32; NH] = [3, 4, 5];

const CMD_OPTIMAL: i32 = -100;
const CMD_TAKE: i32 = -101;
const CMD_STATUS: i32 = -102;

const E_OK: i32 = 0;
const E_PARSE: i32 = 1;
const E_FORMAT: i32 = 2;
const E_BOUNDS: i32 = 3;
const E_AMOUNT: i32 = 4;
const E_INTERNAL: i32 = 5;

fn code_name(code: i32) -> &'static str {
    match code {
        E_OK => "E_OK",
        E_PARSE => "E_PARSE",
        E_FORMAT => "E_FORMAT",
        E_BOUNDS => "E_BOUNDS",
        E_AMOUNT => "E_AMOUNT",
        _ => "E_INTERNAL",
    }
}

fn parse_int(arg: &str) -> Result<i32, bool> {
    let bytes = arg.as_bytes();
    if bytes.is_empty() {
        return Err(false);
    }
    let mut i = 0usize;
    let mut negative = false;
    if bytes[0] == b'-' {
        negative = true;
        i = 1;
    } else if bytes[0] == b'+' {
        i = 1;
    }
    if i == bytes.len() {
        return Err(false);
    }
    let mut value: i32 = 0;
    let mut digits = 0i32;
    while i < bytes.len() {
        let ch = bytes[i];
        if ch < b'0' || ch > b'9' {
            return Err(false);
        }
        if value > 100000 || digits >= 8 {
            return Err(true);
        }
        value = value * 10 + (ch - b'0') as i32;
        digits += 1;
        i += 1;
    }
    Ok(if negative { -value } else { value })
}

fn nim_sum(heaps: &[i32; NH]) -> i32 {
    let mut x = 0;
    for &h in heaps.iter() {
        x ^= h;
    }
    x
}

fn render_heaps(heaps: &[i32; NH]) -> String {
    let parts: Vec<String> = heaps.iter().map(|h| h.to_string()).collect();
    format!("[{}]", parts.join(","))
}

struct NimGame {
    heaps: [i32; NH],
    moves: i32,
    finished: bool,
    winner: i32,
}

impl NimGame {
    fn new() -> Self {
        NimGame { heaps: INITIAL_HEAPS, moves: 0, finished: false, winner: 0 }
    }

    fn heap(&self, i: i32) -> i32 {
        if i < 0 || i >= NH as i32 {
            -1
        } else {
            self.heaps[i as usize]
        }
    }

    fn empty(&self) -> bool {
        self.heaps.iter().all(|&h| h == 0)
    }

    // The single guarded mutation. Ok(()) or Err(code).
    fn take(&mut self, i: i32, amount: i32) -> Result<(), i32> {
        if i < 0 || i >= NH as i32 {
            return Err(E_BOUNDS);
        }
        let idx = i as usize;
        if amount < 1 || amount > self.heaps[idx] {
            return Err(E_AMOUNT);
        }
        self.heaps[idx] -= amount;
        self.moves += 1;
        if self.heaps[idx] < 0 {
            return Err(E_INTERNAL);
        }
        Ok(())
    }

    fn optimal(&self) -> Result<(i32, i32), i32> {
        let x = nim_sum(&self.heaps);
        if self.empty() {
            return Err(E_INTERNAL);
        }
        if x != 0 {
            for i in 0..NH {
                let target = self.heaps[i] ^ x;
                if target < self.heaps[i] {
                    return Ok((i as i32, self.heaps[i] - target));
                }
            }
        }
        for i in 0..NH {
            if self.heaps[i] > 0 {
                return Ok((i as i32, 1));
            }
        }
        Err(E_INTERNAL)
    }

    fn takeaway(&self) -> Result<(i32, i32), i32> {
        for i in 0..NH {
            if self.heaps[i] > 0 {
                return Ok((i as i32, 1));
            }
        }
        Err(E_INTERNAL)
    }

    fn play(&mut self, player: i32, strategy: i32, manual_i: i32, manual_a: i32)
        -> Result<(i32, i32), i32>
    {
        if self.finished {
            return Err(E_INTERNAL);
        }
        if self.moves >= MAX_MOVES {
            return Err(E_INTERNAL);
        }
        let (i, amount) = if strategy == CMD_OPTIMAL {
            self.optimal()?
        } else if strategy == CMD_TAKE {
            if manual_i < 0 || manual_i >= NH as i32 {
                return Err(E_BOUNDS);
            }
            (manual_i, manual_a)
        } else {
            return Err(E_FORMAT);
        };
        self.take(i, amount)?;
        if self.empty() {
            self.finished = true;
            self.winner = player;
        }
        Ok((i, amount))
    }
}

fn refuse_line(out: &mut String, step: i32, detail: &str, code: i32, reason: &str) {
    out.push_str(&format!(
        "REFUSE step={} {} code={} reason={}\n",
        step, detail, code_name(code), reason
    ));
}

fn refuse_arg(out: &mut String, token: &str, code: i32, reason: &str) {
    refuse_line(out, 0, &format!("arg={}", token), code, reason);
}

fn move_line(out: &mut String, move_no: i32, player: i32, strategy: i32,
             heap: i32, amount: i32, x_before: i32, x_after: i32, heaps: &[i32; NH]) {
    out.push_str(&format!(
        "MOVE {} player={} strategy={} heap={} amount={} bound=ok xor_before={} xor_after={} heaps={}\n",
        move_no,
        if player == 1 { "A" } else { "B" },
        if strategy == CMD_OPTIMAL { "optimal" } else { "takeaway" },
        heap, amount, x_before, x_after, render_heaps(heaps)
    ));
}

// Built-in A(optimal) vs B(take-away) simulation. Returns Err(code) on refusal.
fn simulate(game: &mut NimGame, out: &mut String) -> Result<(), i32> {
    for _guard in 0..MAX_MOVES {
        let turn = game.moves;
        let player = if turn & 1 == 0 { 1 } else { 2 };
        let strategy = if player == 1 { CMD_OPTIMAL } else { CMD_TAKE };
        let mut i = 0;
        let mut amount = 0;
        if strategy == CMD_TAKE {
            match game.takeaway() {
                Ok((ti, ta)) => { i = ti; amount = ta; }
                Err(code) => return Err(code),
            }
        }
        let x_before = nim_sum(&game.heaps);
        match game.play(player, strategy, i, amount) {
            Ok((ri, ra)) => { i = ri; amount = ra; }
            Err(code) => {
                refuse_line(out, game.moves + 1, &format!("internal move player={}", player), code,
                    if code == E_BOUNDS { "internal move exceeded heap bounds" }
                    else if code == E_AMOUNT { "internal move removed more than the heap holds" }
                    else { "internal move invariant violation" });
                return Err(code);
            }
        }
        let x_after = nim_sum(&game.heaps);
        let move_no = game.moves;
        move_line(out, move_no, player, strategy, i, amount, x_before, x_after, &game.heaps);
        if game.finished {
            break;
        }
    }
    if !game.finished {
        refuse_line(out, game.moves, "simulation=incomplete", E_INTERNAL,
            "game did not finish within the move cap");
        return Err(E_INTERNAL);
    }
    Ok(())
}

fn main() {
    let mut game = NimGame::new();
    let args: Vec<String> = env::args().skip(1).collect();
    let mut prog: Vec<i32> = Vec::new();
    let mut out = String::new();

    let parse_code = if !args.is_empty() {
        let mut code = E_OK;
        for arg in &args {
            if arg.len() == 1 {
                match arg.as_bytes()[0] {
                    b'O' | b'o' => { prog.push(CMD_OPTIMAL); continue; }
                    b'T' | b't' => { prog.push(CMD_TAKE); continue; }
                    b'S' | b's' => { prog.push(CMD_STATUS); continue; }
                    _ => {}
                }
            }
            match parse_int(arg) {
                Ok(v) => prog.push(v),
                Err(oversized) => {
                    refuse_arg(&mut out, arg, E_PARSE,
                        if oversized { "numeric token is oversized" } else { "numeric token is malformed" });
                    code = E_PARSE;
                    break;
                }
            }
        }
        code
    } else {
        E_OK
    };

    if parse_code != E_OK {
        out.push_str(&format!("NIM_DONE status=REFUSED code={}\n", code_name(parse_code)));
        print!("{}", out);
        std::process::exit(2);
    }

    // Built-in mode: no argv tokens at all.
    if prog.is_empty() && args.is_empty() {
        out.push_str(&format!("NIM heaps={} heap_count={} mode=builtin\n",
            render_heaps(&INITIAL_HEAPS), NH));
        out.push_str(&format!("INITIAL heaps={} xor={}\n",
            render_heaps(&game.heaps), nim_sum(&game.heaps)));
        if let Err(code) = simulate(&mut game, &mut out) {
            out.push_str(&format!("NIM_DONE status=REFUSED code={}\n", code_name(code)));
            print!("{}", out);
            std::process::exit(2);
        }
        let winner = if game.winner == 1 { "A" } else { "B" };
        out.push_str(&format!("RESULT winner={} moves={} heaps={}\n",
            winner, game.moves, render_heaps(&game.heaps)));
        out.push_str(&format!("FINAL winner={} moves={}\n", winner, game.moves));
        out.push_str(&format!("NIM_DONE status=OK winner={}\n", winner));
        print!("{}", out);
        return;
    }

    if prog.is_empty() {
        refuse_line(&mut out, 0, "program=empty", E_FORMAT, "empty move program");
        out.push_str(&format!("NIM_DONE status=REFUSED code={}\n", code_name(E_FORMAT)));
        print!("{}", out);
        std::process::exit(2);
    }

    out.push_str(&format!("NIM heaps={} heap_count={} program={}\n",
        render_heaps(&INITIAL_HEAPS), NH, prog.len()));
    out.push_str(&format!("INITIAL heaps={} xor={}\n",
        render_heaps(&game.heaps), nim_sum(&game.heaps)));

    let mut pc: usize = 0;
    let mut step = 0;
    let mut refused = false;
    let mut final_code = E_OK;

    while pc < prog.len() {
        let cmd = prog[pc];
        pc += 1;

        if cmd == CMD_STATUS {
            step += 1;
            out.push_str(&format!(
                "STATUS step={} code=E_OK heaps={} xor={} moves={} finished={}\n",
                step, render_heaps(&game.heaps), nim_sum(&game.heaps), game.moves,
                if game.finished { 1 } else { 0 }
            ));
            continue;
        }

        if cmd != CMD_OPTIMAL && cmd != CMD_TAKE {
            refuse_line(&mut out, step + 1, &format!("cmd={} unknown", cmd), E_FORMAT,
                "unknown command token (expected O/T/S)");
            refused = true; final_code = E_FORMAT; break;
        }

        let mut i_take = 0;
        let mut amount = 0;
        if cmd == CMD_TAKE {
            if pc + 2 > prog.len() {
                refuse_line(&mut out, step + 1, "cmd=T operands=missing", E_FORMAT,
                    "take needs heap index and amount");
                refused = true; final_code = E_FORMAT; break;
            }
            i_take = prog[pc];
            amount = prog[pc + 1];
            pc += 2;
        }

        let player = if game.moves & 1 == 0 { 1 } else { 2 };
        step += 1;
        let x_before = nim_sum(&game.heaps);
        let bound_ok = cmd != CMD_TAKE
            || (i_take >= 0 && i_take < NH as i32 && amount >= 1
                && amount <= game.heap(i_take));

        match game.play(player, cmd, i_take, amount) {
            Ok((ri, ra)) => { i_take = ri; amount = ra; }
            Err(code) => {
                let detail = if cmd == CMD_TAKE {
                    format!("cmd=T i={} a={}", i_take, amount)
                } else {
                    format!("cmd=O player={}", player)
                };
                refuse_line(&mut out, step, &detail, code,
                    if code == E_BOUNDS { "heap index out of range (0..2)" }
                    else if code == E_AMOUNT { "amount must be in [1, heap size]" }
                    else { "no legal move (game over or internal invariant)" });
                refused = true; final_code = code; break;
            }
        }
        if !bound_ok {
            refuse_line(&mut out, step, &format!("cmd={} bound=VIOLATION", cmd), E_INTERNAL,
                "move failed the explicit cannot-remove-more-than-heap check");
            refused = true; final_code = E_INTERNAL; break;
        }

        let x_after = nim_sum(&game.heaps);
        move_line(&mut out, step, player, cmd, i_take, amount, x_before, x_after, &game.heaps);
        if game.finished {
            out.push_str(&format!("RESULT winner={} step={} moves={}\n",
                if game.winner == 1 { "A" } else { "B" }, step, game.moves));
        }
    }

    if refused {
        out.push_str(&format!("NIM_DONE status=REFUSED code={}\n", code_name(final_code)));
        print!("{}", out);
        std::process::exit(2);
    }

    out.push_str(&format!("FINAL winner={} moves={} finished={} steps={}\n",
        if game.winner == 1 { "A" } else { "B" }, game.moves,
        if game.finished { 1 } else { 0 }, step));
    out.push_str(&format!("NIM_DONE status=OK winner={}\n",
        if game.finished { if game.winner == 1 { "A" } else { "B" } } else { "none" }));
    print!("{}", out);
}
