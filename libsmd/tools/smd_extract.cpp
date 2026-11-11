#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: smd-extract <file.smd>\n"; return 2; }
    try {
        std::ifstream file(argv[1],std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open input file.");
        const std::string text{std::istreambuf_iterator<char>(file), {}};
        if (file.bad()) throw std::runtime_error("Cannot read input file.");
        const auto json = smd::toJsonV1(smd::parseSmd10125(text));
        std::cout << json << '\n';
        if (!std::cout) throw std::runtime_error("Cannot write JSON output.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
