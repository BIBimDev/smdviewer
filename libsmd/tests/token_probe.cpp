#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        if (!input) return 2;
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        std::cout << smd::tokensToJsonV1(smd::tokenizeSmd(text)) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
