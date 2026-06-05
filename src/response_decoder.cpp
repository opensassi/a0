#include "response_decoder.h"
#include <cstring>

using json = nlohmann::json;

namespace a0 {

void ResponseDecoder::reset() {
    m_buffer.clear();
    m_mode = Mode::Unknown;
    m_complete = false;
    m_accumToolName.clear();
    m_accumToolArgs.clear();
    m_accumToolId.clear();
    m_events.clear();
}

void ResponseDecoder::feed(const char* data, size_t len) {
    if (m_complete || len == 0) return;

    // Detect mode on first non-empty data
    if (m_mode == Mode::Unknown) {
        if (len >= 6 && std::memcmp(data, "data: ", 6) == 0) {
            m_mode = Mode::SSE;
        } else {
            m_mode = Mode::JSON;
        }
    }

    if (m_mode == Mode::SSE) {
        // SSE mode: buffer until newline, then process each line
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];
            if (c == '\n') {
                xFlushLine(m_buffer);
                m_buffer.clear();
            } else {
                m_buffer += c;
            }
        }
    } else {
        // JSON mode: accumulate complete body, parse at finish
        m_buffer.append(data, len);
    }
}

void ResponseDecoder::xFlushLine(const std::string& line) {
    // Empty line = event separator (blank line between SSE events)
    if (line.empty()) return;

    // "data: [DONE]" = stream complete
    if (line == "data: [DONE]") {
        m_complete = true;
        return;
    }

    // Skip non-data lines (e.g., ":" keepalive comments)
    if (line.compare(0, 6, "data: ") != 0) return;

    std::string payload = line.substr(6);

    // Skip empty data payloads
    if (payload.empty()) return;

    try {
        json j = json::parse(payload);
        xProcessJsonChunk(j);
    } catch (const json::parse_error&) {
        // Ignore malformed JSON chunks
    }
}

void ResponseDecoder::xProcessJsonChunk(const nlohmann::json& j) {
    // Check for error
    if (j.contains("error")) {
        std::string msg = "API error";
        if (j["error"].is_string()) {
            msg = j["error"].get<std::string>();
        } else if (j["error"].is_object() && j["error"].contains("message")) {
            msg = j["error"]["message"].get<std::string>();
        }
        m_events.push_back(mpsc::Error{msg});
        m_complete = true;
        return;
    }

    // Extract choices[0]
    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        return;
    }

    const json& choice = j["choices"][0];

    // Check finish_reason
    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
        std::string reason = choice["finish_reason"].get<std::string>();
        if (reason == "stop" || reason == "length") {
            m_complete = true;

            // Emit ToolStart for any tool_calls in the message (non-streaming responses)
            if (choice.contains("message") && choice["message"].contains("tool_calls") &&
                choice["message"]["tool_calls"].is_array()) {
                for (const auto& tc : choice["message"]["tool_calls"]) {
                    std::string id = tc.value("id", "");
                    std::string name;
                    std::string args;
                    if (tc.contains("function")) {
                        name = tc["function"].value("name", "");
                        args = tc["function"].value("arguments", "");
                    }
                    if (!name.empty()) {
                        m_events.push_back(mpsc::ToolStart{name, args});
                    }
                }
            }

            // Find the assistant message content for non-streaming complete
            if (choice.contains("message") && choice["message"].contains("content") &&
                !choice["message"]["content"].is_null()) {
                std::string content = choice["message"]["content"].get<std::string>();
                if (!content.empty()) {
                    m_events.push_back(mpsc::Complete{content});
                }
            }

            // If we didn't emit Complete via message.content but finish_reason is "stop", emit empty Complete
            if (m_events.empty() || !std::holds_alternative<mpsc::Complete>(m_events.back())) {
                m_events.push_back(mpsc::Complete{""});
            }
            return;
        }
    }

    // Extract delta (streaming) or message (non-streaming)
    json delta;
    bool isStreaming = choice.contains("delta");
    bool isMessage = choice.contains("message");

    if (isStreaming) {
        delta = choice["delta"];
    } else if (isMessage) {
        delta = choice["message"];
    } else {
        return;
    }

    // Content (text tokens)
    if (delta.contains("content") && !delta["content"].is_null()) {
        std::string text = delta["content"].get<std::string>();
        if (!text.empty()) {
            m_events.push_back(mpsc::LlmToken{text});
        }
    }

    // Tool calls
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc : delta["tool_calls"]) {
            std::string id = tc.value("id", "");
            std::string name;
            std::string args;
            if (tc.contains("function")) {
                name = tc["function"].value("name", "");
                args = tc["function"].value("arguments", "");
            }

            if (!name.empty()) {
                m_events.push_back(mpsc::ToolStart{name, args});
            }
        }
    }
}

void ResponseDecoder::xFlushBuffer() {
    if (m_mode == Mode::JSON && !m_buffer.empty()) {
        try {
            json j = json::parse(m_buffer);
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                json wrapped;
                wrapped["choices"] = json::array();
                wrapped["choices"].push_back({
                    {"message", j["choices"][0]["message"]},
                    {"finish_reason", j["choices"][0].value("finish_reason", "stop")}
                });
                xProcessJsonChunk(wrapped);
            } else {
                m_events.push_back(mpsc::Complete{j.dump()});
            }
        } catch (const nlohmann::json::parse_error&) {
            m_events.push_back(mpsc::Error{"Failed to parse LLM response"});
        }
        m_complete = true;
        m_buffer.clear();
    }
}

std::vector<mpsc::AppCoreEvent> ResponseDecoder::events() {
    // Flush any remaining buffered data (for JSON mode)
    if (!m_complete && m_mode == Mode::JSON) {
        xFlushBuffer();
    }

    auto result = std::move(m_events);
    m_events.clear();
    return result;
}

std::vector<mpsc::AppCoreEvent> ResponseDecoder::decodeJson(const std::string& body) {
    ResponseDecoder dec;
    dec.feed(body);
    return dec.events();
}

} // namespace a0
