/* SAFE-Rust model of examples/modern_gui/dashboard/dashboard.cp
 *
 * This is a teaching model, not a GUI application.  It reproduces the
 * deterministic observable core of the C+ example:
 *   - the fixed three-scenario table (title / labels / buttons / states)
 *   - the pure `dash_describe` transcript string
 *   - the window-dimension and scenario-id refusal model (category/code/message)
 *   - the simulated headless render transcript
 *
 * No SDL, no Nuklear, no unsafe, no FFI, no wall clock and no randomness.
 * The real C+ example does open a dummy SDL3 window on `render`; this model
 * deliberately reports the same *observable transcript* without a display.
 */

#[derive(Clone, Copy)]
struct Scenario {
    id: i32,
    title: &'static str,
    l0: &'static str,
    l1: &'static str,
    l2: &'static str,
    b0: &'static str,
    b1: &'static str,
    b2: &'static str,
    s0: bool,
    s1: bool,
    s2: bool,
}

const MAX_DIM: i32 = 4096;

fn scenario(id: i32) -> Option<Scenario> {
    match id {
        0 => Some(Scenario {
            id: 0,
            title: "overview",
            l0: "CPU",
            l1: "MEM",
            l2: "NET",
            b0: "RUN",
            b1: "STOP",
            b2: "RESET",
            s0: true,
            s1: false,
            s2: true,
        }),
        1 => Some(Scenario {
            id: 1,
            title: "operations",
            l0: "TASKS",
            l1: "QUEUE",
            l2: "ERRORS",
            b0: "START",
            b1: "PAUSE",
            b2: "CLEAR",
            s0: false,
            s1: true,
            s2: false,
        }),
        2 => Some(Scenario {
            id: 2,
            title: "diagnostics",
            l0: "TEMP",
            l1: "FAN",
            l2: "PWR",
            b0: "ACK",
            b1: "MUTE",
            b2: "RESET",
            s0: true,
            s1: true,
            s2: false,
        }),
        _ => None,
    }
}

fn on_off(v: bool) -> &'static str {
    if v {
        "ON"
    } else {
        "OFF"
    }
}

fn describe(s: &Scenario) -> String {
    format!(
        "DASH_UI id={} title={} labels={},{},{} buttons={}:{},{}:{},{}:{}",
        s.id,
        s.title,
        s.l0,
        s.l1,
        s.l2,
        s.b0,
        on_off(s.s0),
        s.b1,
        on_off(s.s1),
        s.b2,
        on_off(s.s2)
    )
}

/* The C+ error* refusal values, modelled as plain data. */
struct Refusal {
    category: i32,
    code: i32,
    message: &'static str,
}

fn invalid(msg: &'static str) -> Refusal {
    Refusal {
        category: 1,
        code: 108,
        message: msg,
    }
}

fn range(msg: &'static str) -> Refusal {
    Refusal {
        category: 2,
        code: 101,
        message: msg,
    }
}

fn validate_dims(width: i32, height: i32) -> Option<Refusal> {
    if width <= 0 || height <= 0 {
        Some(invalid("dashboard: window dimensions must be positive"))
    } else if width > MAX_DIM || height > MAX_DIM {
        Some(range("dashboard: window dimensions exceed DASH_MAX_DIM"))
    } else {
        None
    }
}

fn scenario_error() -> Refusal {
    range("scenario: id out of range")
}

fn print_refusal(tag: &str, r: &Refusal) {
    println!(
        "DASH_REFUSED {} category {} code {} message {}",
        tag, r.category, r.code, r.message
    );
}

fn parse_i32(text: Option<&str>) -> Option<i32> {
    text.and_then(|t| t.parse::<i32>().ok())
}

fn tag(kind: &str, a: i32, b: i32, c: i32) -> String {
    format!("{}:{},{},{}", kind, a, b, c)
}

fn cmd_describe(args: &[&str]) {
    /* C+ initialises id to 0 and short-circuits the parse chain. */
    let parsed = if args.is_empty() {
        None
    } else {
        parse_i32(args.get(0).copied())
    };
    let id = parsed.unwrap_or(0);
    let t = tag("describe", id, 0, 0);
    match parsed.and_then(scenario) {
        Some(sc) => {
            println!("{}", describe(&sc));
            println!("DASH_RESULT {} OK 0", t);
        }
        None => {
            let r = if parsed.is_none() {
                invalid("describe: missing or invalid scenario id")
            } else {
                scenario_error()
            };
            print_refusal(&t, &r);
            println!("DASH_RESULT {} REFUSED {}", t, r.code);
        }
    }
}

fn cmd_render(args: &[&str]) {
    /* Each C+ parse writes its slot only when reached; && short-circuits, so a
     * failed parse leaves all later slots at their initial zero. */
    let mut width = 0;
    let mut height = 0;
    let mut id = 0;
    let mut parsed = args.len() >= 3;
    if parsed {
        match parse_i32(args.get(0).copied()) {
            Some(v) => width = v,
            None => parsed = false,
        }
    }
    if parsed {
        match parse_i32(args.get(1).copied()) {
            Some(v) => height = v,
            None => parsed = false,
        }
    }
    if parsed {
        match parse_i32(args.get(2).copied()) {
            Some(v) => id = v,
            None => parsed = false,
        }
    }
    let t = tag("render", width, height, id);

    if !parsed {
        let r = invalid("render: missing or invalid arguments");
        print_refusal(&t, &r);
        println!("DASH_RESULT {} REFUSED {}", t, r.code);
        return;
    }

    let sc = match scenario(id) {
        Some(sc) => sc,
        None => {
            let r = scenario_error();
            print_refusal(&t, &r);
            println!("DASH_RESULT {} REFUSED {}", t, r.code);
            return;
        }
    };

    /* The C+ source emits the shared description before attempting the frame. */
    println!("{}", describe(&sc));

    if let Some(r) = validate_dims(width, height) {
        print_refusal(&t, &r);
        println!("DASH_RESULT {} REFUSED {}", t, r.code);
        return;
    }

    println!(
        "DASH_RENDER {} {} {} shown 1 widgets 6 frame 1",
        width, height, id
    );
    println!("DASH_RESULT {} OK 0", t);
}

fn cmd_refuse(args: &[&str]) {
    let name = args.get(0).copied().unwrap_or("missing");
    let r = match name {
        "zero-width" => invalid("dashboard: window dimensions must be positive"),
        "negative-height" => invalid("dashboard: window dimensions must be positive"),
        "oversized" => range("dashboard: window dimensions exceed DASH_MAX_DIM"),
        "bad-scenario" => scenario_error(),
        "null-context" => invalid("gui: invalid window or renderer"),
        _ => invalid("refuse: unknown case"),
    };
    print_refusal(name, &r);
    println!("DASH_RESULT refuse {} REFUSED {}", name, r.code);
}

fn main() {
    let owned: Vec<String> = std::env::args().skip(1).collect();
    let argv: Vec<&str> = owned.iter().map(|s| s.as_str()).collect();
    let cmd = argv.get(0).copied().unwrap_or("describe");
    let rest = if argv.is_empty() { &argv[..] } else { &argv[1..] };
    match cmd {
        "describe" => cmd_describe(rest),
        "render" => cmd_render(rest),
        "refuse" => cmd_refuse(rest),
        _ => {
            let r = invalid("dashboard: unknown command");
            print_refusal("command", &r);
            println!("DASH_RESULT command REFUSED {}", r.code);
        }
    }
}
