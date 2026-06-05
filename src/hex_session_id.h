#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <array>
#include <cstdint>

inline std::string generateHexSessionId() {
    std::array<uint32_t, 4> buf{};
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(buf.data()), buf.size() * sizeof(uint32_t));
    }
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint32_t val : buf) {
        ss << std::setw(8) << val;
    }
    return ss.str();
}
