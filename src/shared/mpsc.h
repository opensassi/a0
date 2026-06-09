#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include <unistd.h>
#include <sys/eventfd.h>

#include "shared/resource_provider.h"

namespace a0::mpsc {

// ============================================================================
// Command types — sent from UI/CLI to AppCore
// ============================================================================

struct SubmitGoal {
    std::string goal;
};
struct Cancel {};
struct Shutdown {};
struct SetSession {
    int64_t sessionDbId;
    std::string sessionUuid;
};
struct ListSessions {
    int limit = 20;
};
struct ResumeSession {
    std::string uuid;
};
struct LoadResource {
    ResourceType type;
    int64_t id;
    int64_t offset;
    int64_t limit;
};

using Command = std::variant<SubmitGoal, Cancel, Shutdown, SetSession,
                             ListSessions, ResumeSession, LoadResource>;

// ============================================================================
// Event types — emitted by AppCore to UI/CLI
// ============================================================================

struct LlmStart {
    int64_t streamId;
    int roundSeq;
};

struct LlmChunk {
    int64_t streamId;
    int seq;
    std::string text;
    bool isFinal = false;
};

struct LlmComplete {
    int64_t streamId;
    std::string finishReason;
};

struct ToolStart {
    int64_t invocationId;
    std::string toolCallId;
    std::string toolName;
    std::string arguments;
};

struct ToolChunk {
    int64_t invocationId;
    int seq;
    std::string text;
    std::string streamType;
};

struct ToolEnd {
    int64_t invocationId;
    int exitCode = 0;
    int64_t totalBytes = 0;
    std::string outputPreview;
};

struct Complete {
    int64_t sessionId;
    std::string summary;
};

struct Error {
    std::string source;
    int64_t contextId = 0;
    std::string message;
};

/// Lightweight mirror of persistence::Message for MPSC transport.
/// The TUI receives this read-only and never writes to persistence.
struct SessionMessage {
    std::string role;
    std::string content;
    std::string toolCallId;
    std::string name;
    std::string resultJson;
    int64_t createdAt = 0;
};

struct SessionReady {
    int64_t dbId;
    std::string uuid;
};

struct SessionList {
    struct Entry {
        std::string uuid;
        int64_t dbId = 0;
        std::string startedAt;
        int messageCount = 0;
    };
    std::vector<Entry> entries;
};

struct SessionHistory {
    int64_t dbId = 0;
    std::string uuid;
    bool found = false;
    std::vector<SessionMessage> messages;
};

struct LoadResourceResult {
    int64_t id;
    std::string data;
};

using AppCoreEvent = std::variant<LlmStart, LlmChunk, LlmComplete,
                                  ToolStart, ToolChunk, ToolEnd,
                                  Complete, Error,
                                  SessionReady, SessionList, SessionHistory,
                                  LoadResourceResult>;

// ============================================================================
// B1Control variants — sent from c2 to b1 via Cap'n Proto IPC
// ============================================================================

struct FollowAgent {
    std::string sessionUuid;
};
struct UnfollowAgent {
    std::string sessionUuid;
};

using B1Control = std::variant<FollowAgent, UnfollowAgent>;

// ============================================================================
// MPSC Channel — multi-producer single-consumer with eventfd wakeup
// ============================================================================

template<typename T> class Sender;
template<typename T> class Receiver;
template<typename T> struct Channel;

namespace detail {

struct ChannelStateBase {
    std::mutex mutex;
    int event_fd;

    ChannelStateBase()
        : event_fd(eventfd(0, EFD_NONBLOCK))
    {}

    ~ChannelStateBase() {
        if (event_fd != -1) {
            close(event_fd);
        }
    }

    ChannelStateBase(const ChannelStateBase&) = delete;
    ChannelStateBase& operator=(const ChannelStateBase&) = delete;
};

} // namespace detail

// ---------------------------------------------------------------------------
// Sender<T> — cloneable, thread-safe (multiple producers)
// ---------------------------------------------------------------------------

template<typename T>
class Sender {
public:
    Sender() = default;

    Sender(const Sender& other)
        : m_state(other.m_state)
    {
        if (m_state) m_state->senders.fetch_add(1, std::memory_order_relaxed);
    }

    Sender& operator=(const Sender& other) {
        if (this != &other) {
            if (m_state) m_state->senders.fetch_sub(1, std::memory_order_relaxed);
            m_state = other.m_state;
            if (m_state) m_state->senders.fetch_add(1, std::memory_order_relaxed);
        }
        return *this;
    }

    Sender(Sender&& other) noexcept
        : m_state(std::move(other.m_state))
    {}

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            if (m_state) m_state->senders.fetch_sub(1, std::memory_order_relaxed);
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    void send(T value) {
        if (!m_state) return;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->queue.push_back(std::move(value));
        }
        uint64_t one = 1;
        write(m_state->event_fd, &one, sizeof(one));
    }

    /// Create a copy of this sender (same underlying channel).
    Sender clone() const { return Sender(*this); }

    bool valid() const { return m_state != nullptr; }

    ~Sender() {
        if (m_state) {
            m_state->senders.fetch_sub(1, std::memory_order_relaxed);
        }
    }

private:
    friend class Channel<T>;
    friend class Receiver<T>;

    struct State : detail::ChannelStateBase {
        std::deque<T> queue;
        std::atomic<int> senders{0};
    };

    std::shared_ptr<State> m_state;

    explicit Sender(std::shared_ptr<State> state)
        : m_state(std::move(state))
    {
        m_state->senders.fetch_add(1, std::memory_order_relaxed);
    }
};

// ---------------------------------------------------------------------------
// Receiver<T> — single consumer with poll()-compatible fd
// ---------------------------------------------------------------------------

template<typename T>
class Receiver {
public:
    Receiver() = default;

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    Receiver(Receiver&& other) noexcept
        : m_state(std::move(other.m_state))
    {}

    Receiver& operator=(Receiver&& other) noexcept {
        if (this != &other) {
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    /// File descriptor usable with poll()/epoll()/select().
    /// Readable when at least one item is available.
    int poll_fd() const {
        return m_state ? m_state->event_fd : -1;
    }

    /// Drain all available items. Non-blocking.
    /// Clears the eventfd counter and dequeues all pending items under lock.
    /// Safe to call from any thread (but designed for single consumer).
    std::vector<T> drain() {
        if (!m_state) return {};

        // Consume all accumulated wakeup signals.
        uint64_t val;
        while (read(m_state->event_fd, &val, sizeof(val)) > 0) {}

        // Dequeue all items under lock.
        std::vector<T> result;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            result.reserve(m_state->queue.size());
            while (!m_state->queue.empty()) {
                result.push_back(std::move(m_state->queue.front()));
                m_state->queue.pop_front();
            }
        }
        return result;
    }

    /// True while at least one Sender is alive.
    /// Once all senders are destroyed, this returns false permanently.
    bool connected() const {
        return m_state && m_state->senders.load(std::memory_order_relaxed) > 0;
    }

private:
    friend class Channel<T>;
    std::shared_ptr<typename Sender<T>::State> m_state;

    explicit Receiver(std::shared_ptr<typename Sender<T>::State> state)
        : m_state(std::move(state))
    {}
};

// ---------------------------------------------------------------------------
// Channel<T> — factory for Sender/Receiver pairs
// ---------------------------------------------------------------------------

template<typename T>
struct Channel {
    static std::pair<Sender<T>, Receiver<T>> create() {
        auto state = std::make_shared<typename Sender<T>::State>();
        return {Sender<T>(state), Receiver<T>(state)};
    }
};

} // namespace a0::mpsc
