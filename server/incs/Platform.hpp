#pragma once
#ifndef NET_PLATFORM_HPP
# define NET_PLATFORM_HPP

// ============================================================================
//  Platform.hpp : the ONLY place where Linux and Windows differ
//
//  This header includes NO OS headers (no <winsock2.h>, no <sys/socket.h>).
//  Including a system header here would drag its macros (min, max, ERROR, ...)
//  into every file that includes Net.hpp, and including one inside a namespace
//  breaks the standard library. All OS code lives in Platform.cpp.
//
//    Linux / POSIX : socket handle = int,    errors = errno,            poll()
//    Windows       : socket handle = SOCKET, errors = WSAGetLastError,  WSAPoll()
//
//  There are no timers or wake-up handles on purpose: WSAPoll() can only wait
//  on sockets, so ticks come from the poll timeout (see Transport.cpp).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net {
namespace platform {

// ---- socket handle type -------------------------------------------------------

#if defined(_WIN32)
using SocketHandle = std::uintptr_t;                               // same width as SOCKET
inline constexpr SocketHandle kInvalidSocket = ~SocketHandle{0};   // == INVALID_SOCKET
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

// A contiguous piece of memory to send (scatter/gather: sendmsg / WSASend).
struct IoSlice {
	const void* data;
	std::size_t size;
};

constexpr int kMaxIoSlices = 16;

// ---- networking runtime -----------------------------------------------------------
//
// Windows needs WSAStartup() before any socket call and WSACleanup() after the
// last socket is closed. On Linux this does nothing.
//
// Declare it BEFORE any member that owns sockets so it is destroyed AFTER them.

class NetworkRuntime {
public:
	NetworkRuntime();
	~NetworkRuntime();
	NetworkRuntime(const NetworkRuntime&)            = delete;
	NetworkRuntime& operator=(const NetworkRuntime&) = delete;
	
	[[nodiscard]] bool               ok()    const { return _ok; }
	[[nodiscard]] const std::string& error() const { return _error; }

private:
	bool        _ok = true;
	std::string _error;
};

// ---- errors -------------------------------------------------------------------------
//
// Call lastError() IMMEDIATELY after the failing call, before anything else
// that could overwrite it. Then classify it with the helpers below.
// Never compare against errno / EAGAIN directly in portable code: on Windows
// socket errors are NOT in errno.

int         lastError();                       // errno  /  WSAGetLastError()
bool        isWouldBlock(int err);             // EAGAIN, EWOULDBLOCK  /  WSAEWOULDBLOCK
bool        isInterrupted(int err);            // EINTR  /  WSAEINTR
bool        isOutOfDescriptors(int err);       // EMFILE, ENFILE  /  WSAEMFILE
std::string errorString(int err);              // human readable text

// ---- socket setup ----------------------------------------------------------------------

bool setNonBlocking(SocketHandle s);           // fcntl(O_NONBLOCK)  /  ioctlsocket(FIONBIO)
bool setNoInherit(SocketHandle s);             // FD_CLOEXEC         /  HANDLE_FLAG_INHERIT off
bool setNoDelay(SocketHandle s);               // TCP_NODELAY (both)

// Non-blocking + no-inherit + TCP_NODELAY (+ SO_NOSIGPIPE where it exists).
// Applied to every accepted socket.
bool configureAccepted(SocketHandle s);

void closeSocket(SocketHandle s) noexcept;     // close()  /  closesocket()

// ---- I/O ------------------------------------------------------------------------------------

// Returns bytes received (> 0), 0 if the peer closed, or < 0 on error
// (then use lastError() / isWouldBlock()).
std::ptrdiff_t recvSome(SocketHandle s, void* buf, std::size_t len);

// Gathered send of up to kMaxIoSlices slices. Returns bytes sent (>= 0,
// possibly fewer than offered) or < 0 on error. Never raises SIGPIPE.
std::ptrdiff_t sendv(SocketHandle s, const IoSlice* slices, int count);

// ---- listening / accepting -----------------------------------------------------------------

// Creates a non-blocking TCP listener on 0.0.0.0:port (port 0 = pick one).
// Linux: SO_REUSEADDR (restart without waiting for TIME_WAIT).
// Windows: SO_EXCLUSIVEADDRUSE (SO_REUSEADDR there would let another process
// steal the port). Returns kInvalidSocket and fills `error` on failure.
SocketHandle listenTcp(std::uint16_t port, int backlog, std::uint16_t& boundPort, std::string& error);

enum class AcceptStatus {
	Accepted,           // `out` holds a configured, non-blocking socket
	WouldBlock,         // nothing pending: stop the accept loop
	Retry,              // transient error (peer reset before accept): just call again
	OutOfDescriptors,   // EMFILE / ENFILE: back off, do not spin
	Error,
};

AcceptStatus acceptTcp(SocketHandle listener, SocketHandle& out,
                       std::string& peerAddress, std::uint16_t& peerPort);

// ---- connecting (client side) ---------------------------------------------------------------

// Resolves `host` (IPv4 name or dotted address; this part may block briefly
// on a DNS lookup) and starts a NON-BLOCKING connect. The socket is already
// non-blocking, no-inherit and TCP_NODELAY.
//   inProgress = false : connected already (loopback can do that)
//   inProgress = true  : wait until poll says writable (or error), then check
//                        socketError(): 0 means connected
// Returns kInvalidSocket and fills `error` on failure.
SocketHandle connectTcp(const std::string& host, std::uint16_t port,
                        bool& inProgress, std::string& error);

// The pending error of a socket (SO_ERROR), 0 if none. Reading it clears it.
int socketError(SocketHandle s);

} // namespace platform

using platform::SocketHandle;
using platform::AcceptStatus;      // so `AcceptStatus::Accepted` works unqualified inside namespace net

// ============================================================================
//  Socket : move-only RAII owner of one socket (one owner per socket, always)
// ============================================================================

class Socket {
public:
	Socket() noexcept = default;
	explicit Socket(SocketHandle s) noexcept : _s(s) {}
	Socket(Socket&& o) noexcept : _s(o.release()) {}
	Socket& operator=(Socket&& o) noexcept { if (this != &o) reset(o.release()); return *this; }
	Socket(const Socket&)            = delete;
	Socket& operator=(const Socket&) = delete;
	~Socket() { reset(); }

	[[nodiscard]] SocketHandle get()   const noexcept { return _s; }
	[[nodiscard]] bool         valid() const noexcept { return _s != platform::kInvalidSocket; }

	SocketHandle release() noexcept     { SocketHandle s = _s; _s = platform::kInvalidSocket; return s; }
	void         reset(SocketHandle s = platform::kInvalidSocket) noexcept;   // closes the old one

private:
	SocketHandle _s = platform::kInvalidSocket;
};

// ============================================================================
//  Socket readiness polling  (level-triggered; poll() / WSAPoll())
// ============================================================================

struct PollItem {
	SocketHandle  socket    = platform::kInvalidSocket;
	std::uint64_t tag       = 0;       // returned untouched in PollResult
	bool          wantRead  = true;
	bool          wantWrite = false;
};

struct PollResult {
	std::uint64_t tag      = 0;
	bool          readable = false;
	bool          writable = false;
	bool          error    = false;    // ERR / HUP / NVAL: recv() will report the real cause
};

// Waits up to timeoutMs (0 = just check, -1 = forever) and fills `results`
// with the sockets that are ready.
// Returns the number of results, 0 on timeout or when interrupted by a signal,
// or -1 on a real error (then lastError() still holds the cause).
// An empty `items` just sleeps for the timeout (WSAPoll rejects an empty set).
int pollSockets(const std::vector<PollItem>& items,
                std::vector<PollResult>& results,
                int timeoutMs);

} // namespace net

#endif /* NET_PLATFORM_HPP */