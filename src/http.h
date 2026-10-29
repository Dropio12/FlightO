// Minimal HTTP GET by running the system `curl` executable.
//
// Windows 10 and later ship curl.exe, and every Linux and macOS machine has
// curl, so nothing needs to be linked. The interface is small so tests can
// swap in a fake client and never touch the network.
#pragma once

#include <string>
#include <vector>

namespace flighto {

struct HttpResponse {
    int status = 0;     // HTTP status code; 0 when the request never got an answer
    std::string body;   // response body (or curl's error text when status is 0)
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const std::string& url, const std::vector<std::string>& headers) = 0;
};

class CurlHttpClient : public HttpClient {
public:
    explicit CurlHttpClient(int timeout_seconds = 30) : timeout_seconds_(timeout_seconds) {}

    // The command goes through the shell, so the URL and headers must only
    // use a small safe character set (letters, digits and :/?&=._-, plus
    // spaces in headers). Anything else is refused with status 0.
    // Headers are curl arguments, so while a request runs they show up in
    // the process list of the machine (relevant for an API token header).
    HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override;

private:
    int timeout_seconds_;
};

}  // namespace flighto
