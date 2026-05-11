#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_bin> <output_hpp" << std::endl;
        return 1;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "Could not open input file: " << argv[1] << "\n";
        std::cerr << "errno: " << errno << "\n";
        std::cerr << "reason: " << std::strerror(errno) << "\n";
        return 1;
    }

    std::ofstream output(argv[2]);
    if (!output) {
        std::cerr << "Could not open output file: " << argv[2] << std::endl;
        return 1;
    }

    // Read data from bin file
    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    output << "#pragma once\n";
    output << "#include <cstdint>\n";
    output << "#include <cstddef>\n\n";
    output << "inline const uint8_t OPENING_BOOK[] {\n    ";

    for (size_t i=0; i<buffer.size(); ++i) {
        output << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[i] << ", ";
        if (((i + 1) % 12) == 0) {
            output << "\n    ";
        }
    }

    output << "\n};\n\n";
    output << "inline constexpr size_t OPENING_BOOK_SIZE = " << std::dec << buffer.size() << ";\n";

    return 0; 
}