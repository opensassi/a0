#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <random>

inline std::string generateHexSessionId() {
    thread_local std::random_device rd;
    thread_local std::mt19937 rng(rd());
    thread_local std::uniform_int_distribution<int> dist(0, 255);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        ss << std::setw(2) << dist(rng);
    }
    return ss.str();
}
