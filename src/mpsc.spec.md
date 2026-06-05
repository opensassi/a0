# MPSC Spec

## 1. Overview

Header-only multi-producer single-consumer channel with eventfd wakeup. `Channel<T>::create()` returns a `Sender<T>`/`Receiver<T>` pair. Senders are cloneable and thread-safe; the Receiver is move-only with a `poll_fd()` for poll/epoll integration.

Defines `Command` and `AppCoreEvent` variant types used for communication between the CLI and AppCore.

**Source files:** `src/mpsc.h`

**Dependencies:** POSIX (`eventfd`, `write`, `read`)

## 2. Component Specifications

```cpp
namespace a0::mpsc {

// ---- Command types ----
struct SubmitGoal { std::string goal; };
struct Cancel {};
struct Shutdown {};
using Command = std::variant<SubmitGoal, Cancel, Shutdown>;

// ---- AppCore event types ----
struct LlmToken   { std::string text; };
struct ToolStart  { std::string toolName; std::string arguments; };
struct ToolEnd    { std::string toolName; int exitCode = 0; std::string output; };
struct Complete   { std::string text; };
struct Error      { std::string message; };
using AppCoreEvent = std::variant<LlmToken, ToolStart, ToolEnd, Complete, Error>;

// ---- Channel components ----
template<typename T>
class Sender {
public:
    Sender() = default;
    Sender(const Sender&);            // clone (refcounted)
    Sender(Sender&&) noexcept;
    Sender& operator=(const Sender&);
    Sender& operator=(Sender&&) noexcept;
    ~Sender();

    void send(T value);
    Sender clone() const;
    bool valid() const;
};

template<typename T>
class Receiver {
public:
    Receiver() = default;
    Receiver(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept;
    Receiver& operator=(Receiver&&) noexcept;
    ~Receiver() = default;

    int poll_fd() const;              // poll()-compatible fd
    std::vector<T> drain();           // non-blocking drain
    bool connected() const;           // true while senders exist
};

template<typename T>
struct Channel {
    static std::pair<Sender<T>, Receiver<T>> create();
};

} // namespace a0::mpsc
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Producers
        S1[Sender&lt;T&gt; A]
        S2[Sender&lt;T&gt; B]
    end

    subgraph ChannelState
        MUTEX[std::mutex]
        QUEUE[std::deque&lt;T&gt;]
        EFD[event_fd]
        REFCNT[senders atomic]
    end

    subgraph Consumer
        R[Receiver&lt;T&gt;]
        POLL[poll / epoll]
    end

    S1 -->|lock / push / write eventfd| MUTEX
    S2 -->|lock / push / write eventfd| MUTEX
    MUTEX --> QUEUE
    EFD -->|POLLIN| POLL
    POLL -->|readable| R
    R -->|drain() lock / pop| QUEUE
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant S as Sender&lt;T&gt;
    participant CS as ChannelState
    participant R as Receiver&lt;T&gt;
    participant EL as Event Loop

    S->>CS: lock(mutex)
    S->>CS: queue.push_back(value)
    S->>CS: unlock(mutex)
    S->>CS: write(eventfd, 1)

    CS->>EL: eventfd readable

    EL->>R: poll() returns
    R->>CS: read(eventfd) drain
    R->>CS: lock(mutex)
    R->>CS: drain all dequeued items
    R->>CS: unlock(mutex)
    R-->>EL: vector&lt;T&gt; of events
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| Single sender, single receiver | Message delivered via drain() |
| Multiple senders (threaded) | All messages received, no data race |
| Sender cloning | Both clones write to same channel |
| Receiver move | Moved-to receiver drains correctly |
| poll_fd readability | eventfd readable after send |
| drain() empty | Returns empty vector |
| connected() after all senders die | Returns false |
| Shutdown via eventfd | stop() wakes poll loop |
| Receiver::drain() non-blocking | Returns immediately when empty |
