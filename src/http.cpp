#include "http.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define FLIGHTO_POPEN _popen
#define FLIGHTO_PCLOSE _pclose
#define FLIGHTO_POPEN_MODE "rb"
#else
#define FLIGHTO_POPEN popen
#define FLIGHTO_PCLOSE pclose
#define FLIGHTO_POPEN_MODE "r"
#endif

namespace flighto {

namespace {

const char kStatusMarker[] = "\nFLIGHTO_HTTP_STATUS:";

bool safe_for_shell(const std::string& s, bool allow_space) {
    if (s.empty()) return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':' ||
                        c == '/' || c == '?' || c == '&' || c == '=' || c == '.' || c == '_' || c == '-' ||
                        (allow_space && c == ' ');
        if (!ok) return false;
    }
    return true;
}

}  // namespace

HttpResponse CurlHttpClient::get(const std::string& url, const std::vector<std::string>& headers) {
    if (!safe_for_shell(url, false)) return {0, "refused to request a URL with unexpected characters"};
    for (const auto& h : headers) {
        if (!safe_for_shell(h, true)) return {0, "refused to send a header with unexpected characters"};
    }

    // -sS: no progress bar, but still print errors. --retry also covers HTTP
    // 429 and 5xx and honors Retry-After. The status code is appended after
    // a marker line so it can be split from the body.
    std::string cmd = "curl -sS --compressed --max-time " + std::to_string(timeout_seconds_) +
                      " --retry 2 --retry-delay 2";
    for (const auto& h : headers) cmd += " -H \"" + h + "\"";
    cmd += " -w \"\\nFLIGHTO_HTTP_STATUS:%{http_code}\" \"" + url + "\" 2>&1";

    std::FILE* pipe = FLIGHTO_POPEN(cmd.c_str(), FLIGHTO_POPEN_MODE);
    if (!pipe) return {0, "could not start curl"};
    std::string out;
    char buf[8192];
    std::size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, pipe)) > 0) out.append(buf, got);
    FLIGHTO_PCLOSE(pipe);

    const auto pos = out.rfind(kStatusMarker);
    if (pos == std::string::npos) {
        // curl did not run (not installed) or died before writing anything.
        return {0, out.empty() ? "curl produced no output (is curl installed?)" : out};
    }
    HttpResponse response;
    response.status = std::atoi(out.c_str() + pos + sizeof(kStatusMarker) - 1);
    response.body = out.substr(0, pos);
    return response;
}

}  // namespace flighto
