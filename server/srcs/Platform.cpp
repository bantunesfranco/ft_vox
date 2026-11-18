// Define before ANY include: some standard headers pull in <windows.h>.
#if defined(_WIN32)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00      // WSAPoll needs Vista or newer
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

#include "Platform.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#  include <winsock2.h>      // must come before <windows.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")   // MinGW / CMake link ws2_32 explicitly
#  endif
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/uio.h>
#  include <unistd.h>
#endif

namespace net {
namespace platform {

// ----------------------------------------------------------------------------
//  The OS's own socket type, and conversions from/to our SocketHandle
// ----------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
static_assert(sizeof(SocketHandle) == sizeof(SOCKET), "SocketHandle must be as wide as SOCKET");
static_assert(kInvalidSocket == static_cast<SocketHandle>(INVALID_SOCKET),
              "kInvalidSocket must equal INVALID_SOCKET");
#else
using NativeSocket = int;
#endif

inline NativeSocket toNative(SocketHandle s)   { return static_cast<NativeSocket>(s); }
inline SocketHandle fromNative(NativeSocket s) { return static_cast<SocketHandle>(s); }

} // namespace

// ============================================================================
//  NetworkRuntime
// ============================================================================

#if defined(_WIN32)

NetworkRuntime::NetworkRuntime() {
	WSADATA data;
	if (const int rc = WSAStartup(MAKEWORD(2, 2), &data); rc != 0) {   // reference counted by the OS
		_ok    = false;
		_error = "WSAStartup failed: " + errorString(rc);
	}
}

NetworkRuntime::~NetworkRuntime() {
	if (_ok) WSACleanup();
}

#else

NetworkRuntime::NetworkRuntime()  = default;
NetworkRuntime::~NetworkRuntime() = default;

#endif

// ============================================================================
//  Errors
// ============================================================================

#if defined(_WIN32)

int  lastError()                    { return WSAGetLastError(); }
bool isWouldBlock(const int err)    { return err == WSAEWOULDBLOCK; }
bool isInterrupted(const int err)   { return err == WSAEINTR; }
bool isOutOfDescriptors(const int err) { return err == WSAEMFILE; }

std::string errorString(const int err) {
	char* msg = nullptr;
	const DWORD n = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, static_cast<DWORD>(err), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<char*>(&msg), 0, nullptr);
	std::string out = (n && msg) ? std::string(msg, n) : "error";
	if (msg) LocalFree(msg);
	while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
		out.pop_back();
	return out + " (" + std::to_string(err) + ")";
}

#else

int  lastError()                    { return errno; }
bool isWouldBlock(const int err)    { return err == EAGAIN || err == EWOULDBLOCK; }
bool isInterrupted(const int err)   { return err == EINTR; }
bool isOutOfDescriptors(const int err) { return err == EMFILE || err == ENFILE; }

std::string errorString(const int err) {
	return std::string(std::strerror(err)) + " (" + std::to_string(err) + ")";
}

#endif

// ============================================================================
//  Socket setup
// ============================================================================

#if defined(_WIN32)

bool setNonBlocking(const SocketHandle s) {
	u_long mode = 1;
	return ioctlsocket(toNative(s), FIONBIO, &mode) == 0;
}

bool setNoInherit(const SocketHandle s) {
	return SetHandleInformation(reinterpret_cast<HANDLE>(toNative(s)), HANDLE_FLAG_INHERIT, 0) != 0;
}

#else

bool setNonBlocking(const SocketHandle s) {
	const int flags = ::fcntl(toNative(s), F_GETFL, 0);
	return flags != -1 && ::fcntl(toNative(s), F_SETFL, flags | O_NONBLOCK) != -1;
}

bool setNoInherit(const SocketHandle s) {
	const int flags = ::fcntl(toNative(s), F_GETFD, 0);
	return flags != -1 && ::fcntl(toNative(s), F_SETFD, flags | FD_CLOEXEC) != -1;
}

#endif

bool setNoDelay(const SocketHandle s) {
	constexpr int one = 1;
	// The option value is `const char*` on Windows and `const void*` on POSIX;
	// a char* cast satisfies both.
	return ::setsockopt(toNative(s), IPPROTO_TCP, TCP_NODELAY,
	                    reinterpret_cast<const char*>(&one), sizeof(one)) == 0;
}

bool configureAccepted(const SocketHandle s) {
	if (!setNonBlocking(s) || !setNoInherit(s) || !setNoDelay(s)) return false;
#if defined(SO_NOSIGPIPE)                      // macOS / BSD have no MSG_NOSIGNAL
	const int one = 1;
	::setsockopt(toNative(s), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	return true;
}

void closeSocket(const SocketHandle s) noexcept {
#if defined(_WIN32)
	::closesocket(toNative(s));
#else
	::close(toNative(s));
#endif
}

// ============================================================================
//  I/O
// ============================================================================

std::ptrdiff_t recvSome(const SocketHandle s, void* buf, const std::size_t len) {
#if defined(_WIN32)
	const int want = static_cast<int>(std::min<std::size_t>(len, INT_MAX));   // recv takes an int
	return ::recv(toNative(s), static_cast<char*>(buf), want, 0);
#else
	return ::recv(toNative(s), buf, len, 0);
#endif
}

std::ptrdiff_t sendv(const SocketHandle s, const IoSlice* slices, int count) {
	count = std::min(count, kMaxIoSlices);

#if defined(_WIN32)
	WSABUF bufs[kMaxIoSlices];
	for (int i = 0; i < count; ++i) {
		bufs[i].buf = const_cast<CHAR*>(static_cast<const CHAR*>(slices[i].data));
		bufs[i].len = static_cast<ULONG>(std::min<std::size_t>(slices[i].size, 1u << 30));
	}
	DWORD sent = 0;
	if (::WSASend(toNative(s), bufs, static_cast<DWORD>(count), &sent, 0, nullptr, nullptr)
	    == SOCKET_ERROR)
		return -1;
	return static_cast<std::ptrdiff_t>(sent);
#else
	iovec iov[kMaxIoSlices];
	for (int i = 0; i < count; ++i) {
		iov[i].iov_base = const_cast<void*>(slices[i].data);
		iov[i].iov_len  = slices[i].size;
	}
	msghdr msg{};
	msg.msg_iov    = iov;
	msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(count);

#if defined(MSG_NOSIGNAL)
	constexpr int flags = MSG_NOSIGNAL;        // a closed peer gives EPIPE, not SIGPIPE
#else
	constexpr int flags = 0;                   // SO_NOSIGPIPE is set in configureAccepted()
#endif
	return ::sendmsg(toNative(s), &msg, flags);
#endif
}

// ============================================================================
//  Listening / accepting
// ============================================================================

SocketHandle listenTcp(const std::uint16_t port, const int backlog,
                       std::uint16_t& boundPort, std::string& error) {
	const SocketHandle s = fromNative(::socket(AF_INET, SOCK_STREAM, 0));
	if (s == kInvalidSocket) {
		error = "socket: " + errorString(lastError());
		return kInvalidSocket;
	}

	// Read the error text BEFORE closing: closing can overwrite it.
	auto fail = [&](const char* what) {
		error = std::string(what) + ": " + errorString(lastError());
		closeSocket(s);
		return kInvalidSocket;
	};

	if (!setNonBlocking(s)) return fail("set non-blocking");
	setNoInherit(s);                                   // best effort

	constexpr int one = 1;
#if defined(_WIN32)
	// On Windows SO_REUSEADDR lets another process bind the same port.
	if (::setsockopt(toNative(s), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
	                 reinterpret_cast<const char*>(&one), sizeof(one)) != 0)
		return fail("SO_EXCLUSIVEADDRUSE");
#else
	if (::setsockopt(toNative(s), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
		return fail("SO_REUSEADDR");
#endif

	sockaddr_in addr{};
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(port);
	if (::bind(toNative(s), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
		return fail("bind");
	if (::listen(toNative(s), backlog) != 0)
		return fail("listen");

	boundPort = port;
	sockaddr_in bound{};
	socklen_t len = sizeof(bound);
	if (::getsockname(toNative(s), reinterpret_cast<sockaddr*>(&bound), &len) == 0)
		boundPort = ntohs(bound.sin_port);
	return s;
}

namespace {

// Errors that mean "this one connection died before we accepted it", not
// "the listener is broken". Just call accept() again.
bool isTransientAcceptError(const int err) {
#if defined(_WIN32)
	return err == WSAECONNRESET || err == WSAECONNABORTED || err == WSAEINTR;
#else
	switch (err) {
		case EINTR: case ECONNABORTED: case EPROTO:
		case ENETDOWN: case ENOPROTOOPT: case EHOSTDOWN:
		case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
#ifdef ENONET
		case ENONET:
#endif
			return true;
		default:
			return false;
	}
#endif
}

} // namespace

AcceptStatus acceptTcp(const SocketHandle listener, SocketHandle& out,
                       std::string& peerAddress, std::uint16_t& peerPort) {
	sockaddr_in addr{};
	socklen_t len = sizeof(addr);
	const SocketHandle s = fromNative(
		::accept(toNative(listener), reinterpret_cast<sockaddr*>(&addr), &len));

	if (s == kInvalidSocket) {
		const int err = lastError();
		if (isWouldBlock(err))            return AcceptStatus::WouldBlock;
		if (isOutOfDescriptors(err))      return AcceptStatus::OutOfDescriptors;
		if (isTransientAcceptError(err))  return AcceptStatus::Retry;
		return AcceptStatus::Error;
	}

	if (!configureAccepted(s)) {
		closeSocket(s);
		return AcceptStatus::Retry;
	}

	char text[INET_ADDRSTRLEN] = {};
	::inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text));
	peerAddress = text;
	peerPort    = ntohs(addr.sin_port);
	out         = s;
	return AcceptStatus::Accepted;
}

// ============================================================================
//  Connecting
// ============================================================================

SocketHandle connectTcp(const std::string& host, const std::uint16_t port,
                        bool& inProgress, std::string& error) {
	inProgress = false;

	addrinfo hints{};
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	addrinfo* found = nullptr;
	if (const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &found); rc != 0 || !found) {
#if defined(_WIN32)
		error = "cannot resolve '" + host + "': " + errorString(rc);
#else
		error = "cannot resolve '" + host + "': " + ::gai_strerror(rc);
#endif
		if (found) ::freeaddrinfo(found);
		return kInvalidSocket;
	}
	sockaddr_in addr{};
	std::memcpy(&addr, found->ai_addr, std::min<std::size_t>(sizeof(addr), found->ai_addrlen));
	::freeaddrinfo(found);
	addr.sin_port = htons(port);

	const SocketHandle s = fromNative(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
	if (s == kInvalidSocket) {
		error = "socket: " + errorString(lastError());
		return kInvalidSocket;
	}
	if (!configureAccepted(s)) {                       // same options as an accepted socket
		error = "socket setup: " + errorString(lastError());
		closeSocket(s);
		return kInvalidSocket;
	}

	if (::connect(toNative(s), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0)
		return s;                                      // connected immediately

	const int err = lastError();
#if defined(_WIN32)
	const bool pending = err == WSAEWOULDBLOCK;
#else
	const bool pending = err == EINPROGRESS || err == EINTR;
#endif
	if (!pending) {
		error = "connect: " + errorString(err);
		closeSocket(s);
		return kInvalidSocket;
	}
	inProgress = true;
	return s;
}

int socketError(const SocketHandle s) {
	int err = 0;
	socklen_t len = sizeof(err);
	if (::getsockopt(toNative(s), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0)
		return lastError();
	return err;
}

} // namespace platform

// ============================================================================
//  Socket
// ============================================================================

void Socket::reset(const SocketHandle s) noexcept {
	if (_s != platform::kInvalidSocket) platform::closeSocket(_s);
	_s = s;
}

// ============================================================================
//  pollSockets : poll() on Linux, WSAPoll() on Windows
// ============================================================================

namespace {

#if defined(_WIN32)
using PollFd = WSAPOLLFD;
// WSAPoll accepts POLLRDNORM / POLLWRNORM as requested events.
constexpr short kWantIn  = POLLRDNORM;
constexpr short kWantOut = POLLWRNORM;
inline int doPoll(PollFd* fds, const std::size_t n, const int timeoutMs) {
	return ::WSAPoll(fds, static_cast<ULONG>(n), timeoutMs);
}
#else
using PollFd = pollfd;
constexpr short kWantIn  = POLLIN;
constexpr short kWantOut = POLLOUT;
inline int doPoll(PollFd* fds, const std::size_t n, const int timeoutMs) {
	return ::poll(fds, static_cast<nfds_t>(n), timeoutMs);
}
#endif

} // namespace

int pollSockets(const std::vector<PollItem>& items,
                std::vector<PollResult>& results,
                const int timeoutMs) {
	results.clear();

	if (items.empty()) {                       // WSAPoll fails on an empty set
		if (timeoutMs > 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
		return 0;
	}

	static thread_local std::vector<PollFd> fds;   // reused: no allocation per call
	fds.resize(items.size());
	for (std::size_t i = 0; i < items.size(); ++i) {
		fds[i].fd      = static_cast<decltype(PollFd::fd)>(items[i].socket);
		fds[i].events  = static_cast<short>((items[i].wantRead  ? kWantIn  : 0) |
		                                    (items[i].wantWrite ? kWantOut : 0));
		fds[i].revents = 0;
	}

	const int rc = doPoll(fds.data(), fds.size(), timeoutMs);
	if (rc < 0)                                // read the error before anything else runs
		return platform::isInterrupted(platform::lastError()) ? 0 : -1;
	if (rc == 0)
		return 0;

	for (std::size_t i = 0; i < fds.size(); ++i) {
		const short r = fds[i].revents;
		if (!r) continue;
		results.push_back({
			items[i].tag,
			(r & POLLIN)  != 0,
			(r & POLLOUT) != 0,
			(r & (POLLERR | POLLHUP | POLLNVAL)) != 0,
		});
	}
	return static_cast<int>(results.size());
}

} // namespace net