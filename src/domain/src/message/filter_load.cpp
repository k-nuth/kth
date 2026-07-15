// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_load.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const filter_load::command = "filterload";
uint32_t const filter_load::version_minimum = version::level::bip37;
uint32_t const filter_load::version_maximum = version::level::maximum;

filter_load::filter_load(data_chunk filter, uint32_t hash_functions, uint32_t tweak, uint8_t flags)
    : filter_(std::move(filter)), hash_functions_(hash_functions), tweak_(tweak), flags_(flags)
{}

// static
expect<filter_load> filter_load::create(data_chunk filter, uint32_t hash_functions, uint32_t tweak, uint8_t flags) {
    // BIP37 caps both; neither can be encoded above the cap.
    if (filter.size() > max_filter_load) {
        return std::unexpected(error::invalid_filter_load);
    }
    if (hash_functions > max_filter_functions) {
        return std::unexpected(error::invalid_filter_load);
    }
    return filter_load(std::move(filter), hash_functions, tweak, flags);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<filter_load> filter_load::from_data(byte_reader& reader, uint32_t version) {
    auto const size = reader.read_size_little_endian();
    if ( ! size) {
        return std::unexpected(size.error());
    }
    if (*size > max_filter_load) {
        return std::unexpected(error::invalid_filter_load);
    }
    auto const filter = reader.read_bytes(*size);
    if ( ! filter) {
        return std::unexpected(filter.error());
    }
    auto const hash_functions = reader.read_little_endian<uint32_t>();
    if ( ! hash_functions) {
        return std::unexpected(hash_functions.error());
    }
    auto const tweak = reader.read_little_endian<uint32_t>();
    if ( ! tweak) {
        return std::unexpected(tweak.error());
    }
    auto const flags = reader.read_byte();
    if ( ! flags) {
        return std::unexpected(flags.error());
    }

    if (version < filter_load::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return create(
        data_chunk(filter->begin(), filter->end()),
        *hash_functions,
        *tweak,
        *flags
    );
}

// Serialization.
//-----------------------------------------------------------------------------

size_t filter_load::serialized_size(uint32_t /*version*/) const {
    return 1u + 4u + 4u + infrastructure::message::variable_uint_size(filter_.size()) + filter_.size();
}

data_chunk const& filter_load::filter() const {
    return filter_;
}

uint32_t filter_load::hash_functions() const {
    return hash_functions_;
}

uint32_t filter_load::tweak() const {
    return tweak_;
}

uint8_t filter_load::flags() const {
    return flags_;
}

expect<void> filter_load::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_variable_little_endian(filter_.size()); ! r) return r;
        if (auto r = writer.write_bytes(filter_); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(hash_functions_); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(tweak_); ! r) return r;
        if (auto r = writer.write_byte(flags_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
