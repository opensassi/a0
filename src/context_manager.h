#pragma once

#include "agent_interfaces.h"

class DefaultContextManager : public ContextManager {
public:
    void push(const ContextFrame& frame) override;
    ContextFrame pop() override;
    ContextFrame peek() const override;
    size_t size() const override;
    void clear() override;
    std::vector<ContextFrame> snapshot() const override;

private:
    std::vector<ContextFrame> m_frames;
    static constexpr size_t m_maxDepth = 1000;
};
