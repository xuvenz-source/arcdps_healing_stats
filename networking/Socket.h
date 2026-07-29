#pragma once
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <cstdint>
#include <string_view>

enum class SocketError
{
	None,
	//ConnectionReset, // ECONNRESET, WSAECONNRESET
	WouldBlock, // EAGAIN, EWOULDBLOCK, WSAEWOULDBLOCK
	Interrupted, // EINTR, WSAEINTR
	Unknown, // all other errors
};

#ifdef _WIN32
using SocketHandleT = SOCKET;
using socklen_t = int;
#else
using SocketHandleT = int;
#endif

class Socket
{
public:
	Socket() noexcept;
	~Socket() noexcept;

	Socket(Socket&& pOther) noexcept;
	Socket& operator=(Socket&& pOther) noexcept;

	Socket(const Socket&) = delete;
	Socket& operator=(const Socket&) = delete;

	SocketError socket(int domain, int type, int protocol);
	void close();

	SocketError bind(const sockaddr* addr, socklen_t addrlen);
	SocketError connect(const sockaddr* addr, socklen_t addrlen);

	SocketError listen(int backlog);
	SocketError accept(sockaddr* addr, socklen_t* addrlen, Socket* pOutSocket);

	SocketError send(const void* buf, uint32_t len, int flags, uint32_t* pOutSentBytes);

	SocketError recv(void* buf, uint32_t len, int flags, uint32_t* pOutRecvedBytes);

	SocketError getsockname(sockaddr* name, socklen_t* namelen);

	SocketError socket_and_bind(std::string_view pBindEndpoint);

	SocketHandleT GetUnderlyingHandle() const;

#ifndef TEST
private:
#endif
	// Parse e.g. "127.0.0.1:12345" to "127.0.0.1", 12345.
	// pPortRequired denotes whether missing port is an error. If not set, pOutPort will be left untouched in case the
	// input string is missing a port
	bool ParseEndpoint(std::string_view pEndpoint, bool pPortRequired, std::string_view* pOutAddress, uint16_t* pOutPort);

	SocketHandleT mHandle;
};

