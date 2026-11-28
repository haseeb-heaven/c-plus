// Safe-Rust equivalent of demos/E_networking.c.
//
// Observable behavior modeled:
//   * bind a TCP listener on 127.0.0.1:0 and report the chosen port
//   * a self-contained client connects, sends a minimal HTTP/1.0 GET request,
//     reads the reply and reports only its first line
//   * the server accepts, reads the request, reports only its first line,
//     sends the HTTP response, waits for the client, then closes
//
// No unsafe and no FFI: std::net::{TcpListener, TcpStream} and std::thread are
// safe APIs.  RAII cleanup of the listener and connection is provided by the
// std types' own Drop impls, which play the role of C+'s compiler-inserted
// Listener_drop / Connection_drop scope cleanups.
//
// Differences from the C+ original are inherent to the demo:
//   - the ephemeral port (bind port 0) is chosen by the OS on every run;
//   - client/server message ordering is not deterministic (the C demo forks a
//     process whose dprintf is unbuffered while the server's printf is block
//     buffered when stdout is redirected).
// compare.sh therefore canonicalizes the port and line order for this example.

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;

/// Owning listener wrapper: dropped at scope exit (mirrors C+ `Listener`).
struct Listener {
    inner: TcpListener,
}

/// Owning connection wrapper: dropped at scope exit (mirrors C+ `Connection`).
struct Connection {
    inner: TcpStream,
}

/// Print only the first line of `buf`, truncating at '\n' but keeping any
/// preceding '\r', exactly like the original's `strchr(reply, '\n')` + `*nl=0`.
fn print_first_line(prefix: &str, buf: &[u8]) {
    let end = buf.iter().position(|&b| b == b'\n').unwrap_or(buf.len());
    let mut out = std::io::stdout().lock();
    out.write_all(prefix.as_bytes()).unwrap();
    out.write_all(&buf[..end]).unwrap();
    out.write_all(b"\n").unwrap();
}

fn client_main(port: u16) {
    let mut stream = TcpStream::connect(("127.0.0.1", port)).expect("connect failed");
    let request = b"GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    stream.write_all(request).unwrap();

    let mut reply = [0u8; 127]; // sizeof(reply) - 1 in the original
    let n = stream.read(&mut reply).unwrap();
    if n > 0 {
        print_first_line("client received: ", &reply[..n]);
    }
    // `stream` is closed here by RAII, matching the child's close(fd).
}

fn main() {
    let server = Listener {
        inner: TcpListener::bind("127.0.0.1:0").expect("cannot create listener"),
    };
    let port = server.inner.local_addr().unwrap().port();
    println!("listening on 127.0.0.1:{}", port);

    // Bounded join: the child thread is always joined before main returns.
    let client = thread::spawn(move || client_main(port));

    let (stream, _peer) = server.inner.accept().expect("accept failed");
    let mut conn = Connection { inner: stream };

    let mut request = [0u8; 511]; // sizeof(request) - 1 in the original
    let n = conn.inner.read(&mut request).unwrap();
    if n > 0 {
        print_first_line("server received: ", &request[..n]);
    }

    let response = b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    conn.inner.write_all(response).unwrap();

    client.join().expect("client thread panicked");
    println!("server closing connection");
    // `conn` and `server` are closed here by RAII (Connection_drop/Listener_drop).
}
