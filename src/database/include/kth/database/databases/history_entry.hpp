// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HISTORY_ENTRY_HPP_
#define KTH_DATABASE_HISTORY_ENTRY_HPP_

#include <kth/domain.hpp>
#include <kth/database/define.hpp>

namespace kth::database {

struct KD_API history_entry {

    constexpr history_entry(uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum)
        : id_(id), point_(point), point_kind_(kind), height_(height), index_(index), value_or_checksum_(value_or_checksum)
    {}

    // Getters
    constexpr uint64_t id() const { return id_; }
    constexpr domain::chain::point const& point() const { return point_; }
    constexpr domain::chain::point_kind point_kind() const { return point_kind_; }
    constexpr uint64_t value_or_checksum() const { return value_or_checksum_; }
    constexpr uint32_t height() const { return height_; }
    constexpr uint32_t index() const { return index_; }

    static constexpr size_t serialized_size(domain::chain::point const& point) {
        return sizeof(uint64_t) + point.serialized_size(false) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t);
    }

    // Instance-side wrapper so the type satisfies `kth::Serializable`
    // and can flow through `kth::to_data_chunk`.
    [[nodiscard]]
    constexpr size_t serialized_size() const {
        return serialized_size(point_);
    }

    static
    expect<history_entry> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const {
        return factory_to_data(writer, id_, point_, point_kind_, height_, index_, value_or_checksum_);
    }

    data_chunk to_data() const;

    static
    data_chunk factory_to_data(uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum);

    static
    expect<void> factory_to_data(byte_writer& writer, uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum) {
        if (auto r = writer.write_little_endian<uint64_t>(id); ! r) return r;
        if (auto r = point.to_data(writer, false); ! r) return r;
        if (auto r = writer.write_byte(uint8_t(kind)); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(height); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(index); ! r) return r;
        return writer.write_little_endian<uint64_t>(value_or_checksum);
    }

private:
    uint64_t id_;
    domain::chain::point point_;
    domain::chain::point_kind point_kind_;
    uint32_t height_;
    uint32_t index_;
    uint64_t value_or_checksum_;
};

} // namespace kth::database


#endif // KTH_DATABASE_HISTORY_ENTRY_HPP_
