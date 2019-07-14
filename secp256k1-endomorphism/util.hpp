#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

inline std::mt19937_64 &global_urng() {
    static std::mt19937_64 mt64{};
    return mt64;
}

inline void randomize() {
    static std::random_device rd{};
    global_urng().seed(rd());
}

template <typename ForwardIter>
inline void print_bytes(const char *str, const ForwardIter beg,
                        const ForwardIter end) {
    printf("%s: ", str);
    for (auto p = beg; p != end; ++p)
        printf("%02x", static_cast<uint16_t>(*p) & 0xff);
    printf("\n");
}

template <typename WriteByteIter>
inline void rand_bytes(WriteByteIter beg, WriteByteIter end) {
    std::uniform_int_distribution<unsigned char> uiuc;
    for (auto iter = beg; iter != end; ++iter) *iter = uiuc(global_urng());
}

template <typename Func, typename Clock = std::chrono::steady_clock,
          typename Unit = std::chrono::milliseconds>
inline double perf(Func f) {
    auto t1 = Clock::now();
    f();
    auto t2 = Clock::now();
    return std::chrono::duration_cast<Unit>(t2 - t1).count();
}

signed char is_hex_digit(char c) {
    static const signed char p_util_hexdigit[256] = {
        // clang-format off
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  1,   2,   3,   4,   5,   6,   7,  8,  9,  -1, -1, -1, -1, -1, -1,
    -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  -1,  -1,  -1,  -1,  -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1,
        // clang-format on
    };
    return p_util_hexdigit[(uint8_t)c];
}

bool is_hex_str(const std::string &str) {
    for (std::string::const_iterator it(str.begin()); it != str.end(); ++it) {
        if (is_hex_digit(*it) < 0) return false;
    }
    return (str.size() > 0) && (str.size() % 2 == 0);
}

bool is_hex_number(const std::string &str) {
    size_t starting_location = 0;
    if (str.size() > 2 && *str.begin() == '0' && *(str.begin() + 1) == 'x') {
        starting_location = 2;
    }
    for (auto c : str.substr(starting_location)) {
        if (is_hex_digit(c) < 0) return false;
    }
    // Return false for empty string or "0x".
    return (str.size() > starting_location);
}

std::vector<uint8_t> parse_hex(const char *psz) {
    // convert hex dump to vector
    std::vector<uint8_t> vch;
    while (true) {
        while (isspace(*psz)) psz++;
        signed char c = is_hex_digit(*psz++);
        if (c == (signed char)-1) break;
        uint8_t n = (c << 4);
        c = is_hex_digit(*psz++);
        if (c == (signed char)-1) break;
        n |= c;
        vch.push_back(n);
    }
    return vch;
}

std::vector<uint8_t> parse_hex(const std::string &str) {
    return parse_hex(str.c_str());
}
