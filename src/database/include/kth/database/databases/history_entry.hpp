// Copyright (c) 2016-2025 Knuth Project developers.
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

    data_chunk to_data() const;
    void to_data(std::ostream& stream) const;

    template <typename W, KTH_IS_WRITER(W)>
    void to_data(W& sink) const {
        factory_to_data(sink, id_, point_, point_kind_, height_, index_, value_or_checksum_);
    }

    static
    expect<history_entry> from_data(byte_reader& reader);

    static
    data_chunk factory_to_data(uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum);

    static
    void factory_to_data(std::ostream& stream, uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum);

    template <typename W, KTH_IS_WRITER(W)>
    static
    void factory_to_data(W& sink, uint64_t id, domain::chain::point const& point, domain::chain::point_kind kind, uint32_t height, uint32_t index, uint64_t value_or_checksum) {
        sink.write_8_bytes_little_endian(id);
        point.to_data(sink, false);
        sink.write_byte(uint8_t(kind));
        sink.write_4_bytes_little_endian(height);
        sink.write_4_bytes_little_endian(index);
        sink.write_8_bytes_little_endian(value_or_checksum);
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
