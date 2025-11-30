// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_PONG_HPP
#define KTH_DOMAIN_MESSAGE_PONG_HPP

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API pong {
    using ptr = std::shared_ptr<pong>;
    using const_ptr = std::shared_ptr<const pong>;

    static
    size_t satoshi_fixed_size(uint32_t version);

    pong() = default;
    pong(uint64_t nonce);

    bool operator==(pong const& x) const;
    bool operator!=(pong const& x) const;


    [[nodiscard]]
    uint64_t nonce() const;

    void set_nonce(uint64_t value);

    static
    expect<pong> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        sink.write_8_bytes_little_endian(nonce_);
    }

    //void to_data(uint32_t version, writer& sink) const;
    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;


private:
    uint64_t nonce_{0};
    bool valid_{false};
};

} // namespace kth::domain::message

#endif
