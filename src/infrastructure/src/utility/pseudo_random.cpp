// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/pseudo_random.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__EMSCRIPTEN__) || defined(__APPLE__)
#include <unistd.h>
#include <strings.h>
#else
#include <sys/random.h>
#include <strings.h>
#endif

namespace kth {

namespace {

// getentropy refuses more than this in one call, with EIO.
constexpr size_t max_getentropy_bytes = 256;

// Draw into `out`, retrying an interrupted syscall. Returns errno on a failure
// the caller has to report, or 0.
int draw(std::span<std::byte> out) noexcept {
    while ( ! out.empty()) {
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
        auto const chunk = std::min(max_getentropy_bytes, out.size());
        if (getentropy(out.data(), chunk) != 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno;
        }
        out = out.subspan(chunk);
#else
        auto const ret = ::getrandom(out.data(), out.size(), 0);
        if (ret < 0) {
            // A signal can interrupt the call while it blocks on an entropy
            // pool that is not seeded yet. Transient, so try again.
            if (errno == EINTR) {
                continue;
            }
            return errno;
        }
        // Modern kernels satisfy the whole request, but a short read is
        // permitted; resume where it stopped rather than assume.
        out = out.subspan(size_t(ret));
#endif
    }
    return 0;
}

} // namespace

// static
code pseudo_random::check_available() noexcept {
    std::byte probe[1];
    auto const err = draw(probe);
    return err == 0 ? code{} : code(err, std::system_category());
}

// static
void pseudo_random::wipe_bytes(std::span<std::byte> out) noexcept {
    if (out.empty()) {
        return;
    }

#if defined(_WIN32)
    // Documented never to be optimized away.
    ::SecureZeroMemory(out.data(), out.size());
#elif defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
    // Same contract, and it is the libc's job to keep it.
    ::explicit_bzero(out.data(), out.size());
#else
    std::memset(out.data(), 0, out.size());
    // No explicit_bzero here, so deny the optimizer the premise it needs to
    // drop the store: the "memory" clobber says an unseen party may read or
    // change these bytes, and passing the pointer as an input stops the buffer
    // itself from being reasoned about as dead.
    #if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : "r"(out.data()) : "memory");
    #else
        std::atomic_signal_fence(std::memory_order_seq_cst);
    #endif
#endif
}

// static
void pseudo_random::fill_bytes(std::span<std::byte> out) noexcept {
    auto const err = draw(out);
    if (err == 0) {
        return;
    }

    // Unreachable once check_available has passed at startup: the remaining
    // errno values (EINVAL, EFAULT) mean a bug in this file, and ENOSYS cannot
    // appear after a successful probe. There is nothing to report to a caller
    // and no way to carry on without entropy, so fail loudly and immediately
    // rather than hand back a buffer that is not random.
    std::fprintf(stderr,
        "FATAL: system CSPRNG failed after startup probe, errno=%d. "
        "Aborting rather than returning non-random bytes.\n", err);
    std::abort();
}

} // namespace kth
