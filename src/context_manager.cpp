#include "context_manager.h"
#include "trace.h"
#include <stdexcept>

void DefaultContextManager::push(const ContextFrame& frame) {
    TRACE_LOG("push(" << frame.role << ")");
    m_frames.push_back(frame);
    if (m_frames.size() > m_maxDepth) {
        m_frames.erase(m_frames.begin());
    }
}

ContextFrame DefaultContextManager::pop() {
    TRACE_LOG("pop()");
    if (m_frames.empty()) {
        throw std::out_of_range("pop on empty context");
    }
    ContextFrame top = m_frames.back();
    m_frames.pop_back();
    return top;
}

ContextFrame DefaultContextManager::peek() const {
    TRACE_LOG("peek()");
    if (m_frames.empty()) {
        throw std::out_of_range("peek on empty context");
    }
    return m_frames.back();
}

size_t DefaultContextManager::size() const {
    TRACE_LOG("size()=" << m_frames.size());
    return m_frames.size();
}

void DefaultContextManager::clear() {
    TRACE_LOG("clear()");
    m_frames.clear();
}

std::vector<ContextFrame> DefaultContextManager::snapshot() const {
    TRACE_LOG("snapshot()=" << m_frames.size());
    return m_frames;
}
