// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_NOISE_FILE_HPP
#define KTH_NODE_DETAIL_NOISE_FILE_HPP

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/node/detail/scope_action.hpp>

namespace kth::node::detail {

struct noise_file_status {
    bool regular = false;
    bool owned = false;
    bool private_file = false;
    uint64_t size = 0;
};

template <size_t Size, typename Operations>
[[nodiscard]]
std::optional<byte_array<Size>> read_noise_file(Operations& operations,
    int fd, bool owner_only) {
    scope_action close([&] { operations.close(fd); }, true);
    noise_file_status status;
    if (operations.inspect(fd, status) != 0 || ! status.regular
        || (owner_only && ( ! status.owned || ! status.private_file)) || status.size != Size) {
        return std::nullopt;
    }
    byte_array<Size> bytes{};
    scope_action erase([&] { pseudo_random::wipe(bytes); }, true);
    size_t received = 0;
    while (received < bytes.size()) {
        auto const count = operations.read(fd, byte_span_mut{bytes}.subspan(received));
        if (count == -1 && operations.error() == EINTR) continue;
        if (count <= 0) return std::nullopt;
        received += static_cast<size_t>(count);
    }
    return bytes;
}

// Operations belong to this call, so tests can exercise interrupted and failed
// syscalls without replacing process-wide functions while the node is running.
template <typename Operations, typename Generate>
[[nodiscard]]
std::optional<hash_digest> load_noise_secret(Operations& operations,
    std::string const& path, Generate generate) {
    auto fd = operations.open_read(path);
    if (fd == -1 && operations.error() == ENOENT) {
        auto secret = generate();
        scope_action erase([&] { pseudo_random::wipe(secret); }, true);
        fd = operations.create(path);
        if (fd == -1) return std::nullopt;
        scope_action close([&] { operations.close(fd); }, true);
        if (operations.secure(fd) != 0) return std::nullopt;
        size_t written = 0;
        while (written < secret.size()) {
            auto const count = operations.write(fd, byte_span{secret}.subspan(written));
            if (count == -1 && operations.error() == EINTR) continue;
            if (count <= 0) return std::nullopt;
            written += static_cast<size_t>(count);
        }
        return secret;
    }
    if (fd == -1) return std::nullopt;
    return read_noise_file<32>(operations, fd, true);
}

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_NOISE_FILE_HPP
