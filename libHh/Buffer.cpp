// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Buffer.h"

#if defined(_WIN32)
#include <fcntl.h>  // O_RDONLY
#include <io.h>     // read(), write(), open(), dup()
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>  // WaitForSingleObject(), CreateThread(), SetEvent(), etc.
#define BUFFER_USE_WIN32_THREAD
#else
#include <sys/select.h>  // select(), fd_set
#include <unistd.h>      // read(), write()
#endif

#if defined(_WIN32) && !defined(EWOULDBLOCK)  // undefined in __MINGW32__; was undefined until Visual Studio 2010
#define EWOULDBLOCK EAGAIN
#endif

#include <cerrno>   // errno, EINTR, etc.
#include <cstring>  // memcpy(), memmove()

#include "libHh/NetworkOrder.h"
#include "libHh/StringOp.h"
#include "libHh/Vec.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wold-style-cast"  // for FD_ZERO() macro
#endif

namespace hh {

constexpr int k_initial_size = 4096;
constexpr int k_read_size = 1024;
constexpr int k_write_size = 8192;

//----------------------------------------------------------------------------
// *** Buffer

void Buffer::shift() {
  assertx(_beg);
  // if (_n) std::copy(_ar.data() + _beg, _ar.data() + _beg + _n, _ar.data());  // overlap but shift is "left" so OK.
  std::memmove(_ar.data(), _ar.data() + _beg, unsigned(_n));  // safe for known type (char); extents may overlap.
  _beg = 0;
}

void Buffer::expand() {
  assertw(!_beg);  // not necessary, current implementation
  _ar.resize(assert_narrow_cast<int>(!_ar.num() ? k_initial_size : int64_t{_ar.num()} * 2));
}

//----------------------------------------------------------------------------
// *** RBuffer

#if defined(BUFFER_USE_WIN32_THREAD)

extern HANDLE g_buf_event_data_available;
HANDLE g_buf_event_data_available;  // manual-reset; used in libHwWindows/Hw.cpp

namespace {

HANDLE buf_event_data_copied;  // auto-reset
Vec<char, 2048> buf_buffer;
int buf_buffern;
int buf_fd;

DWORD WINAPI buf_thread_func(void* param) {
  dummy_use(param);
  for (;;) {
    if (1) assertx(WaitForSingleObject(g_buf_event_data_available, 0) == WAIT_TIMEOUT);
    assertx(buf_buffern == 0);
    int nread = HH_POSIX(read)(buf_fd, buf_buffer.data(), buf_buffer.num());
    if (nread < 0 && errno == EINVAL && GetLastError() == ERROR_NO_DATA) {
      // Cygwin bash has implemented a pipe using a non-blocking read mode, and there is no data, so we must wait.
      if (0) {
        my_sleep(0.005);  // However, busy-waiting wastes CPU cycles.
      } else {
        // Instead, we modify the wait mode on the pipe handle from PIPE_NOWAIT to PIPE_WAIT.
        DWORD mode = PIPE_READMODE_BYTE | PIPE_WAIT;
        assertx(SetNamedPipeHandleState(HANDLE(_get_osfhandle(buf_fd)), &mode, NULL, NULL));
      }
      continue;
    }
    buf_buffern = nread;
    if (nread < 0)
      assertnever(
          SSHOW("read", buf_fd, reinterpret_cast<uintptr_t>(buf_buffer.data()), buf_buffer.num(), nread, errno));
    assertx(SetEvent(g_buf_event_data_available));
    if (nread == 0) break;
    assertx(WaitForSingleObject(buf_event_data_copied, INFINITE) == WAIT_OBJECT_0);
    assertx(buf_buffern == 0);
  }
  return 0;
}

}  // namespace

#endif  // defined(BUFFER_USE_WIN32_THREAD)

RBuffer::RBuffer(int fd) : Buffer(fd) {
#if defined(BUFFER_USE_WIN32_THREAD)
  if (_fd == 0) {
    buf_fd = _fd;
    if (1) {
      // Win32 CreateWindow() stops responding if fd0 == STDIN is open on a pipe.
      buf_fd = HH_POSIX(dup)(_fd);
      assertx(buf_fd != _fd);
      assertx(!HH_POSIX(close)(_fd));
      // Create a dummy open file so fd0 is not re-used
      assertx(HH_POSIX(open)("NUL", O_RDONLY) == 0);  // (never freed)
    }
    // Only one RBuffer on fd 0 allowed.
    assertx(!g_buf_event_data_available);
    assertx(!buf_event_data_copied);
    // manual-reset; initial state non-signaled.
    g_buf_event_data_available = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    // auto-reset; initial state non-signaled.
    buf_event_data_copied = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, buf_thread_func, nullptr, 0, nullptr);
    assertx(thread);
  }
#endif  // defined(BUFFER_USE_WIN32_THREAD)
}

// if buffer is full, shift() || expand()
RBuffer::ERefill RBuffer::refill() {
  assertx(!eof() && !err());
  if (_beg + _n == _ar.num() && _beg)
    shift();
  else if (_n == _ar.num())
    expand();
  int ntoread = min(_ar.num() - _beg - _n, k_read_size), nread;
  assertx(ntoread);
#if defined(BUFFER_USE_WIN32_THREAD)
  if (_fd == 0) {
    if (WaitForSingleObject(g_buf_event_data_available, 0) == WAIT_TIMEOUT) return ERefill::no;
    assertx(ResetEvent(g_buf_event_data_available));
    nread = buf_buffern;
    if (nread < 0) {
      _err = true;
      return ERefill::other;
    }
    if (!nread) {
      _eof = true;
      return ERefill::other;
    }
    if (_ar.num() - _beg - _n < buf_buffern && _beg) shift();
    if (_ar.num() - _beg - _n < buf_buffern) expand();
    assertx(_ar.num() - _beg - _n >= buf_buffern);
    // std::copy(buf_buffer.data(), buf_buffer.data() + buf_buffern, &_ar[_beg + _n]);
    std::memcpy(&_ar[_beg + _n], buf_buffer.data(), unsigned(buf_buffern));  // safe for known element type (char)
    buf_buffern = 0;
    assertx(SetEvent(buf_event_data_copied));
  } else {
    nread = HH_POSIX(read)(_fd, &_ar[_beg + _n], unsigned(ntoread));
    if (nread < 0) {
      _err = true;
      return ERefill::other;
    }
    if (!nread) {
      _eof = true;
      return ERefill::other;
    }
  }
#else
  for (;;) {
    nread = HH_POSIX(read)(_fd, &_ar[_beg + _n], ntoread);
    if (nread < 0) {
      if (errno == EINTR) continue;  // for ATT UNIX (hpux)
      if (errno == EWOULDBLOCK || (EAGAIN != EWOULDBLOCK && errno == EAGAIN)) return ERefill::no;
    }
    if (nread < 0) {
      _err = true;
      return ERefill::other;
    }
    if (!nread) {
      _eof = true;
      return ERefill::other;
    }
    break;
  }
#endif  // defined(BUFFER_USE_WIN32_THREAD)
  _n += nread;
  return ERefill::yes;
}

// have read n bytes
void RBuffer::extract(int n) {
  assertx(n && n <= _n);
  _n -= n;
  _beg += n;
  if (!_n) _beg = 0;
  if (0 && _beg & 0x3) shift();  // safest but inefficient
}

bool RBuffer::has_line() const {
  for_int(i, num()) {
    if ((*this)[i] == '\n') return true;
  }
  return false;
}

bool RBuffer::extract_line(string& str) {
  const char* par = &_ar[_beg];  // optimization
  int i = 0;
  for (; i < _n; i++)
    if (par[i] == '\n') break;
  if (i == _n) return false;  // no complete line yet
  str.assign(par, i);         // skip trailing '\n'
  extract(i + 1);             // including trailing '\n'
  if (remove_at_end(str, "\r")) {
    static const bool ignore_dos_eol = getenv_bool("IGNORE_DOS_EOL");
    if (!ignore_dos_eol) Warning("RBuffer: stripping out control-M from DOS file");
  }
  return true;
}

void RBuffer::wait_for_input() {
  assertx(_fd == 0);
#if defined(BUFFER_USE_WIN32_THREAD)
  WaitForSingleObject(g_buf_event_data_available, INFINITE);
#elif defined(_WIN32)
#else
  // NOTE: Win32 select() is only valid with sockets (not ordinary pipes).
  fd_set fdr;
  FD_ZERO(&fdr);
  FD_SET(0, &fdr);
  select(1, &fdr, implicit_cast<fd_set*>(nullptr), implicit_cast<fd_set*>(nullptr), nullptr);
#endif
}

//----------------------------------------------------------------------------
// *** WBuffer

// no alignment problem to worry about since buffer is never word-accessed (at least not by user)
WBuffer::EFlush WBuffer::flush(int nb) {
  int nwritten;
  if (!nb) nb = _n;
  for (;;) {
    assertx(nb <= _n);
    if (!nb) return EFlush::all;
    nwritten = HH_POSIX(write)(_fd, &_ar[_beg], unsigned(nb));
    if (nwritten < 0) {
      if (errno == EINTR) continue;
      if (errno == EWOULDBLOCK || (EAGAIN != EWOULDBLOCK && errno == EAGAIN)) return EFlush::part;
      _err = true;
      return EFlush::other;
    }
    assertx(nwritten > 0 && nwritten <= nb);
    _beg += nwritten;
    _n -= nwritten;
    if (!_n) _beg = 0;
    nb -= nwritten;
  }
}

// if full, shift() || expand()
void WBuffer::put(const void* buf, int nbytes) {
  assertx(!eof() && !err());
  if (_beg + _n + nbytes > _ar.num() && _beg) shift();
  while (_n + nbytes > _ar.num()) expand();
  // std::copy(static_cast<const char*>(buf), static_cast<const char*>(buf) + nbytes, &_ar[_beg + _n]);
  std::memcpy(&_ar[_beg + _n], buf, unsigned(nbytes));  // safe for known element type (char)
  _n += nbytes;
  if (_n >= k_write_size) flush(k_write_size);
}

void WBuffer::put(char ch) { put(&ch, 1); }

void WBuffer::put(short i) {
  short t = i;
  to_std(&t);
  put(&t, 2);
}

void WBuffer::put(int i) {
  int t = i;
  to_std(&t);
  put(&t, 4);
}

void WBuffer::put(float f) {
  float t = f;
  to_std(&t);
  put(&t, 4);
}

}  // namespace hh
