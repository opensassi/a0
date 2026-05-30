#include "invocation_logger.h"
#include "trace.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

JsonLinesLogger::JsonLinesLogger(const std::string& logDir)
    : m_logDir(logDir) {}

void JsonLinesLogger::log(const LogEntry& entry) {
    TRACE_LOG("log(" << entry.sessionId << "," << entry.eventType << ")");
    fs::create_directories(m_logDir);
    std::string path = m_logDir + "/" + entry.sessionId + ".jsonl";
    std::ofstream file(path, std::ios::app);
    if (!file) return;

    json j;
    j["sessionId"] = entry.sessionId;
    j["timestamp"] = entry.timestamp;
    j["eventType"] = entry.eventType;
    j["data"] = entry.data;
    file << j.dump() << std::endl;
}

bool JsonLinesLogger::replay(const std::string& sessionId,
                              std::function<void(const LogEntry&)> callback) {
    TRACE_LOG("replay(" << sessionId << ")");
    std::string path = m_logDir + "/" + sessionId + ".jsonl";
    std::ifstream file(path);
    if (!file) return false;

    std::string line;
    while (std::getline(file, line)) {
        try {
            json j = json::parse(line);
            LogEntry entry;
            entry.sessionId = j.value("sessionId", "");
            entry.timestamp = j.value("timestamp", (int64_t)0);
            entry.eventType = j.value("eventType", "");
            entry.data = j.value("data", "");
            callback(entry);
        } catch (...) {
            // skip corrupt lines
        }
    }
    return true;
}

std::vector<std::string> JsonLinesLogger::listSessions() const {
    TRACE_LOG("listSessions()");
    std::vector<std::string> sessions;
    std::string pattern = m_logDir;
    if (!fs::exists(pattern)) return sessions;

    for (const auto& entry : fs::directory_iterator(pattern)) {
        std::string filename = entry.path().filename().string();
        if (filename.size() > 6 && filename.substr(filename.size() - 6) == ".jsonl") {
            sessions.push_back(filename.substr(0, filename.size() - 6));
        }
    }
    return sessions;
}

bool JsonLinesLogger::exportSession(const std::string& sessionId,
                                     const std::string& outputPath) const {
    TRACE_LOG("exportSession(" << sessionId << ")");
    std::string inPath = m_logDir + "/" + sessionId + ".jsonl";
    std::ifstream inFile(inPath);
    if (!inFile) return false;

    std::ofstream outFile(outputPath);
    if (!outFile) return false;

    outFile << "[\n";
    bool first = true;
    std::string line;
    while (std::getline(inFile, line)) {
        if (!first) outFile << ",\n";
        outFile << line;
        first = false;
    }
    outFile << "\n]\n";
    return !first;
}
