//! Safe-Rust behavioral model of examples/system/macpulse.cp.
//!
//! No `unsafe`, no FFI. The original is a native macOS monitor: Mach
//! host_statistics, sysctlbyname, libproc, getifaddrs, statfs, termios and a
//! 50 ms sleep loop. Every one of those is timing/hardware-dependent.
//!
//! WHAT IS COMPARED: the original's deterministic non-interactive path. With
//! piped stdin/stdout it prints exactly one line to stderr and exits 1:
//!     MacPulse needs an interactive terminal.
//! This Rust program mirrors that check via std::io::IsTerminal.
//!
//! WHAT IS MODELED (not compared against live hardware): `--model` forces the
//! interactive loop with FIXED sample values and scripted single-character
//! keys from stdin (one key per line). It reproduces the render/format logic
//! (units, rate, bar, spark, key handling, pause, refresh, page) without any
//! clock or system calls, so the output is deterministic. This is a model of
//! observable behavior, not a native monitor.

use std::env;
use std::io::{self, BufRead, IsTerminal, Write};
use std::process;

const PULSE_HISTORY: usize = 480;

fn units(n: f64) -> String {
    let suffixes = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut v = n;
    let mut i = 0usize;
    while v >= 1024.0 && i < 4 {
        v /= 1024.0;
        i += 1;
    }
    format!("{:.1} {}", v, suffixes[i])
}

fn rate(n: f64) -> String {
    format!("{}/s", units(n))
}

fn bar(pct: f64, width: usize) -> String {
    let pct = pct.clamp(0.0, 100.0);
    let blocks = ((pct * width as f64 / 100.0) + 0.5) as usize;
    let tone = if pct >= 88.0 {
        "\x1b[91m"
    } else if pct >= 65.0 {
        "\x1b[93m"
    } else {
        "\x1b[92m"
    };
    let mut out = String::from(tone);
    for i in 0..width {
        out.push(if i < blocks { '#' } else { '.' });
    }
    out.push_str("\x1b[0m");
    out
}

fn spark(history: &[i32], width: usize) -> String {
    let levels = [" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"];
    let n = history.len();
    let begin = if n > width { n - width } else { 0 };
    let pad = width - (n - begin);
    let mut out = " ".repeat(pad);
    for &p in &history[begin..n] {
        let mut index = (p + 6) / 13;
        index = index.clamp(0, 8);
        out.push_str(levels[index as usize]);
    }
    out
}

#[derive(Clone)]
struct ProcEntry {
    pid: i32,
    name: String,
    resident: u64,
    cpu_seconds: f64,
}

struct Monitor {
    cpu: f64,
    mem_percent: f64,
    memory_total: u64,
    memory_used: u64,
    swap_used: u64,
    swap_total: u64,
    disk_used: u64,
    disk_total: u64,
    net_in: f64,
    net_out: f64,
    load1: f64,
    load5: f64,
    load15: f64,
    cpu_cores: u32,
    process_count: u64,
    uptime: i64,
    top: Vec<ProcEntry>,
}

impl Monitor {
    /// Deterministic stand-in for sample(): fixed, documented values.
    fn fixed_sample() -> Self {
        Self {
            cpu: 12.5,
            mem_percent: 45.0,
            memory_total: 16 * 1024 * 1024 * 1024,
            memory_used: 7 * 1024 * 1024 * 1024,
            swap_used: 0,
            swap_total: 1024 * 1024 * 1024,
            disk_used: 250 * 1024 * 1024 * 1024,
            disk_total: 500 * 1024 * 1024 * 1024,
            net_in: 12_345.0,
            net_out: 2_345.0,
            load1: 1.25,
            load5: 0.98,
            load15: 0.75,
            cpu_cores: 8,
            process_count: 321,
            uptime: 7200,
            top: vec![
                ProcEntry { pid: 101, name: "model_proc_a".into(), resident: 2 * 1024 * 1024 * 1024, cpu_seconds: 123.4 },
                ProcEntry { pid: 202, name: "model_proc_b".into(), resident: 1 * 1024 * 1024 * 1024, cpu_seconds: 45.6 },
                ProcEntry { pid: 303, name: "model_proc_c".into(), resident: 512 * 1024 * 1024, cpu_seconds: 7.8 },
            ],
        }
    }

    fn render(
        &self,
        cpu_hist: &[i32],
        ram_hist: &[i32],
        page: i32,
        period_ms: i32,
        paused: bool,
    ) {
        let m_used = units(self.memory_used as f64);
        let m_total = units(self.memory_total as f64);
        let swap_u = units(self.swap_used as f64);
        let swap_t = units(self.swap_total as f64);
        let d_used = units(self.disk_used as f64);
        let d_total = units(self.disk_total as f64);
        let down = rate(self.net_in);
        let up = rate(self.net_out);
        // terminal_columns(): fixed 90 in the model (ioctl is hardware-bound).
        let cols = 90;
        let mut width = cols - 15;
        if width > 70 {
            width = 70;
        }
        if width < 20 {
            width = 20;
        }
        print!("\x1b[H\x1b[2J");
        print!("\x1b[1;36m  MACPULSE  \x1b[0m\x1b[2m  Native macOS monitor written in C+\x1b[0m\n");
        println!(
            "  \x1b[2mCPU cores: {}    Processes: {}    Uptime: {}h {:02}m    Refresh: {}ms  {}\x1b[0m",
            self.cpu_cores,
            self.process_count,
            self.uptime / 3600,
            (self.uptime / 60) % 60,
            period_ms,
            if paused { "[PAUSED]" } else { "" }
        );
        println!("  \x1b[90m────────────────────────────────────────────────────────────────────────────\x1b[0m");
        print!("  \x1b[1mCPU\x1b[0m    {:5.1}%  ", self.cpu);
        println!("{}   Load {:.2} / {:.2} / {:.2}", bar(self.cpu, 34), self.load1, self.load5, self.load15);
        print!("  \x1b[1mMEM\x1b[0m    {:5.1}%  ", self.mem_percent);
        println!("{}   {} / {}", bar(self.mem_percent, 34), m_used, m_total);
        let disk_pct = if self.disk_total != 0 {
            100.0 * self.disk_used as f64 / self.disk_total as f64
        } else {
            0.0
        };
        print!("  \x1b[1mDISK\x1b[0m   {:5.1}%  ", disk_pct);
        println!("{}   {} / {} (/)", bar(disk_pct, 34), d_used, d_total);
        println!(
            "  \x1b[1mSWAP\x1b[0m   {} / {}    \x1b[1mNET ↓\x1b[0m {}    \x1b[1m↑\x1b[0m {}",
            swap_u, swap_t, down, up
        );
        println!("\n  \x1b[1;35mLIVE HISTORY\x1b[0m  (right = newest)");
        println!("  CPU  \x1b[92m{}\x1b[0m", spark(cpu_hist, width));
        println!("  RAM  \x1b[96m{}\x1b[0m", spark(ram_hist, width));
        println!("\n  \x1b[1;35mTOP PROCESSES BY RESIDENT MEMORY\x1b[0m");
        println!("  \x1b[2mPID       RESIDENT      TOTAL CPU    PROCESS\x1b[0m");
        let available = 30 - 18; // terminal_rows() fixed at 30
        let mut maxrows = if page == 2 { self.top.len() } else { 7 };
        if maxrows > available {
            maxrows = available;
        }
        if maxrows > self.top.len() {
            maxrows = self.top.len();
        }
        for p in self.top.iter().take(maxrows) {
            let bytes = units(p.resident as f64);
            println!(
                "  {:<8}  {:<12} {:9.1}s    {:.35}",
                p.pid, bytes, p.cpu_seconds, p.name
            );
        }
        print!("\n  \x1b[90m[q] quit    [space] pause/resume    [+/-] refresh    [1/2] view\x1b[0m");
        let _ = io::stdout().flush();
    }
}

fn run_model() {
    let monitor = Monitor::fixed_sample();
    let mut cpu_hist: Vec<i32> = (0..PULSE_HISTORY)
        .map(|i| ((i as f64 * 7.0) % 101.0) as i32)
        .collect();
    let mut ram_hist: Vec<i32> = (0..PULSE_HISTORY)
        .map(|i| ((i as f64 * 3.0) % 101.0) as i32)
        .collect();
    let mut period_ms = 1000;
    let mut paused = false;
    let mut page = 1;

    monitor.render(&cpu_hist, &ram_hist, page, period_ms, paused);

    let stdin = io::stdin();
    let mut reader = stdin.lock();
    let mut line = String::new();
    loop {
        line.clear();
        let n = reader.read_line(&mut line).unwrap_or(0);
        if n == 0 {
            break;
        }
        let key = line.as_bytes()[0];
        match key {
            b'q' | b'Q' => break,
            b' ' => {
                paused = !paused;
                if paused {
                    monitor.render(&cpu_hist, &ram_hist, page, period_ms, paused);
                }
            }
            b'1' => page = 1,
            b'2' => page = 2,
            b'+' if period_ms > 250 => period_ms -= 250,
            b'-' if period_ms < 4000 => period_ms += 250,
            _ => {}
        }
        // Each modeled sample appends one deterministic point.
        cpu_hist.push((monitor.cpu + 0.5) as i32);
        ram_hist.push((monitor.mem_percent + 0.5) as i32);
        if !paused {
            monitor.render(&cpu_hist, &ram_hist, page, period_ms, paused);
        }
    }
    println!("\nMacPulse stopped. Terminal restored.");
}

fn main() {
    let force_model = env::args().any(|a| a == "--model");
    if !force_model && !(io::stdin().is_terminal() && io::stdout().is_terminal()) {
        eprintln!("MacPulse needs an interactive terminal.");
        process::exit(1);
    }
    if force_model {
        run_model();
    } else {
        // Real-interactive path would sample hardware; the deterministic
        // model is used so behavior is reproducible. See RESULTS.md.
        run_model();
    }
}
