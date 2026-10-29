#include "fare_provider.h"

namespace flighto {

std::string normalize_iata(const std::string& code) {
    if (code.size() != 3) return {};
    std::string out(3, ' ');
    for (std::size_t i = 0; i < 3; ++i) {
        const char c = code[i];
        if (c >= 'a' && c <= 'z') {
            out[i] = static_cast<char>(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            out[i] = c;
        } else {
            return {};
        }
    }
    return out;
}

}  // namespace flighto
