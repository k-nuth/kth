// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/alert.hpp>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const alert::command = "alert";
uint32_t const alert::version_minimum = version::level::minimum;
uint32_t const alert::version_maximum = version::level::maximum;

alert::alert(data_chunk const& payload, data_chunk const& signature)
    : payload_(payload), signature_(signature) {
}

alert::alert(data_chunk&& payload, data_chunk&& signature)
    : payload_(std::move(payload)), signature_(std::move(signature)) {
}

bool alert::operator==(alert const& x) const {
    return (payload_ == x.payload_) && (signature_ == x.signature_);
}

bool alert::operator!=(alert const& x) const {
    return !(*this == x);
}

bool alert::is_valid() const {
    return !payload_.empty() || !signature_.empty();
}

void alert::reset() {
    payload_.clear();
    payload_.shrink_to_fit();
    signature_.clear();
    signature_.shrink_to_fit();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<alert> alert::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const payload_size = reader.read_size_little_endian();
    if ( ! payload_size) {
        return std::unexpected(payload_size.error());
    }

    auto const payload = reader.read_bytes(*payload_size);
    if ( ! payload) {
        return std::unexpected(payload.error());
    }

    auto const signature_size = reader.read_size_little_endian();
    if ( ! signature_size) {
        return std::unexpected(signature_size.error());
    }

    auto const signature = reader.read_bytes(*signature_size);
    if ( ! signature) {
        return std::unexpected(signature.error());
    }

    return alert(
        data_chunk(payload->begin(), payload->end()),
        data_chunk(signature->begin(), signature->end()));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk alert::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void alert::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t alert::serialized_size(uint32_t /*version*/) const {
    return infrastructure::message::variable_uint_size(payload_.size()) + payload_.size() +
           infrastructure::message::variable_uint_size(signature_.size()) + signature_.size();
}

data_chunk& alert::payload() {
    return payload_;
}

data_chunk const& alert::payload() const {
    return payload_;
}

void alert::set_payload(data_chunk const& value) {
    payload_ = value;
}

void alert::set_payload(data_chunk&& value) {
    payload_ = std::move(value);
}

data_chunk& alert::signature() {
    return signature_;
}

data_chunk const& alert::signature() const {
    return signature_;
}

void alert::set_signature(data_chunk const& value) {
    signature_ = value;
}

void alert::set_signature(data_chunk&& value) {
    signature_ = std::move(value);
}

} // namespace kth::domain::message
