// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_PONG_HPP
#define KTH_DOMAIN_MESSAGE_PONG_HPP

#include <cstdint>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API pong {
    using ptr = std::shared_ptr<pong>;
    using const_ptr = std::shared_ptr<const pong>;

    static constexpr
    size_t satoshi_fixed_size(uint32_t /*version*/) {
        return sizeof(nonce_);
    }

    pong() = default;
    constexpr
    pong(uint64_t nonce)
        : nonce_(nonce)
    {}

    [[nodiscard]]
    friend bool operator==(pong const&, pong const&) = default;

    [[nodiscard]]
    constexpr
    uint64_t nonce() const {
        return nonce_;
    }

    static
    expect<pong> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    constexpr
    size_t serialized_size(uint32_t version) const {
        return satoshi_fixed_size(version);
    }

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
    uint64_t nonce_{0};
};

} // namespace kth::domain::message

#endif
