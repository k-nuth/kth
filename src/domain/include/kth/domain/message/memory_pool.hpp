// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_MEMORY_POOL_HPP
#define KTH_DOMAIN_MESSAGE_MEMORY_POOL_HPP

#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API memory_pool {
    using ptr = std::shared_ptr<memory_pool>;
    using const_ptr = std::shared_ptr<const memory_pool>;

    static
    size_t satoshi_fixed_size(uint32_t version);

    // This is a default instance so is invalid.
    // The only way to make this valid is to deserialize it :/.
    memory_pool() = default;
    memory_pool(memory_pool const& x) = default;
    memory_pool(memory_pool&& x) = default;

    static
    expect<memory_pool> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W&  /*sink*/) const {
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


protected:
    memory_pool(bool insufficient_version);

private:
    bool insufficient_version_{true};
};

} // namespace kth::domain::message

#endif
