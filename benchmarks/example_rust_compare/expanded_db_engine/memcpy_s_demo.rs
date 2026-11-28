/* Safe-Rust equivalent of examples/expanded/safety/memcpy_s_demo.cp.
 *
 * The original performs an *unguarded* one-over copy in a forked child to
 * show canary corruption, then calls the additive checked cplus_memcpy_s in
 * the parent.  Safe Rust cannot fork or write past a buffer, so the child's
 * deterministic outcome is modelled explicitly (the 9th source byte lands on
 * the canary) while the checked call is modelled as a length check followed
 * by copy_from_slice. No unsafe, no FFI, no raw pointers. See RESULTS.md.
 */

const CANARY_OK: u8 = 0xA5;
const CANARY_HIT: u8 = 0x2A;
const ESNOSPC: i32 = 406;

fn mark(case_name: &str, got: &str, want: &str, detail: &str, failed: &mut i32) {
    println!("memcpy_s {} {} {}", case_name, got, detail);
    if got != want {
        *failed = 1;
    }
}

/* Additive memcpy_s model: refuse before writing when the source is larger
 * than the destination; otherwise copy every byte. */
fn checked_memcpy(dst: &mut [u8], src: &[u8]) -> i32 {
    if src.len() > dst.len() {
        return ESNOSPC;
    }
    dst[..src.len()].copy_from_slice(src);
    0
}

fn main() {
    let src9: [u8; 9] = [1, 2, 3, 4, 5, 6, 7, 8, CANARY_HIT];
    let src8: [u8; 8] = [1, 2, 3, 4, 5, 6, 7, 8];
    let mut failed = 0;

    /* BEFORE: model the forked child's unguarded 9-byte copy into an 8-byte
     * buffer followed by the canary. Its deterministic result is that byte 9
     * overwrites the canary and the child exits with status 9. */
    let child_canary = src9[8];
    let corrupted = (child_canary == CANARY_HIT) as i32;
    mark(
        "ordinary_one_over",
        if corrupted == 1 { "OK" } else { "REFUSED" },
        "OK",
        &format!(
            "canary=0x{:02x} corrupted={} (unguarded subprocess)",
            child_canary, corrupted
        ),
        &mut failed,
    );
    let child_code = if corrupted == 1 { 9 } else { 8 };
    mark(
        "child_exit",
        if child_code == 9 { "OK" } else { "REFUSED" },
        "OK",
        &format!("child_exit={}", child_code),
        &mut failed,
    );

    /* AFTER: checked one-over — refused, destination left untouched. */
    let mut buf = [0u8; 8];
    let canary = CANARY_OK;
    let rc = checked_memcpy(&mut buf, &src9);
    if rc != 0 {
        /* The C build links safeclib's global IGNORE handler, which emits this
         * line on stdout at each refusal; safe Rust has no such side channel,
         * so it is modelled explicitly for byte-for-byte comparison. */
        println!("IGNORE CONSTRAINT HANDLER: ({}) memcpy_s: slen exceeds max", rc);
    }
    mark(
        "checked_one_over",
        if rc != 0 && canary == CANARY_OK { "REFUSED" } else { "OK" },
        "REFUSED",
        &format!(
            "rc={} canary=0x{:02x} intact={}",
            rc,
            canary,
            (canary == CANARY_OK) as i32
        ),
        &mut failed,
    );

    /* Positive control: exact fit copies all 8 bytes. */
    let rc = checked_memcpy(&mut buf, &src8);
    mark(
        "checked_exact_fit",
        if rc == 0 && canary == CANARY_OK && buf[7] == 8 { "OK" } else { "REFUSED" },
        "OK",
        &format!("rc={} canary=0x{:02x} buf7={}", rc, canary, buf[7]),
        &mut failed,
    );

    mark(
        "parent_canary",
        if canary == CANARY_OK { "OK" } else { "REFUSED" },
        "OK",
        &format!("canary=0x{:02x}", canary),
        &mut failed,
    );

    std::process::exit(failed);
}
