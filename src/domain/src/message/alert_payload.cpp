// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/alert_payload.hpp>

#include <kth/domain/constants.hpp>
// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

// Libbitcon doesn't use this.
const ec_uncompressed alert_payload::satoshi_public_key{
    {0x04, 0xfc, 0x97, 0x02, 0x84, 0x78, 0x40, 0xaa, 0xf1, 0x95, 0xde,
     0x84, 0x42, 0xeb, 0xec, 0xed, 0xf5, 0xb0, 0x95, 0xcd, 0xbb, 0x9b,
     0xc7, 0x16, 0xbd, 0xa9, 0x11, 0x09, 0x71, 0xb2, 0x8a, 0x49, 0xe0,
     0xea, 0xd8, 0x56, 0x4f, 0xf0, 0xdb, 0x22, 0x20, 0x9e, 0x03, 0x74,
     0x78, 0x2c, 0x09, 0x3b, 0xb8, 0x99, 0x69, 0x2d, 0x52, 0x4e, 0x9d,
     0x6a, 0x69, 0x56, 0xe7, 0xc5, 0xec, 0xbc, 0xd6, 0x82, 0x84}};

alert_payload::alert_payload(
    uint32_t version,
    uint64_t relay_until,
    uint64_t expiration,
    uint32_t id,
    uint32_t cancel,
    const std::vector<uint32_t>& set_cancel,
    uint32_t min_version,
    uint32_t max_version,
    const std::vector<std::string>& set_sub_version,
    uint32_t priority,
    std::string const& comment,
    std::string const& status_bar,
    std::string const& reserved)
    : version_(version),
      relay_until_(relay_until),
      expiration_(expiration),
      id_(id),
      cancel_(cancel),
      set_cancel_(set_cancel),
      min_version_(min_version),
      max_version_(max_version),
      set_sub_version_(set_sub_version),
      priority_(priority),
      comment_(comment),
      status_bar_(status_bar),
      reserved_(reserved) {
}

alert_payload::alert_payload(
    uint32_t version,
    uint64_t relay_until,
    uint64_t expiration,
    uint32_t id,
    uint32_t cancel,
    std::vector<uint32_t>&& set_cancel,
    uint32_t min_version,
    uint32_t max_version,
    std::vector<std::string>&& set_sub_version,
    uint32_t priority,
    std::string&& comment,
    std::string&& status_bar,
    std::string&& reserved)
    : version_(version),
      relay_until_(relay_until),
      expiration_(expiration),
      id_(id),
      cancel_(cancel),
      set_cancel_(std::move(set_cancel)),
      min_version_(min_version),
      max_version_(max_version),
      set_sub_version_(std::move(set_sub_version)),
      priority_(priority),
      comment_(std::move(comment)),
      status_bar_(std::move(status_bar)),
      reserved_(std::move(reserved)) {
}

// alert_payload::alert_payload(alert_payload const& x)
//     : alert_payload(
//           x.version_,
//           x.relay_until_,
//           x.expiration_,
//           x.id_,
//           x.cancel_,
//           x.set_cancel_,
//           x.min_version_,
//           x.max_version_,
//           x.set_sub_version_,
//           x.priority_,
//           x.comment_,
//           x.status_bar_,
//           x.reserved_) {
// }

// alert_payload::alert_payload(alert_payload&& x) noexcept
//     : alert_payload(
//           x.version_,
//           x.relay_until_,
//           x.expiration_,
//           x.id_,
//           x.cancel_,
//           std::move(x.set_cancel_),
//           x.min_version_,
//           x.max_version_,
//           std::move(x.set_sub_version_),
//           x.priority_,
//           std::move(x.comment_),
//           std::move(x.status_bar_),
//           std::move(x.reserved_)) {
// }

// alert_payload& alert_payload::operator=(alert_payload&& x) noexcept {
//     version_ = x.version_;
//     relay_until_ = x.relay_until_;
//     expiration_ = x.expiration_;
//     id_ = x.id_;
//     cancel_ = x.cancel_;
//     set_cancel_ = std::move(x.set_cancel_);
//     min_version_ = x.min_version_;
//     max_version_ = x.max_version_;
//     set_sub_version_ = std::move(x.set_sub_version_);
//     priority_ = x.priority_;
//     comment_ = std::move(x.comment_);
//     status_bar_ = std::move(x.status_bar_);
//     reserved_ = std::move(x.reserved_);
//     return *this;
// }

bool alert_payload::operator==(alert_payload const& x) const {
    return (version_ == x.version_) && (relay_until_ == x.relay_until_) && (expiration_ == x.expiration_) && (id_ == x.id_) && (cancel_ == x.cancel_) && (set_cancel_ == x.set_cancel_) && (min_version_ == x.min_version_) && (max_version_ == x.max_version_) && (set_sub_version_ == x.set_sub_version_) && (priority_ == x.priority_) && (comment_ == x.comment_) && (status_bar_ == x.status_bar_) && (reserved_ == x.reserved_);
}

bool alert_payload::operator!=(alert_payload const& x) const {
    return !(*this == x);
}

bool alert_payload::is_valid() const {
    return (version_ != 0) || (relay_until_ != 0) || (expiration_ != 0) || (id_ != 0) || (cancel_ != 0) || !set_cancel_.empty() || (min_version_ != 0) || (max_version_ != 0) || !set_sub_version_.empty() || (priority_ != 0) || !comment_.empty() || !status_bar_.empty() || !reserved_.empty();
}

void alert_payload::reset() {
    version_ = 0;
    relay_until_ = 0;
    expiration_ = 0;
    id_ = 0;
    cancel_ = 0;
    set_cancel_.clear();
    set_cancel_.shrink_to_fit();
    min_version_ = 0;
    max_version_ = 0;
    set_sub_version_.clear();
    set_sub_version_.shrink_to_fit();
    priority_ = 0;
    comment_.clear();
    comment_.shrink_to_fit();
    status_bar_.clear();
    status_bar_.shrink_to_fit();
    reserved_.clear();
    reserved_.shrink_to_fit();
}

data_chunk alert_payload::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void alert_payload::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t alert_payload::serialized_size(uint32_t /*version*/) const {
    size_t size = 40u +
                  infrastructure::message::variable_uint_size(comment_.size()) + comment_.size() +
                  infrastructure::message::variable_uint_size(status_bar_.size()) + status_bar_.size() +
                  infrastructure::message::variable_uint_size(reserved_.size()) + reserved_.size() +
                  infrastructure::message::variable_uint_size(set_cancel_.size()) + (4 * set_cancel_.size()) +
                  infrastructure::message::variable_uint_size(set_sub_version_.size());

    for (auto const& sub_version : set_sub_version_) {
        size += infrastructure::message::variable_uint_size(sub_version.size()) + sub_version.size();
    }

    return size;
}

uint32_t alert_payload::version() const {
    return version_;
}

void alert_payload::set_version(uint32_t value) {
    version_ = value;
}

uint64_t alert_payload::relay_until() const {
    return relay_until_;
}

void alert_payload::set_relay_until(uint64_t value) {
    relay_until_ = value;
}

uint64_t alert_payload::expiration() const {
    return expiration_;
}

void alert_payload::set_expiration(uint64_t value) {
    expiration_ = value;
}

uint32_t alert_payload::id() const {
    return id_;
}

void alert_payload::set_id(uint32_t value) {
    id_ = value;
}

uint32_t alert_payload::cancel() const {
    return cancel_;
}

void alert_payload::set_cancel(uint32_t value) {
    cancel_ = value;
}

std::vector<uint32_t>& alert_payload::set_cancel() {
    return set_cancel_;
}

const std::vector<uint32_t>& alert_payload::set_cancel() const {
    return set_cancel_;
}

void alert_payload::set_set_cancel(const std::vector<uint32_t>& value) {
    set_cancel_ = value;
}

void alert_payload::set_set_cancel(std::vector<uint32_t>&& value) {
    set_cancel_ = std::move(value);
}

uint32_t alert_payload::min_version() const {
    return min_version_;
}

void alert_payload::set_min_version(uint32_t value) {
    min_version_ = value;
}

uint32_t alert_payload::max_version() const {
    return max_version_;
}

void alert_payload::set_max_version(uint32_t value) {
    max_version_ = value;
}

std::vector<std::string>& alert_payload::set_sub_version() {
    return set_sub_version_;
}

const std::vector<std::string>& alert_payload::set_sub_version() const {
    return set_sub_version_;
}

void alert_payload::set_set_sub_version(const std::vector<std::string>& value) {
    set_sub_version_ = value;
}

void alert_payload::set_set_sub_version(std::vector<std::string>&& value) {
    set_sub_version_ = std::move(value);
}

uint32_t alert_payload::priority() const {
    return priority_;
}

void alert_payload::set_priority(uint32_t value) {
    priority_ = value;
}

std::string& alert_payload::comment() {
    return comment_;
}

std::string const& alert_payload::comment() const {
    return comment_;
}

void alert_payload::set_comment(std::string const& value) {
    comment_ = value;
}

void alert_payload::set_comment(std::string&& value) {
    comment_ = std::move(value);
}

std::string& alert_payload::status_bar() {
    return status_bar_;
}

std::string const& alert_payload::status_bar() const {
    return status_bar_;
}

void alert_payload::set_status_bar(std::string const& value) {
    status_bar_ = value;
}

void alert_payload::set_status_bar(std::string&& value) {
    status_bar_ = std::move(value);
}

std::string& alert_payload::reserved() {
    return reserved_;
}

std::string const& alert_payload::reserved() const {
    return reserved_;
}

void alert_payload::set_reserved(std::string const& value) {
    reserved_ = value;
}

void alert_payload::set_reserved(std::string&& value) {
    reserved_ = std::move(value);
}

} // namespace kth::domain::message
