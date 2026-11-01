# Making HTTP Requests in C++ with cpp-httplib

[cpp-httplib](https://github.com/yhirose/cpp-httplib) is a header-only HTTP/HTTPS
client and server library for C++11 and later. You add a single file, `httplib.h`,
and you're done: no separate build step and no dependencies other than OpenSSL,
which you only need for HTTPS.

This folder holds a runnable demo, [`main.cpp`](main.cpp). It starts a small local
server on a background thread, so every example runs without internet access.

## Build and run

### With CMake (downloads `httplib.h` for you)

```bash
cmake -S . -B build
```

```bash
cmake --build build
```

```bash
./build/http_demo
```

To add HTTPS support (needs `libssl-dev` / OpenSSL 3), configure with
`-DHTTPLIB_USE_OPENSSL=ON` and pass a host to try a real request:

```bash
./build/http_demo httpbin.org
```

### With g++ directly

```bash
curl -LO https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h
```

```bash
g++ -std=c++17 -I. main.cpp -o http_demo -pthread
```

For HTTPS, add `-DCPPHTTPLIB_OPENSSL_SUPPORT` and `-lssl -lcrypto`.

## Cheat sheet

```cpp
#include <httplib.h>

httplib::Client cli("http://localhost:8080");   // or "https://example.com" (needs OpenSSL)
cli.set_connection_timeout(2, 0);               // seconds, microseconds
cli.set_read_timeout(5, 0);
cli.set_follow_location(true);                  // follow 3xx redirects

// GET
auto res = cli.Get("/hello");

// GET with query params and headers
httplib::Params  params{{"q", "c++"}, {"page", "2"}};
httplib::Headers headers{{"Authorization", "Bearer TOKEN"}};
res = cli.Get("/search", params, headers);

// POST JSON
res = cli.Post("/items", R"({"name":"widget"})", "application/json");

// POST form (application/x-www-form-urlencoded)
res = cli.Post("/login", httplib::Params{{"user", "alice"}, {"password", "x"}});

// PUT / DELETE
res = cli.Put("/items/42", "new body", "text/plain");
res = cli.Delete("/items/42");
```

## Handling errors: two kinds of failure

Each call returns an `httplib::Result`. Check two things:

```cpp
if (!res) {
    // Transport failure: no HTTP response at all
    // (connection refused, DNS failure, timeout, TLS error, ...)
    std::cerr << httplib::to_string(res.error()) << '\n';
} else if (res->status != 200) {
    // The server answered, but with an error status (404, 500, ...)
    std::cerr << "HTTP " << res->status << '\n';
} else {
    std::cout << res->body << '\n';
}
```

The demo shows both cases: a `404` from the server, a read timeout, and a refused
connection.

## Tips

- **Reuse a `Client`** for many requests to the same host. Call
  `cli.set_keep_alive(true)` to keep the TCP connection open between them.
- **HTTPS** needs `CPPHTTPLIB_OPENSSL_SUPPORT` defined *before* including
  `httplib.h`, and the program must link against OpenSSL. Certificates are
  verified by default. Use `set_ca_cert_path()` for a custom CA bundle. Don't
  turn verification off in production.
- **Behind a proxy?** Use `cli.set_proxy(host, port)`. cpp-httplib ignores the
  `HTTP(S)_PROXY` environment variables.
- **Blocking API:** each call blocks the calling thread. To send several requests
  at once, run them on separate threads, each with its own `Client`.
- **Large downloads:** pass a content receiver callback to `Get` to stream the
  body in chunks instead of holding it all in memory.
