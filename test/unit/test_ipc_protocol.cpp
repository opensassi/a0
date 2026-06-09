#include "ipc/ipc_protocol.h"
#include <gtest/gtest.h>
#include <string>
#include <sys/socket.h>

using namespace a0::ipc;

// ---------------------------------------------------------------------------
// Test helper — send raw data from a raw fd
// ---------------------------------------------------------------------------
namespace {
void rawSend(int fd, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
        if (n <= 0) break;
        total += static_cast<size_t>(n);
    }
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST(IpcProtocolTest, SerializeRegisterMessageContainsNewline) {
    Message msg;
    msg.type = MessageType::REGISTER;
    msg.pid = 1234;
    msg.sessionUuid = "abc-def";

    std::string wire = serialize(msg);
    // Stub returns "{}\n" — test will fail when real impl produces correct JSON
    EXPECT_NE(wire.find("register"), std::string::npos);
    EXPECT_EQ(wire.back(), '\n');
}

TEST(IpcProtocolTest, SerializeAckMessage) {
    Message msg;
    msg.type = MessageType::ACK;
    msg.status = "registered";

    std::string wire = serialize(msg);
    EXPECT_NE(wire.find("registered"), std::string::npos);
    EXPECT_EQ(wire.back(), '\n');
}

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------

TEST(IpcProtocolTest, DeserializeValidRegisterMessage) {
    std::string jsonLine = R"({"type":"register","pid":5678,"session":"sess-1"})";

    Message msg;
    int rc = deserialize(jsonLine, msg);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(msg.type, "register");
    EXPECT_EQ(msg.pid, 5678);
    EXPECT_EQ(msg.sessionUuid, "sess-1");
}

TEST(IpcProtocolTest, DeserializeValidUpdateMessage) {
    std::string jsonLine = R"({"type":"update","pid":1,"agents":[{"pid":2,"state":"running"}]})";
    Message msg;
    int rc = deserialize(jsonLine, msg);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(msg.type, "update");
    EXPECT_TRUE(msg.agents.is_array());
    EXPECT_EQ(msg.agents.size(), 1);
}

TEST(IpcProtocolTest, DeserializeMissingTypeReturnsMinusTwo) {
    std::string jsonLine = R"({"pid":1})";
    Message msg;
    int rc = deserialize(jsonLine, msg);
    EXPECT_EQ(rc, -2);
}

TEST(IpcProtocolTest, DeserializeMalformedJsonReturnsMinusOne) {
    std::string jsonLine = "{bad json";
    Message msg;
    int rc = deserialize(jsonLine, msg);
    EXPECT_EQ(rc, -1);
}

TEST(IpcProtocolTest, RoundTripPreservesFields) {
    Message original;
    original.type = MessageType::HEARTBEAT;
    original.pid = 99;
    original.sessionUuid = "uuid-99";

    std::string wire = serialize(original);
    // Strip trailing newline
    if (!wire.empty() && wire.back() == '\n') wire.pop_back();

    Message parsed;
    int rc = deserialize(wire, parsed);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.pid, original.pid);
}

// ---------------------------------------------------------------------------
// recvMessage / sendMessage (require actual sockets — tested via socketpair)
// ---------------------------------------------------------------------------

TEST(IpcProtocolTest, SendMessageOnUnconnectedReturnsMinusOne) {
    UnixSocket s;
    Message msg;
    msg.type = MessageType::REGISTER;
    msg.pid = 1;

    int rc = sendMessage(s, msg);
    EXPECT_EQ(rc, -1);
}

TEST(IpcProtocolTest, RecvMessageOnUnconnectedReturnsMinusOne) {
    UnixSocket s;
    Message msg;
    int rc = recvMessage(s, msg, 100);
    EXPECT_EQ(rc, -1);
}

// ---------------------------------------------------------------------------
// BufferedSocket — caller pattern tests (using socketpair)
// ---------------------------------------------------------------------------

/// Create a connected socket pair and a BufferedSocket wrapping the receiver.
/// Aborts test on failure via ASSERT.
struct BufferedFixture {
    int sender = -1;
    BufferedSocket receiver;
    BufferedFixture() {
        int sv[2];
        int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        if (rc != 0) return;
        sender = sv[0];
        receiver = BufferedSocket(sv[1]);
    }
    bool valid() const { return sender >= 0 && receiver.fd() >= 0; }
    ~BufferedFixture() { if (sender >= 0) ::close(sender); }
};

TEST(IpcProtocolTest, BufferedC2B1RecvPattern_prompt_reply) {
    BufferedFixture f;
    ASSERT_TRUE(f.valid());
    std::string wire = "{\"type\":\"prompt_reply\",\"session\":\"s1\",\"toolCallId\":\"c1\"}\n";
    rawSend(f.sender, wire);

    Message msg;
    int r = f.receiver.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "prompt_reply");
    EXPECT_EQ(msg.sessionUuid, "s1");
    EXPECT_EQ(msg.toolCallId, "c1");
}

TEST(IpcProtocolTest, BufferedB1AgentRecvPattern_register) {
    BufferedFixture f;
    ASSERT_TRUE(f.valid());
    std::string wire = "{\"type\":\"register\",\"pid\":5678,\"session\":\"sess-a\"}\n";
    rawSend(f.sender, wire);

    Message msg;
    int r = f.receiver.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "register");
    EXPECT_EQ(msg.pid, 5678);
    EXPECT_EQ(msg.sessionUuid, "sess-a");
}

TEST(IpcProtocolTest, BufferedC2B1RecvPattern_update) {
    BufferedFixture f;
    ASSERT_TRUE(f.valid());
    std::string wire = "{\"type\":\"update\",\"pid\":1,\"agents\":[{\"pid\":2,\"state\":\"running\"}]}\n";
    rawSend(f.sender, wire);

    Message msg;
    int r = f.receiver.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "update");
    EXPECT_TRUE(msg.agents.is_array());
    EXPECT_EQ(msg.agents.size(), 1);
}

TEST(IpcProtocolTest, BufferedTerminalRecvPattern_stream_input) {
    BufferedFixture f;
    ASSERT_TRUE(f.valid());
    std::string wire = "{\"type\":\"stream_input\",\"streamId\":42,\"chunkData\":\"ls\\n\"}\n";
    rawSend(f.sender, wire);

    Message msg;
    int r = f.receiver.recv(msg, 5000);
    EXPECT_EQ(r, RECV_OK);
    EXPECT_EQ(msg.type, "stream_input");
    EXPECT_EQ(msg.streamId, 42);
    EXPECT_EQ(msg.chunkData, "ls\n");
}

TEST(BufferedSocketTest, SendOnUnconnectedReturnsErr) {
    BufferedSocket s;
    Message msg;
    msg.type = "test";
    int r = s.send(msg);
    EXPECT_EQ(r, -1);
}
