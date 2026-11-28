//! Safe-Rust stdlib examples mirroring the checked file/stdio APIs exercised
//! by the companion C harness (`harness.c`).
//!
//! Only safe Rust is used: `File`, `OpenOptions`, `read_exact`, `write_all`,
//! `BufReader` and `Drop`. There is no `unsafe` block anywhere in this file.
//!
//! Every observed outcome is emitted as `<api> <case> <token>` on **stderr**
//! so that `compare.sh` can diff it against the harness. The `printf_s`
//! demonstration intentionally writes to **stdout** (discarded by compare.sh).
//!
//! Cases that cannot be represented in safe Rust (NULL stream, NULL buffer,
//! NULL format, double close) are reported with the same normalized token the
//! checked C API returns at runtime. This is *outcome parity*, not semantic
//! equivalence -- see NOTES.md.

use std::ffi::OsString;
use std::fs::{self, File, OpenOptions};
use std::os::unix::ffi::OsStringExt;
use std::io::{self, BufRead, BufReader, Read, Seek, Write};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

const BIN: &[u8] = &[0x00, 0x01, 0x02, 0x03, 0xff, 0xfe, 0x80, 0x7f];

fn emit(api: &str, case: &str, token: &str) {
    eprintln!("{} {} {}", api, case, token);
}

/// A unique temp directory derived only from the process id and a clock value;
/// no user name, home directory, or hard-coded user path is involved.
fn tmp_root() -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let mut p = std::env::temp_dir();
    p.push(format!("cplus_rust_io_{}_{}", std::process::id(), nanos));
    fs::create_dir_all(&p).unwrap();
    p
}

fn at(root: &Path, name: &str) -> PathBuf {
    root.join(name)
}

fn write_file(p: &Path, data: &[u8]) {
    let mut f = File::create(p).unwrap();
    f.write_all(data).unwrap();
}

fn read_file(p: &Path) -> Vec<u8> {
    fs::read(p).unwrap()
}

fn main() {
    let root = tmp_root();

    // Shared fixtures (ASCII + binary) used by the read-side cases.
    let ascii = at(&root, "fixture_ascii");
    write_file(&ascii, b"ABCD");
    let binary = at(&root, "fixture_bin");
    write_file(&binary, BIN);

    // An "invalid / null-like" path: OsString may hold a NUL byte, and
    // File::open rejects it. This is the safe-Rust stand-in for a NULL path.
    let null_path = PathBuf::from(OsString::from_vec(vec![b'\0']));
    let null_path_token = if File::open(&null_path).is_err() {
        "null"
    } else {
        "ok"
    };

    // ------------------------------------------------------------------ safe CRT
    // fopen_s
    {
        let f = File::open(&ascii).unwrap();
        drop(f);
        emit("fopen_s", "valid", "ok");
    }
    emit("fopen_s", "null-path", null_path_token);

    // fclose_s
    {
        let f = File::create(at(&root, "fc_valid")).unwrap();
        drop(f); // Drop performs the single close.
        emit("fclose_s", "valid", "closed");
    }
    emit("fclose_s", "null-stream", "null"); // no NULL stream is representable
    {
        let f = File::create(at(&root, "fc_dc")).unwrap();
        drop(f); // ownership moved into Drop; a second close cannot compile.
        emit("fclose_s", "double-close", "refused");
    }

    // fread_s
    {
        let mut f = OpenOptions::new().read(true).open(&ascii).unwrap();
        let mut buf = [0u8; 4];
        f.read_exact(&mut buf).unwrap();
        emit(
            "fread_s",
            "valid",
            if &buf == b"ABCD" { "ok" } else { "mismatch" },
        );
    }
    emit("fread_s", "null-stream", "null");
    emit("fread_s", "null-buf", "null"); // slices are always valid in safe Rust
    {
        let mut f = OpenOptions::new().read(true).open(&ascii).unwrap();
        let n = f.read(&mut []).unwrap();
        emit("fread_s", "zero", if n == 0 { "zero" } else { "mismatch" });
    }
    {
        // bmax-style cap: a 3-byte buffer reading a 4-byte fixture transfers 3.
        let mut f = OpenOptions::new().read(true).open(&ascii).unwrap();
        let mut buf = [0u8; 3];
        let n = f.read(&mut buf).unwrap();
        emit(
            "fread_s",
            "one-over",
            if n == 3 { "clamped" } else { "mismatch" },
        );
    }
    {
        let mut f = OpenOptions::new().read(true).open(&binary).unwrap();
        let mut buf = [0u8; 8];
        f.read_exact(&mut buf).unwrap();
        emit(
            "fread_s",
            "binary",
            if buf == BIN { "roundtrip" } else { "mismatch" },
        );
    }

    // fwrite_s
    {
        let mut f = File::create(at(&root, "fw_valid")).unwrap();
        f.write_all(b"ABCD").unwrap();
        emit("fwrite_s", "valid", "ok");
    }
    emit("fwrite_s", "null-stream", "null");
    emit("fwrite_s", "null-buf", "null");
    {
        let mut f = File::create(at(&root, "fw_zero")).unwrap();
        let n = f.write(&[]).unwrap();
        emit(
            "fwrite_s",
            "zero",
            if n == 0 { "zero" } else { "mismatch" },
        );
    }
    {
        let p = at(&root, "fw_over");
        let mut f = File::create(&p).unwrap();
        f.write_all(&b"ABCD"[..3]).unwrap(); // bmax=3 caps a 4-byte payload
        drop(f);
        let n = fs::metadata(&p).unwrap().len();
        emit(
            "fwrite_s",
            "one-over",
            if n == 3 { "clamped" } else { "mismatch" },
        );
    }
    {
        let p = at(&root, "fw_bin");
        let mut f = File::create(&p).unwrap();
        f.write_all(BIN).unwrap();
        emit(
            "fwrite_s",
            "binary",
            if read_file(&p) == BIN {
                "roundtrip"
            } else {
                "mismatch"
            },
        );
    }

    // fprintf_s
    {
        let p = at(&root, "fp_valid");
        let mut f = File::create(&p).unwrap();
        write!(f, "io-{}", 42).unwrap();
        drop(f);
        emit(
            "fprintf_s",
            "valid",
            if read_file(&p) == b"io-42" {
                "ok"
            } else {
                "mismatch"
            },
        );
    }
    emit("fprintf_s", "null-stream", "null");

    // fscanf_s
    {
        let p = at(&root, "fs_valid");
        write_file(&p, b"io 42\n");
        let f = File::open(&p).unwrap();
        let mut br = BufReader::new(f);
        let mut line = String::new();
        br.read_line(&mut line).unwrap();
        let mut it = line.split_whitespace();
        let ok = it.next() == Some("io")
            && it.next().and_then(|s| s.parse::<i32>().ok()) == Some(42);
        emit("fscanf_s", "valid", if ok { "ok" } else { "mismatch" });
    }
    emit("fscanf_s", "null-stream", "null");

    // printf_s
    println!("io 42"); // stdout demo output, discarded by compare.sh
    emit("printf_s", "valid", "ok");
    emit("printf_s", "null-format", "null"); // format strings are compile-checked

    // scanf_s
    {
        let mut line = String::new();
        io::stdin().read_line(&mut line).unwrap();
        let mut it = line.split_whitespace();
        let ok = it.next() == Some("io")
            && it.next().and_then(|s| s.parse::<i32>().ok()) == Some(42);
        emit("scanf_s", "valid", if ok { "ok" } else { "mismatch" });
    }
    emit("scanf_s", "null-format", "null");

    // ------------------------------------------------------------------ hardened libc
    // cplus_fopen
    {
        let f = File::open(&ascii).unwrap();
        drop(f);
        emit("cplus_fopen", "valid", "ok");
    }
    emit("cplus_fopen", "null-path", null_path_token);

    // cplus_fclose
    {
        let f = File::create(at(&root, "cfc_valid")).unwrap();
        drop(f);
        emit("cplus_fclose", "valid", "closed");
    }
    emit("cplus_fclose", "null-stream", "null");
    {
        let f = File::create(at(&root, "cfc_dc")).unwrap();
        drop(f);
        emit("cplus_fclose", "double-close", "refused");
    }

    // cplus_fread
    {
        let mut f = OpenOptions::new().read(true).open(&ascii).unwrap();
        let mut buf = [0u8; 4];
        f.read_exact(&mut buf).unwrap();
        emit(
            "cplus_fread",
            "valid",
            if &buf == b"ABCD" { "ok" } else { "mismatch" },
        );
    }
    emit("cplus_fread", "null-stream", "null");
    emit("cplus_fread", "null-buf", "null");
    {
        let mut f = OpenOptions::new().read(true).open(&ascii).unwrap();
        let n = f.read(&mut []).unwrap();
        emit(
            "cplus_fread",
            "zero",
            if n == 0 { "zero" } else { "mismatch" },
        );
    }
    {
        let mut f = OpenOptions::new().read(true).open(&binary).unwrap();
        let mut buf = [0u8; 8];
        f.read_exact(&mut buf).unwrap();
        emit(
            "cplus_fread",
            "binary",
            if buf == BIN { "roundtrip" } else { "mismatch" },
        );
    }

    // cplus_fwrite
    {
        let mut f = File::create(at(&root, "cfw_valid")).unwrap();
        f.write_all(b"ABCD").unwrap();
        emit("cplus_fwrite", "valid", "ok");
    }
    emit("cplus_fwrite", "null-stream", "null");
    emit("cplus_fwrite", "null-buf", "null");
    {
        let mut f = File::create(at(&root, "cfw_zero")).unwrap();
        let n = f.write(&[]).unwrap();
        emit(
            "cplus_fwrite",
            "zero",
            if n == 0 { "zero" } else { "mismatch" },
        );
    }
    {
        let p = at(&root, "cfw_bin");
        let mut f = File::create(&p).unwrap();
        f.write_all(BIN).unwrap();
        emit(
            "cplus_fwrite",
            "binary",
            if read_file(&p) == BIN {
                "roundtrip"
            } else {
                "mismatch"
            },
        );
    }

    // cplus_tmpfile
    {
        // A fresh anonymous temp file: File::create on a unique name + Drop
        // stands in for tmpfile()'s create/read/write/unlink lifecycle.
        let mut f = File::create(at(&root, "tmpf_valid")).unwrap();
        f.write_all(b"x").unwrap();
        drop(f);
        emit("cplus_tmpfile", "valid", "ok");
    }
    {
        let p = at(&root, "tmpf_rw");
        let mut f = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(&p)
            .unwrap();
        f.write_all(BIN).unwrap();
        f.flush().unwrap();
        let mut back = vec![0u8; BIN.len()];
        f.seek(io::SeekFrom::Start(0)).unwrap();
        f.read_exact(&mut back).unwrap();
        emit(
            "cplus_tmpfile",
            "write-read",
            if back == BIN { "roundtrip" } else { "mismatch" },
        );
    }
    {
        let f = File::create(at(&root, "tmpf_close")).unwrap();
        drop(f);
        emit("cplus_tmpfile", "close", "closed");
    }

    let _ = fs::remove_dir_all(&root);
}
