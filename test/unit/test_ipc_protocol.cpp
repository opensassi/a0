#include "ipc_protocol.h"
#include <gtest/gtest.h>
#include <string>

using namespace a0::ipc;

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
