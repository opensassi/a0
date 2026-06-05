#include "ipc_protocol.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <cstring>
#include <thread>
#include <chrono>

using namespace a0::ipc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a connected socket pair. Returns {sender_fd, receiver_fd}.
static std::pair<int, int> createPair() {
    int sv[2];
    int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    EXPECT_EQ(rc, 0);
    return {sv[0], sv[1]};
}

/// Send a raw string on a fd.
static void rawSend(int fd, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
        ASSERT_GT(n, 0);
        total += static_cast<size_t>(n);
    }
}

/// Build a simple register message string.
static std::string registerMsg() {
    Message m;
    m.type = "register";
    m.pid = 42;
    m.sessionUuid = "test-session";
    return serialize(m);
}

// ---------------------------------------------------------------------------
// Construction & Lifecycle
// ---------------------------------------------------------------------------

TEST(BufferedSocketTest, DefaultConstructed) {
    BufferedSocket s;
    EXPECT_EQ(s.fd(), -1);
}

TEST(BufferedSocketTest, MoveTransfersOwnership) {
    auto [snd, rcv] = createPair();
    BufferedSocket a(rcv);
    BufferedSocket b(std::move(a));
    EXPECT_EQ(a.fd(), -1);
    EXPECT_EQ(b.fd(), rcv);
    ::close(snd);
}

TEST(BufferedSocketTest, CloseIsIdempotent) {
    auto [snd, rcv] = createPair();
    BufferedSocket s(rcv);
    s.close();
    s.close();
    EXPECT_EQ(s.fd(), -1);
    ::close(snd);
}

TEST(BufferedSocketTest, ReleaseDetachesWithoutClosing) {
    auto [snd, rcv] = createPair();
    BufferedSocket s(rcv);
    int fd = s.release();
    EXPECT_EQ(fd, rcv);
    EXPECT_EQ(s.fd(), -1);
    // fd is still open — we can send through it
    rawSend(snd, "test\n");
    char buf[16];
    EXPECT_GT(::read(fd, buf, sizeof(buf)), 0);
    ::close(fd);
    ::close(snd);
}

// ---------------------------------------------------------------------------
// Single Message
// ---------------------------------------------------------------------------

TEST(BufferedSocketTest, CompleteMessageInOneRecv) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    std::string wire = registerMsg();
    rawSend(snd, wire);

    Message msg;
    int r = bs.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "register");
    EXPECT_EQ(msg.pid, 42);
    EXPECT_EQ(msg.sessionUuid, "test-session");

    ::close(snd);
}

TEST(BufferedSocketTest, RecvReturnsAgainWhenNoData) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    Message msg;
    int r = bs.recv(msg, 10);  // 10ms timeout
    EXPECT_EQ(r, RECV_AGAIN);

    ::close(snd);
}

TEST(BufferedSocketTest, RecvReturnsErrOnClosedConnection) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);
    ::close(snd);

    Message msg;
    int r = bs.recv(msg, 100);
    EXPECT_EQ(r, RECV_ERR);
}

TEST(BufferedSocketTest, SendAndRecvRoundTrip) {
    auto [snd, rcv] = createPair();

    // Send from BufferedSocket to raw fd
    BufferedSocket bs(rcv);
    Message out;
    out.type = "heartbeat";
    out.pid = 99;
    int sr = bs.send(out);
    EXPECT_EQ(sr, 0);

    // Receive on the raw sender fd
    std::vector<char> buf(4096);
    ssize_t n = ::recv(snd, buf.data(), buf.size(), 0);
    ASSERT_GT(n, 0);
    std::string wire(buf.data(), static_cast<size_t>(n));

    Message in;
    EXPECT_EQ(deserialize(wire, in), 0);
    EXPECT_EQ(in.type, "heartbeat");
    EXPECT_EQ(in.pid, 99);

    ::close(snd);
}

TEST(BufferedSocketTest, InvalidJsonReturnsErr) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    rawSend(snd, "{bad json\n");

    Message msg;
    int r = bs.recv(msg, 5000);
    EXPECT_EQ(r, RECV_ERR);

    ::close(snd);
}

// ---------------------------------------------------------------------------
// Partial & Accumulated Messages
// ---------------------------------------------------------------------------

TEST(BufferedSocketTest, MessageLargerThanReadChunk) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    // Build a message payload larger than READ_CHUNK (100)
    // A message with a big sessionUuid field
    Message m;
    m.type = "register";
    m.pid = 1;
    m.sessionUuid = std::string(200, 'x');  // 200 chars
    std::string wire = serialize(m);
    ASSERT_GT(wire.size(), static_cast<size_t>(100));

    rawSend(snd, wire);

    Message msg;
    int r;
    do {
        r = bs.recv(msg, 5000);
    } while (r == RECV_AGAIN);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "register");
    EXPECT_EQ(msg.pid, 1);
    EXPECT_EQ(msg.sessionUuid, std::string(200, 'x'));

    ::close(snd);
}

TEST(BufferedSocketTest, FragmentWithoutNewlineReturnsAgain) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    rawSend(snd, R"({"type":"register","pid":1)");

    Message msg;
    int r = bs.recv(msg, 5000);
    // Partial fragment, no newline
    EXPECT_EQ(r, RECV_AGAIN);

    ::close(snd);
}

TEST(BufferedSocketTest, FragmentCompletedBySecondWrite) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    // First write: partial
    rawSend(snd, R"({"type":"register","pid":1)");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Message msg;
    int r = bs.recv(msg, 100);
    EXPECT_EQ(r, RECV_AGAIN);  // no newline yet

    // Second write: completion
    rawSend(snd, R"(,"session":"sess"})" "\n");

    r = bs.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "register");
    EXPECT_EQ(msg.pid, 1);
    EXPECT_EQ(msg.sessionUuid, "sess");

    ::close(snd);
}

// ---------------------------------------------------------------------------
// Multiple Messages
// ---------------------------------------------------------------------------

TEST(BufferedSocketTest, BackToBackMessagesInOneRead) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    // Two small messages in one write
    std::string msg1 = R"({"type":"register","pid":1})" "\n";
    std::string msg2 = R"({"type":"heartbeat","pid":2})" "\n";
    rawSend(snd, msg1 + msg2);

    Message m1;
    int r1 = bs.recv(m1, 5000);
    EXPECT_EQ(r1, RECV_OK);
    EXPECT_EQ(m1.type, "register");
    EXPECT_EQ(m1.pid, 1);

    // Second message should be in buffer — no poll needed
    Message m2;
    int r2 = bs.recv(m2, 1);  // 1ms timeout, should return immediately from buffer
    EXPECT_EQ(r2, RECV_OK);
    EXPECT_EQ(m2.type, "heartbeat");
    EXPECT_EQ(m2.pid, 2);

    ::close(snd);
}

TEST(BufferedSocketTest, ThreeMessagesAccumulated) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    std::string all;
    for (int i = 0; i < 3; ++i) {
        all += R"({"type":"register","pid":)" + std::to_string(i + 1) + "}\n";
    }
    rawSend(snd, all);

    for (int i = 0; i < 3; ++i) {
        Message m;
        int r = bs.recv(m, 5000);
        EXPECT_EQ(r, RECV_OK);
        EXPECT_EQ(m.pid, i + 1);
    }

    ::close(snd);
}

// ---------------------------------------------------------------------------
// Buffer Overflow
// ---------------------------------------------------------------------------

TEST(BufferedSocketTest, BufferOverflowReturnsErr) {
    auto [snd, rcv] = createPair();
    BufferedSocket bs(rcv);

    // Send enough data to exceed MAX_BUFFER (no \n in any of it)
    std::string big(70000, 'y');  // 70 KB without newline
    rawSend(snd, big);

    // Keep recv'ing until we get RECV_ERR from overflow
    int r;
    do {
        Message msg;
        r = bs.recv(msg, 100);
    } while (r == RECV_AGAIN);

    EXPECT_EQ(r, RECV_ERR) << "Buffer should overflow and return RECV_ERR";

    ::close(snd);
}
