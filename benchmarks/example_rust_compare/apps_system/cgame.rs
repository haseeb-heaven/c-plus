//! Safe-Rust behavioral model of examples/apps/cgame.c (the "CGAME" loop).
//!
//! No `unsafe`, no FFI. Models the observable game-engine behavior with
//! deterministic state steps: banner, menu, state progression, quiz scoring,
//! sandbox/library prompts, progress load/save and every printed string on
//! the paths exercised by compare.sh.
//!
//! SCOPE / NON-EQUIVALENCE: the full static question/lesson corpus of levels
//! 2-9 and exercises Ex01-Ex09 is teaching *content*, not engine behavior;
//! it is not replicated here. The Rust model implements level 1 and Ex00 in
//! full and the same engine state machine for all menu paths. The sandbox's
//! gcc/timeout subprocess checking is modeled as the "does not compile yet"
//! path for the untouched broken stub (no compiler is invoked from Rust).

use std::env;
use std::fs;
use std::io::{self, BufRead, Write};
use std::path::PathBuf;

const N_LEVELS: usize = 9;
const N_LIBEX: usize = 10;

#[derive(Clone, Copy)]
struct Colors {
    reset: &'static str,
    bold: &'static str,
    dim: &'static str,
    green: &'static str,
    red: &'static str,
    yellow: &'static str,
    cyan: &'static str,
    mag: &'static str,
}

impl Colors {
    fn new(enabled: bool) -> Self {
        if enabled {
            Self {
                reset: "\x1b[0m",
                bold: "\x1b[1m",
                dim: "\x1b[2m",
                green: "\x1b[32m",
                red: "\x1b[31m",
                yellow: "\x1b[33m",
                cyan: "\x1b[36m",
                mag: "\x1b[35m",
            }
        } else {
            Self {
                reset: "",
                bold: "",
                dim: "",
                green: "",
                red: "",
                yellow: "",
                cyan: "",
                mag: "",
            }
        }
    }
}

struct Input {
    reader: io::BufReader<io::Stdin>,
}

impl Input {
    fn new() -> Self {
        Self {
            reader: io::BufReader::new(io::stdin()),
        }
    }

    /// read_line(buf, 64): fgets, strip '\n', trim leading/trailing isspace.
    fn read_line(&mut self) -> Option<String> {
        let mut raw = String::new();
        let n = self.reader.read_line(&mut raw).ok()?;
        if n == 0 {
            return None;
        }
        let mut buf: String = raw.chars().take(63).collect();
        if let Some(pos) = buf.find('\n') {
            buf.truncate(pos);
        }
        Some(buf.trim().to_string())
    }
}

/// C atoi(): leading whitespace/sign, digits only, 0 on no conversion.
fn c_atoi(s: &str) -> i32 {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && b[i].is_ascii_whitespace() {
        i += 1;
    }
    let mut sign = 1i32;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        if b[i] == b'-' {
            sign = -1;
        }
        i += 1;
    }
    let mut value = 0i32;
    let mut any = false;
    while i < b.len() && b[i].is_ascii_digit() {
        any = true;
        value = value.wrapping_mul(10).wrapping_add((b[i] - b'0') as i32);
        i += 1;
    }
    if any {
        sign * value
    } else {
        0
    }
}

const LEVEL_TITLES: [&str; N_LEVELS] = [
    "Hello, C — the very basics",
    "Types & operators",
    "Control flow",
    "Functions & scope",
    "Pointers",
    "Arrays & strings",
    "Dynamic memory",
    "Integer gotchas",
    "Undefined behavior — hall of fame",
];

const LIBEX_TITLES: [&str; N_LIBEX] = [
    "Ex00: your very first function (the grammar of C)",
    "Ex01: my_abs — parameters and decisions",
    "Ex02: my_max — two parameters",
    "Ex03: my_toupper — characters are just numbers",
    "Ex04: my_strlen — how long is a string?",
    "Ex05: my_strcpy — copying a string",
    "Ex06: my_strcat — gluing strings together",
    "Ex07: my_strcmp — comparing strings",
    "Ex08: my_strchr — finding a character (hello, pointers)",
    "Ex09: my_atoi — from text to number (final boss)",
];

const LEVEL1_LESSON: &str = "Every C program starts at main(). printf() writes formatted\n\
output: %d for int, %f for double, %s for strings, %c for chars.\n\
Integer math truncates: 7/2 is 3. Operator precedence follows\n\
school math: * and / before + and -.";

const EX00_THEORY: &str = r#"Welcome! Before writing library functions, you need to know
what a C source file even looks like. Here is the anatomy of
a function — the basic building block of every C program:

      int  my_answer (void)      <- the 'signature'
      ^    ^          ^
      |    |          +-- parameters: what goes IN.
      |    |              'void' means: nothing goes in.
      |    +-- the name. You call it by this name.
      +-- return type: what comes OUT. 'int' = a whole number.

      {                          <- '{' opens the body
          return 42;             <- statements end with ';'
      }                          <- '}' closes the body

'return' hands a value back to whoever called the function and
ends it immediately. That's it — that's a complete, valid piece
of C. The game compiles your file together with a hidden test
program that calls your function and checks the result."#;

const EX01_TASK: &str =
    "The program must print the average of 9 and 4 as a decimal:\nexactly  6.5";
const EX01_HINT: &str =
    "Integer division truncates! Make the division floating point\n(divide by 2.0), store it in a double, and print with %.1f.";

const EX01_SOURCE: &str = r#"/* ex01: The broken average
 * TASK: print the average of 9 and 4 as a decimal: 6.5
 * Fix this file, then press Enter in the game to check it.
 */
#include <stdio.h>

int main(void) {
    int a = 9, b = 4;
    int avg = (a + b) / 2;   /* something truncates here...      */
    printf("%d\n", avg);    /* ...and this specifier can't do 6.5 */
    return 0;
}
"#;

const EX00_STUB: &str = r#"/* Ex00: The Answer
 * TASK: make my_answer give back the number 42.
 * Do NOT add a main() — the game brings its own test program.
 */
int my_answer(void)
{
    return 0;    /* <-- that is not the Answer. Fix me! */
}
"#;

struct Question {
    prompt: &'static str,
    code: Option<&'static str>,
    choices: &'static [&'static str],
    answer: &'static str,
    explain: &'static str,
}

const LEVEL1_QUESTIONS: [Question; 4] = [
    Question {
        prompt: "Which function is the entry point of every C program?",
        code: None,
        choices: &["a) start()", "b) main()", "c) init()", "d) run()"],
        answer: "b",
        explain: "Execution always begins at main(). Its return value becomes the\nprogram's exit status (0 usually means success).",
    },
    Question {
        prompt: "What does this print? (type the exact output)",
        code: Some(
            "#include <stdio.h>\nint main(void) {\n    printf(\"%d\", 2 + 3 * 4);\n    return 0;\n}",
        ),
        choices: &[],
        answer: "14",
        explain: "Multiplication binds tighter than addition: 2 + (3*4) = 14.",
    },
    Question {
        prompt: "Which format specifier prints an int?",
        code: None,
        choices: &["a) %f", "b) %s", "c) %d", "d) %c"],
        answer: "c",
        explain: "%d (or %i) is for int, %f for double, %s for strings, %c for a\nsingle character. Mismatched specifiers are undefined behavior!",
    },
    Question {
        prompt: "What is the value of x after this line? (type a number)",
        code: Some("int x = 7 / 2;"),
        choices: &[],
        answer: "3",
        explain: "Integer division truncates toward zero: 7/2 is 3, not 3.5.\nUse 7.0/2 or (double)7/2 to get 3.5.",
    },
];

fn print_code(code: &str, c: &Colors) {
    println!("{}  ┌──────────────────────────────────────┐{}", c.dim, c.reset);
    for line in code.split('\n') {
        println!("{}  │{} {}{}{}", c.dim, c.reset, c.cyan, line, c.reset);
    }
    println!("{}  └──────────────────────────────────────┘{}", c.dim, c.reset);
}

struct Game {
    colors: Colors,
    input: Input,
    progress_path: PathBuf,
    lvl_done: usize,
    lib_done: usize,
}

impl Game {
    fn new() -> Self {
        let use_color = env::var_os("NO_COLOR").is_none();
        let home = env::var("HOME").unwrap_or_else(|_| ".".to_string());
        let progress_path = PathBuf::from(home).join(".cgame_progress");
        let (lvl_done, lib_done) = Self::load_progress(&progress_path);
        Self {
            colors: Colors::new(use_color),
            input: Input::new(),
            progress_path,
            lvl_done,
            lib_done,
        }
    }

    fn load_progress(path: &PathBuf) -> (usize, usize) {
        let mut lvl = 0usize;
        let mut lib = 0usize;
        if let Ok(text) = fs::read_to_string(path) {
            let mut toks = text.split_whitespace();
            if let Some(t) = toks.next() {
                lvl = t.parse::<i64>().unwrap_or(0).max(0) as usize;
            }
            if let Some(t) = toks.next() {
                lib = t.parse::<i64>().unwrap_or(0).max(0) as usize;
            }
        }
        (
            lvl.min(N_LEVELS),
            lib.min(N_LIBEX),
        )
    }

    fn save_progress(&self) {
        let content = format!("{} {}\n", self.lvl_done, self.lib_done);
        let _ = fs::write(&self.progress_path, content);
    }

    fn banner(&self) {
        let c = self.colors;
        print!("\n{}{}", c.bold, c.cyan);
        println!("   ██████╗ ██████╗  █████╗ ███╗   ███╗███████╗");
        println!("  ██╔════╝██╔════╝ ██╔══██╗████╗ ████║██╔════╝");
        println!("  ██║     ██║  ███╗███████║██╔████╔██║█████╗  ");
        println!("  ██║     ██║   ██║██╔══██║██║╚██╔╝██║██╔══╝  ");
        println!("  ╚██████╗╚██████╔╝██║  ██║██║ ╚═╝ ██║███████╗");
        println!("   ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝");
        print!("{}        Learn C, in C — gotchas included\n{}", c.dim, c.reset);
        println!(
            "\n  Gotcha levels: {}/{}    Library built: {}/{}",
            self.lvl_done, N_LEVELS, self.lib_done, N_LIBEX
        );
    }

    fn main_menu(&self) {
        let c = self.colors;
        println!("\n{}Choose your path:{}", c.bold, c.reset);
        println!(
            "  {}1{}) Gotcha levels — quizzes + fix broken code  ({}/{})",
            c.bold, c.reset, self.lvl_done, N_LEVELS
        );
        println!(
            "  {}2{}) Build your own C library — write stdlib\n     functions from scratch, zero experience needed  ({}/{})",
            c.bold, c.reset, self.lib_done, N_LIBEX
        );
        println!(
            "  {}r{}) reset all progress    {}q{}) quit",
            c.bold, c.reset, c.bold, c.reset
        );
        print!("{}> {}", c.yellow, c.reset);
        let _ = io::stdout().flush();
    }

    fn ask(&mut self, q: &Question, num: usize, total: usize) -> bool {
        let c = self.colors;
        println!("\n{}Question {}/{}{}", c.bold, num, total, c.reset);
        println!("{}", q.prompt);
        if let Some(code) = q.code {
            print_code(code, &c);
        }
        for choice in q.choices {
            println!("  {}", choice);
        }
        if q.choices.is_empty() {
            print!("{}Your answer: {}", c.yellow, c.reset);
        } else {
            print!("{}Your answer (a-e): {}", c.yellow, c.reset);
        }
        let _ = io::stdout().flush();
        let answer = self.input.read_line().unwrap_or_default().to_lowercase();
        let expected = q.answer.to_lowercase();
        let ok = answer == expected;
        if ok {
            println!("{}✔ Correct!{}", c.green, c.reset);
        } else {
            println!("{}✘ Not quite. Answer: {}{}", c.red, q.answer, c.reset);
        }
        println!("{}{}{}", c.dim, q.explain, c.reset);
        ok
    }

    fn sandbox(&mut self, idx: usize) -> bool {
        if idx >= 1 {
            return true; // no exercise modeled beyond ex01
        }
        let c = self.colors;
        // write_exercise(ex, 0)
        let _ = fs::create_dir_all("exercises");
        let _ = fs::write("exercises/ex01_average.c", EX01_SOURCE);
        println!(
            "\n{}{}────── Sandbox: fix the broken code! ──────{}",
            c.bold, c.mag, c.reset
        );
        println!(
            "A broken C file has been written to:\n  {}{}exercises/{}{}",
            c.bold, c.cyan, "ex01_average.c", c.reset
        );
        println!("\nOpen it in your editor and fix it so that:\n{}{}{}\n", c.green, EX01_TASK, c.reset);
        println!(
            "{}[Enter]{} check my fix   {}[h]{} hint   {}[r]{} reset file\n\
             {}[s]{} skip exercise      {}[q]{} back to menu",
            c.bold, c.reset, c.bold, c.reset, c.bold, c.reset,
            c.bold, c.reset, c.bold, c.reset
        );
        loop {
            print!("{}sandbox> {}", c.yellow, c.reset);
            let _ = io::stdout().flush();
            let line = self.input.read_line().unwrap_or_default();
            let key = line.as_bytes().first().copied().unwrap_or(0);
            match key {
                b'\0' => {
                    // check_build on the untouched broken stub: gcc fails.
                    println!("{}✘ It doesn't compile yet.{}", c.red, c.reset);
                    println!(
                        "{}Edit exercises/ex01_average.c and press Enter to re-check.{}",
                        c.dim, c.reset
                    );
                }
                b'h' => println!("{}Hint: {}{}", c.cyan, EX01_HINT, c.reset),
                b'r' => {
                    let _ = fs::write("exercises/ex01_average.c", EX01_SOURCE);
                    println!("File reset to its original broken state.");
                }
                b's' => {
                    println!("{}Skipped — but the bug is still out there...{}", c.dim, c.reset);
                    return true;
                }
                b'q' => return false,
                _ => println!("Enter, h, r, s, or q."),
            }
        }
    }

    fn play_level(&mut self, idx: usize) -> bool {
        let c = self.colors;
        println!(
            "\n{}{}══════ Level {}: {} ══════{}",
            c.bold,
            c.mag,
            idx + 1,
            LEVEL_TITLES[idx],
            c.reset
        );
        println!("\n{}", LEVEL1_LESSON);
        print!(
            "{}(You need 3/4 correct to pass. Press Enter to start){}",
            c.dim, c.reset
        );
        let _ = io::stdout().flush();
        let _ = self.input.read_line();

        let mut score = 0;
        if idx == 0 {
            for (i, q) in LEVEL1_QUESTIONS.iter().enumerate() {
                score += self.ask(q, i + 1, 4) as usize;
            }
        }
        print!("\n{}Score: {}/4{} — ", c.bold, score, c.reset);
        if score < 3 {
            println!("{}not enough this time. Try again!{}", c.red, c.reset);
            return false;
        }
        println!("{}quiz passed!{} Now prove it in code...", c.green, c.reset);
        if !self.sandbox(idx) {
            println!(
                "{}Sandbox left unsolved — level not completed yet.{}",
                c.yellow, c.reset
            );
            return false;
        }
        println!("\n{}{}LEVEL {} COMPLETE! 🎉{}", c.bold, c.green, idx + 1, c.reset);
        true
    }

    fn levels_menu(&self) {
        let c = self.colors;
        println!("\n{}Gotcha levels:{}", c.bold, c.reset);
        for (i, title) in LEVEL_TITLES.iter().enumerate() {
            if i < self.lvl_done {
                println!("  {}✔{} {:2}) {}", c.green, c.reset, i + 1, title);
            } else if i == self.lvl_done {
                println!("  {}▶{} {:2}) {}", c.yellow, c.reset, i + 1, title);
            } else {
                println!("  {}🔒 {:2}) {}{}", c.dim, i + 1, title, c.reset);
            }
        }
        print!(
            "\n  Type a level number to play (unlocked/current only),\n  or {}b{} to go back to the main menu.\n",
            c.bold, c.reset
        );
    }

    fn levels_path(&mut self) {
        let c = self.colors;
        loop {
            self.levels_menu();
            print!("{}levels> {}", c.yellow, c.reset);
            let _ = io::stdout().flush();
            let line = self.input.read_line().unwrap_or_default();
            if line.starts_with('b') || line.starts_with('q') {
                return;
            }
            let choice = c_atoi(&line);
            if choice < 1 || choice > N_LEVELS as i32 {
                println!(
                    "{}Please enter 1-{} or b.{}",
                    c.red, N_LEVELS, c.reset
                );
                continue;
            }
            let ci = (choice - 1) as usize;
            if ci > self.lvl_done {
                println!(
                    "{}That level is still locked — beat level {} first!{}",
                    c.red, self.lvl_done + 1, c.reset
                );
                continue;
            }
            if self.play_level(ci) && ci == self.lvl_done {
                self.lvl_done += 1;
                self.save_progress();
                if self.lvl_done == N_LEVELS {
                    println!(
                        "\n{}{}🏆 ALL LEVELS COMPLETE — you know C's dark corners now!{}",
                        c.bold, c.green, c.reset
                    );
                }
            }
        }
    }

    fn lib_menu(&self) {
        let c = self.colors;
        println!("\n{}Build your own C library:{}", c.bold, c.reset);
        for (i, title) in LIBEX_TITLES.iter().enumerate() {
            if i < self.lib_done {
                println!("  {}✔{} {:2}) {}", c.green, c.reset, i, title);
            } else if i == self.lib_done {
                println!("  {}▶{} {:2}) {}", c.yellow, c.reset, i, title);
            } else {
                println!("  {}🔒 {:2}) {}{}", c.dim, i, title, c.reset);
            }
        }
        print!(
            "\n  Type an exercise number (0-{}, unlocked/current only),\n  or {}b{} to go back to the main menu.\n",
            N_LIBEX - 1,
            c.bold,
            c.reset
        );
    }

    fn play_libex(&mut self, idx: usize) -> bool {
        let c = self.colors;
        println!(
            "\n{}{}══════ {} ══════{}",
            c.bold, c.mag, LIBEX_TITLES[idx], c.reset
        );
        println!("\n{}", EX00_THEORY);
        print!(
            "\n{}(Press Enter when you're ready to write it yourself){}",
            c.dim, c.reset
        );
        let _ = io::stdout().flush();
        let _ = self.input.read_line();
        let _ = fs::create_dir_all("library");
        let _ = fs::write("library/ex00_answer.c", EX00_STUB);
        println!("\nYour file is waiting at:\n  {}{}library/{}{}", c.bold, c.cyan, "ex00_answer.c", c.reset);
        println!("Open it in your editor, write the function, come back.");
        if idx != 0 {
            // Only Ex00 is modeled; later exercises each have a `forbidden`
            // stdlib name and print this extra warning line.
            println!(
                "{}(Calling the real {}() is detected and rejected!){}",
                c.dim, "library_function", c.reset
            );
        }
        println!(
            "\n{}[Enter]{} check my code   {}[t]{} show theory again   {}[h]{} hint\n\
             {}[r]{} reset file          {}[s]{} skip   {}[q]{} back to menu",
            c.bold, c.reset, c.bold, c.reset, c.bold, c.reset,
            c.bold, c.reset, c.bold, c.reset, c.bold, c.reset
        );
        loop {
            print!("{}library> {}", c.yellow, c.reset);
            let _ = io::stdout().flush();
            let line = self.input.read_line().unwrap_or_default();
            let key = line.as_bytes().first().copied().unwrap_or(0);
            match key {
                b'\0' => {
                    println!("{}✘ It doesn't compile yet.{}", c.red, c.reset);
                    println!(
                        "{}Edit library/ex00_answer.c and press Enter to re-check.{}",
                        c.dim, c.reset
                    );
                }
                b't' => println!("\n{}", EX00_THEORY),
                b'h' => println!("{}Hint:\n{}{}", c.cyan, EX01_HINT, c.reset),
                b'r' => {
                    let _ = fs::write("library/ex00_answer.c", EX00_STUB);
                    println!("File reset to the empty starting version.");
                }
                b's' => {
                    println!("{}Skipped — the library builds itself, apparently.{}", c.dim, c.reset);
                    return true;
                }
                b'q' => return false,
                _ => println!("Enter, t, h, r, s, or q."),
            }
        }
    }

    fn library_path(&mut self) {
        let c = self.colors;
        loop {
            self.lib_menu();
            print!("{}library> {}", c.yellow, c.reset);
            let _ = io::stdout().flush();
            let line = self.input.read_line().unwrap_or_default();
            if line.starts_with('b') || line.starts_with('q') {
                return;
            }
            if !line.as_bytes().first().map_or(false, |b| b.is_ascii_digit()) {
                println!("{}Please enter 0-{} or b.{}", c.red, N_LIBEX - 1, c.reset);
                continue;
            }
            let choice = c_atoi(&line);
            if choice < 0 || choice >= N_LIBEX as i32 {
                println!("{}Please enter 0-{} or b.{}", c.red, N_LIBEX - 1, c.reset);
                continue;
            }
            let ci = choice as usize;
            if ci > self.lib_done {
                println!(
                    "{}Still locked — finish Ex{:02} first!{}",
                    c.red, self.lib_done, c.reset
                );
                continue;
            }
            if self.play_libex(ci) && ci == self.lib_done {
                println!("\n{}{}Ex{:02} COMPLETE! 🎉{}", c.bold, c.green, ci, c.reset);
                self.lib_done += 1;
                self.save_progress();
                if self.lib_done == N_LIBEX {
                    println!(
                        "\n{}{}🏆 YOU BUILT YOUR OWN C LIBRARY — from zero to strchr!{}",
                        c.bold, c.green, c.reset
                    );
                }
            }
        }
    }

    fn run(&mut self) {
        self.banner();
        let c = self.colors;
        loop {
            self.main_menu();
            let line = self.input.read_line().unwrap_or_default();
            let key = line.as_bytes().first().copied().unwrap_or(0);
            if key == b'q' || key == b'Q' {
                println!("Progress saved. Happy hacking!");
                break;
            }
            if key == b'r' || key == b'R' {
                self.lvl_done = 0;
                self.lib_done = 0;
                self.save_progress();
                println!("Progress reset.");
                continue;
            }
            if key == b'1' {
                self.levels_path();
            } else if key == b'2' {
                self.library_path();
            } else {
                println!("{}Please enter 1, 2, r, or q.{}", c.red, c.reset);
            }
        }
    }
}

fn main() {
    let mut game = Game::new();
    game.run();
}
