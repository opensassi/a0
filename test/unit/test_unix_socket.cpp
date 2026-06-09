#include "ipc/unix_socket.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <string>

using namespace a0::ipc;

// ---------------------------------------------------------------------------
// Socket lifecycle
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, DefaultConstructedIsClosed) {
    UnixSocket s;
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.fd(), -1);
}

TEST(UnixSocketTest, MoveTransfersOwnership) {
    UnixSocket s1;
    UnixSocket s2(std::move(s1));
    EXPECT_FALSE(s1.isOpen());
}

TEST(UnixSocketTest, BindAndListenCreatesSocketFile) {
    UnixSocket server;
    std::string path = "/tmp/test_a0_bind.sock";
    UnixSocket::unlinkPath(path);
    int rc = server.bindAndListen(path);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(server.isOpen());
    UnixSocket::unlinkPath(path);
}

TEST(UnixSocketTest, BindAndListenUnlinksStaleSocket) {
    UnixSocket server;
    std::string path = "/tmp/test_a0_stale.sock";
    // Create stale socket file
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    ::bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    ::close(fd);

    // bindAndListen should unlink and succeed
    int rc = server.bindAndListen(path);
    EXPECT_EQ(rc, 0);
    UnixSocket::unlinkPath(path);
}

TEST(UnixSocketTest, AcceptReturnsMinusOneWhenNoClient) {
    UnixSocket server;
    std::string path = "/tmp/test_a0_noaccept.sock";
    UnixSocket::unlinkPath(path);
    ASSERT_EQ(server.bindAndListen(path), 0);

    UnixSocket client;
    int rc = server.accept(client);
    // Stub returns -1 (no pending connection)
    EXPECT_EQ(rc, -1);
    EXPECT_FALSE(client.isOpen());
    UnixSocket::unlinkPath(path);
}

// ---------------------------------------------------------------------------
// Connect
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, ConnectToNonExistentSocketReturnsMinusOne) {
    UnixSocket client;
    int rc = client.connect("/tmp/test_a0_nonexistent.sock", 100);
    EXPECT_EQ(rc, -1);
}

// ---------------------------------------------------------------------------
// Send / Recv
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, SendOnUnconnectedSocketReturnsMinusOne) {
    UnixSocket s;
    int rc = s.send("hello");
    EXPECT_EQ(rc, -1);
}

TEST(UnixSocketTest, RecvOnUnconnectedSocketReturnsMinusOne) {
    UnixSocket s;
    std::vector<char> buf(1024);
    size_t received = 999;
    int rc = s.recv(buf, received);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(received, 0u);
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, PollReadableOnUnconnectedReturnsMinusOne) {
    UnixSocket s;
    int rc = s.pollReadable(0);
    EXPECT_EQ(rc, -1);
}

TEST(UnixSocketTest, PollWritableOnUnconnectedReturnsMinusOne) {
    UnixSocket s;
    int rc = s.pollWritable(0);
    EXPECT_EQ(rc, -1);
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, CloseIsIdempotent) {
    UnixSocket s;
    s.close();
    s.close();
    EXPECT_FALSE(s.isOpen());
}

TEST(UnixSocketTest, UnlinkPathDoesNotCrash) {
    // Should not crash even if file doesn't exist
    UnixSocket::unlinkPath("/tmp/test_a0_noexist_unlink.sock");
}

// ---------------------------------------------------------------------------
// Move assignment
// ---------------------------------------------------------------------------

TEST(UnixSocketTest, MoveAssignmentClosesOld) {
    UnixSocket s1;
    UnixSocket s2;
    s2 = std::move(s1);
    EXPECT_FALSE(s1.isOpen());
}
