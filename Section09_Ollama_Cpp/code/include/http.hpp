#pragma once
// Thin RAII wrapper around libcurl: just enough HTTP for talking to LLM APIs.
#include <functional>
#include <string>
#include <vector>

namespace http {

struct Response {
    long status = 0;
    std::string body;
};

// Called with every chunk of bytes as it arrives (used for streaming).
using ChunkHandler = std::function<void(const char* data, size_t len)>;

// Call once at program start (RAII). Required by libcurl before any request.
class GlobalInit {
public:
    GlobalInit();
    ~GlobalInit();
    GlobalInit(const GlobalInit&) = delete;
    GlobalInit& operator=(const GlobalInit&) = delete;
};

Response get(const std::string& url,
             const std::vector<std::string>& headers = {},
             long timeoutSeconds = 30);

// If onChunk is set, the body is passed to it as it streams in and is NOT
// accumulated in Response::body.
Response post(const std::string& url,
              const std::string& body,
              const std::vector<std::string>& headers = {},
              const ChunkHandler& onChunk = nullptr,
              long timeoutSeconds = 600);

}  // namespace http
