#pragma once

#include "agent_interfaces.h"

class JsonLinesLogger : public InvocationLogger {
public:
    JsonLinesLogger(const std::string& logDir = "logs");

    void log(const LogEntry& entry) override;
    bool replay(const std::string& sessionId,
                std::function<void(const LogEntry&)> callback) override;
    std::vector<std::string> listSessions() const override;
    bool exportSession(const std::string& sessionId,
                        const std::string& outputPath) const override;

private:
    std::string m_logDir;
};
