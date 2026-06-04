#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "agent_interfaces.h"
#include "command_runner.h"

class MockAgentCore : public AgentCore {
public:
    struct ToolCallStep {
        std::string name;
        std::string output;
        bool success = true;
    };

    struct Scenario {
        std::vector<std::string> tokens;
        std::vector<ToolCallStep> toolCalls;
        std::string finalOutput;
        int tokenDelayMs = 5;
    };

    void setScenario(const Scenario& s) {
        m_scenario = s;
        if (m_scenario.tokens.empty())
            m_scenario.tokens = {"Hello ", "from ", "mock!"};
        if (m_scenario.finalOutput.empty()) {
            for (const auto& t : m_scenario.tokens)
                m_scenario.finalOutput += t;
        }
    }

    void setTokenDelayMs(int ms) {
        m_scenario.tokenDelayMs = ms;
    }

    bool init(const std::string&) override { return true; }

    json processGoal(const std::string& goal) override {
        return {{"response", m_scenario.finalOutput}, {"goal", goal}};
    }

    bool resumeSession(const std::string& sessionId) override {
        m_sessionId = sessionId;
        return true;
    }

    std::string currentSessionId() const override {
        return m_sessionId;
    }

    void run() override {}

    bool ensureSession() override {
        if (m_sessionId.empty())
            m_sessionId = "mock-" + std::to_string(std::time(nullptr));
        return true;
    }

    int64_t sessionDbId() const override {
        return m_sessionDbId;
    }

    a0::StreamHandle processGoalStreaming(const std::string& goal,
                                           a0::StreamCallback onChunk) override {
        a0::StreamHandle handle;
        handle.m_state = std::make_shared<a0::StreamHandle::State>();

        auto capturedScenario = m_scenario;
        handle.m_state->thread = std::thread([this, capturedScenario, onChunk]() {
            for (const auto& token : capturedScenario.tokens) {
                if (m_cancelled.load()) return;
                if (onChunk) onChunk(token, "stdout");
                if (capturedScenario.tokenDelayMs > 0)
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(capturedScenario.tokenDelayMs));
            }

            for (const auto& tc : capturedScenario.toolCalls) {
                if (m_cancelled.load()) return;
                if (onChunk) onChunk("{\"tool_start\":\"" + tc.name + "\"}", "tool");
                if (capturedScenario.tokenDelayMs > 0)
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(capturedScenario.tokenDelayMs));
                if (onChunk) onChunk(tc.output, tc.success ? "tool_end" : "tool_error");
            }

            if (!m_cancelled.load() && !capturedScenario.finalOutput.empty()) {
                if (onChunk) onChunk(capturedScenario.finalOutput, "stdout");
            }
        });

        return handle;
    }

    void cancel() { m_cancelled.store(true); }

private:
    Scenario m_scenario;
    std::string m_sessionId;
    int64_t m_sessionDbId = 100;
    std::atomic<bool> m_cancelled{false};
};
