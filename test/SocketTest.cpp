#include "../networking/Socket.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#else
#pragma warning(push, 0)
#pragma warning(disable : 4005)
#pragma warning(disable : 4389)
#pragma warning(disable : 26439)
#pragma warning(disable : 26495)
#endif
#include <gtest/gtest.h>
#ifdef __clang__
#pragma clang diagnostic pop
#else
#pragma warning(pop)
#endif

#ifdef _WIN32
#include <ws2ipdef.h>
#include <mstcpip.h>
#else
#include <arpa/inet.h>
#endif

#ifndef _WIN32
void IN4ADDR_SETLOOPBACK(sockaddr_in* pAddr)
{
	pAddr->sin_family = AF_INET;
	pAddr->sin_port = 0;
	pAddr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	memset(pAddr->sin_zero, 0x00, sizeof(pAddr->sin_zero));
}

void IN6ADDR_SETLOOPBACK(sockaddr_in6* pAddr)
{
	pAddr->sin6_family = AF_INET6;
	pAddr->sin6_port = 0;
	pAddr->sin6_flowinfo = 0;
	pAddr->sin6_addr = in6addr_loopback;
}
#endif

TEST(SocketTest, DestructWithoutOpen)
{
	Socket s;
}

TEST(SocketTest, CreateIpv4)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);
}

TEST(SocketTest, CreateIpv6)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);
}

TEST(SocketTest, DoubleClose)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);
	s.close();
	s.close();
}

TEST(SocketTest, BindIpv4)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in addr{};
	IN4ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);
}

TEST(SocketTest, BindIpv6)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in6 addr{};
	IN6ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);
}

TEST(SocketTest, BindIpv4Mismatch)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in6 addr{};
	IN6ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::Unknown);
}

TEST(SocketTest, BindIpv6Mismatch)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in addr{};
	IN4ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::Unknown);
}

TEST(SocketTest, ConnectIpv4Mismatch)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in6 addr{};
	IN6ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.connect(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::Unknown);
}

TEST(SocketTest, ConnectIpv6Mismatch)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in addr{};
	IN4ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.connect(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::Unknown);
}

TEST(SocketTest, ListenIpv4)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in addr{};
	IN4ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);

	ASSERT_EQ(s.listen(128), SocketError::None);
}

TEST(SocketTest, ListenIpv6)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in6 addr{};
	IN6ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(s.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);

	ASSERT_EQ(s.listen(128), SocketError::None);
}

static constexpr SocketError EXPECTED_RESULT_LISTEN_BEFORE_BIND =
#ifdef _WIN32
	SocketError::Unknown;
#else
	SocketError::None;
#endif

TEST(SocketTest, ListenBeforeBindIpv4)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	ASSERT_EQ(s.listen(128), EXPECTED_RESULT_LISTEN_BEFORE_BIND);
}

TEST(SocketTest, ListenBeforeBindIpv6)
{
	Socket s;
	ASSERT_EQ(s.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	ASSERT_EQ(s.listen(128), EXPECTED_RESULT_LISTEN_BEFORE_BIND);
}

TEST(SocketTest, FullFlowOneClientIpv4)
{
	Socket listener;
	ASSERT_EQ(listener.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in addr{};
	IN4ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(listener.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);

	ASSERT_EQ(listener.listen(128), SocketError::None);

	sockaddr_storage actualListenAddr{};
	socklen_t actualListenAddrLen = sizeof(actualListenAddr);
	ASSERT_EQ(listener.getsockname(reinterpret_cast<sockaddr*>(&actualListenAddr), &actualListenAddrLen), SocketError::None);

	Socket client;
	ASSERT_EQ(client.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	ASSERT_EQ(client.connect(reinterpret_cast<const sockaddr*>(&actualListenAddr), actualListenAddrLen), SocketError::None);

	Socket server;
	ASSERT_EQ(listener.accept(nullptr, nullptr, &server), SocketError::None);

	static constexpr const char SEND_DATA[] = "THIS_IS_DATA";
	uint32_t sentBytes;
	ASSERT_EQ(client.send(SEND_DATA, sizeof(SEND_DATA), 0, &sentBytes), SocketError::None);
	ASSERT_EQ(sentBytes, sizeof(SEND_DATA));

	char receiveData[16];
	uint32_t recvedBytes;
	ASSERT_EQ(server.recv(&receiveData, sizeof(receiveData), 0, &recvedBytes), SocketError::None);
	ASSERT_EQ(recvedBytes, sizeof(SEND_DATA));
	ASSERT_EQ(memcmp(receiveData, SEND_DATA, recvedBytes), 0);
}

TEST(SocketTest, FullFlowOneClientIpv6)
{
	Socket listener;
	ASSERT_EQ(listener.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	sockaddr_in6 addr{};
	IN6ADDR_SETLOOPBACK(&addr);
	ASSERT_EQ(listener.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), SocketError::None);

	ASSERT_EQ(listener.listen(128), SocketError::None);

	sockaddr_storage actualListenAddr{};
	socklen_t actualListenAddrLen = sizeof(actualListenAddr);
	ASSERT_EQ(listener.getsockname(reinterpret_cast<sockaddr*>(&actualListenAddr), &actualListenAddrLen), SocketError::None);

	Socket client;
	ASSERT_EQ(client.socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP), SocketError::None);

	ASSERT_EQ(client.connect(reinterpret_cast<const sockaddr*>(&actualListenAddr), actualListenAddrLen), SocketError::None);

	Socket server;
	ASSERT_EQ(listener.accept(nullptr, nullptr, &server), SocketError::None);

	static constexpr const char SEND_DATA[] = "THIS_IS_DATA";
	uint32_t sentBytes;
	ASSERT_EQ(client.send(SEND_DATA, sizeof(SEND_DATA), 0, &sentBytes), SocketError::None);
	ASSERT_EQ(sentBytes, sizeof(SEND_DATA));

	char receiveData[16];
	uint32_t recvedBytes;
	ASSERT_EQ(server.recv(&receiveData, sizeof(receiveData), 0, &recvedBytes), SocketError::None);
	ASSERT_EQ(recvedBytes, sizeof(SEND_DATA));
	ASSERT_EQ(memcmp(receiveData, SEND_DATA, recvedBytes), 0);
}

TEST(SocketTest, SocketAndBindLoopback)
{
	Socket s;
	ASSERT_EQ(s.socket_and_bind("127.0.0.1:0"), SocketError::None);
}

TEST(SocketTest, SocketAndBindAny)
{
	Socket s;
	ASSERT_EQ(s.socket_and_bind("0.0.0.0:0"), SocketError::None);
}

TEST(SocketTest, SocketAndBindLoopbackMissingPort)
{
	Socket s;
	ASSERT_EQ(s.socket_and_bind("127.0.0.1"), SocketError::Unknown);
}

TEST(SocketTest, SocketAndBindAnyMissingPort)
{
	Socket s;
	ASSERT_EQ(s.socket_and_bind("0.0.0.0"), SocketError::Unknown);
}

TEST(SocketTest, SocketAndBindBadAddress)
{
	Socket s;
	// hopefully someone doesn't run the test on the machine with this IP :)
	ASSERT_EQ(s.socket_and_bind("123.124.125.126:0"), SocketError::Unknown);
}
