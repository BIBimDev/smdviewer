#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        auto tokens = smd::tokenizeSmd("\n  #10125\r\n\"St\\xfctze\\x00\"\n-0\n1e999\n-1e-999\n");
        require(tokens.size() == 5 && tokens[0].line == 2, "token line/storage");
        require(std::get<std::string>(tokens[1].value) == std::string("St\xc3\xbctze\0",8), "hex decoding");
        require(std::signbit(std::get<double>(tokens[2].value)), "signed zero must survive in memory");
        require(std::isinf(std::get<double>(tokens[3].value)), "overflow must survive in memory");
        require(std::signbit(std::get<double>(tokens[4].value)), "underflow signed zero");
        require(smd::tokensToJsonV1(tokens).find("\"value\":null") != std::string::npos, "nonfinite JSON");
        for (const std::string invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x80"}) {
            try { smd::tokenizeSmd(invalid); throw std::logic_error("missing UTF-8 error"); }
            catch (const std::runtime_error& e) { require(std::string(e.what()) == "SMD text must be valid UTF-8.", "UTF-8 error"); }
        }
        const smd::Transform3x4 t{{1,2,3,4,5,6,7,8,9,10,11,12}};
        const auto v = smd::applyTransform({2,3,4},t);
        require(v.x == 24 && v.y == 64 && v.z == 104, "row-major affine transform");
        try { smd::parseSmd10125(""); throw std::logic_error("missing error"); }
        catch (const std::runtime_error& e) { require(std::string(e.what()) == "#10125 main geometry block was not found.", "error precedence"); }
        std::cout << "Native API, ownership, transform and nonfinite checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
