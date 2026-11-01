// Making HTTP requests in C++ with cpp-httplib
// https://github.com/yhirose/cpp-httplib
//
// The demo starts a tiny local server on a background thread so every client
// example can run offline. Pass a URL host (e.g. "httpbin.org") as argv[1] to
// also try a real HTTPS request (requires building with OpenSSL).

#include <httplib.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

// ---------------------------------------------------------------------------
// Local test server (so the client examples have something to talk to)
// ---------------------------------------------------------------------------
void setup_server(httplib::Server& svr) {
    svr.Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello from cpp-httplib!", "text/plain");
    });

    // Echo query parameters and a custom header back to the caller.
    svr.Get("/search", [](const httplib::Request& req, httplib::Response& res) {
        std::string body = "q=" + req.get_param_value("q") +
                           ", page=" + req.get_param_value("page") +
                           ", X-Api-Key=" + req.get_header_value("X-Api-Key");
        res.set_content(body, "text/plain");
    });

    // Echo the request body and its content type.
    svr.Post("/items", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 201;
        res.set_content(req.body, req.get_header_value("Content-Type"));
    });

    svr.Post("/login", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content("user=" + req.get_param_value("user"), "text/plain");
    });

    svr.Put("/items/42", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content("updated: " + req.body, "text/plain");
    });

    svr.Delete("/items/42", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    svr.Get("/slow", [](const httplib::Request&, httplib::Response& res) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        res.set_content("finally", "text/plain");
    });
}

// Print the outcome of any request. A Result converts to false when the
// request failed at the transport level (connection refused, timeout, ...).
void print_result(const std::string& label, const httplib::Result& res) {
    std::cout << "== " << label << " ==\n";
    if (!res) {
        std::cout << "  error: " << httplib::to_string(res.error()) << "\n\n";
        return;
    }
    std::cout << "  status: " << res->status << "\n";
    if (res->has_header("Content-Type")) {
        std::cout << "  content-type: " << res->get_header_value("Content-Type") << "\n";
    }
    std::cout << "  body: " << res->body << "\n\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    // Start the local server on a free port.
    httplib::Server svr;
    setup_server(svr);
    const int port = svr.bind_to_any_port("127.0.0.1");
    std::thread server_thread([&] { svr.listen_after_bind(); });
    svr.wait_until_ready();

    // One Client object can be reused for many requests to the same host.
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);  // 2 seconds
    cli.set_read_timeout(5, 0);        // 5 seconds

    // 1. Simple GET
    print_result("GET /hello", cli.Get("/hello"));

    // 2. GET with query parameters and custom headers
    httplib::Params params{{"q", "c++ http"}, {"page", "2"}};
    httplib::Headers headers{{"X-Api-Key", "secret-123"}};
    print_result("GET /search with params + headers",
                 cli.Get("/search", params, headers));

    // 3. POST a JSON body
    const std::string json = R"({"name":"widget","price":9.99})";
    print_result("POST /items (JSON)", cli.Post("/items", json, "application/json"));

    // 4. POST an HTML form (application/x-www-form-urlencoded)
    httplib::Params form{{"user", "alice"}, {"password", "hunter2"}};
    print_result("POST /login (form)", cli.Post("/login", form));

    // 5. PUT and DELETE
    print_result("PUT /items/42", cli.Put("/items/42", "price=12.50", "text/plain"));
    print_result("DELETE /items/42", cli.Delete("/items/42"));

    // 6. 404 from the server: the request succeeded, check res->status
    print_result("GET /missing", cli.Get("/missing"));

    // 7. Timeout: transport-level error, res is falsy
    {
        httplib::Client impatient("127.0.0.1", port);
        impatient.set_read_timeout(1, 0);
        print_result("GET /slow with 1s read timeout", impatient.Get("/slow"));
    }

    // 8. Connection refused: nothing is listening on this port
    {
        httplib::Client nobody("127.0.0.1", 1);
        nobody.set_connection_timeout(1, 0);
        print_result("GET on closed port", nobody.Get("/"));
    }

    // 9. Optional: real HTTPS request, e.g. ./http_demo httpbin.org
    if (argc > 1) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        // Scheme + host form picks SSLClient automatically for https://.
        httplib::Client https_cli(std::string("https://") + argv[1]);
        https_cli.set_follow_location(true);  // follow 3xx redirects
        print_result(std::string("HTTPS GET https://") + argv[1] + "/get",
                     https_cli.Get("/get"));
#else
        (void)argv;
        std::cout << "Rebuild with OpenSSL (-DHTTPLIB_USE_OPENSSL=ON) for HTTPS.\n";
#endif
    }

    svr.stop();
    server_thread.join();
    return 0;
}
