// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ALERT_FORMATTED_PAYLOAD_HPP
#define KTH_DOMAIN_MESSAGE_ALERT_FORMATTED_PAYLOAD_HPP

#include <istream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API alert_payload {
    alert_payload() = default;
    alert_payload(uint32_t version, uint64_t relay_until, uint64_t expiration, uint32_t id, uint32_t cancel, const std::vector<uint32_t>& set_cancel, uint32_t min_version, uint32_t max_version, const std::vector<std::string>& set_sub_version, uint32_t priority, std::string const& comment, std::string const& status_bar, std::string const& reserved);
    alert_payload(uint32_t version, uint64_t relay_until, uint64_t expiration, uint32_t id, uint32_t cancel, std::vector<uint32_t>&& set_cancel, uint32_t min_version, uint32_t max_version, std::vector<std::string>&& set_sub_version, uint32_t priority, std::string&& comment, std::string&& status_bar, std::string&& reserved);

    bool operator==(alert_payload const& x) const;
    bool operator!=(alert_payload const& x) const;

    [[nodiscard]]
    uint32_t version() const;

    void set_version(uint32_t value);

    [[nodiscard]]
    uint64_t relay_until() const;

    void set_relay_until(uint64_t value);

    [[nodiscard]]
    uint64_t expiration() const;

    void set_expiration(uint64_t value);

    [[nodiscard]]
    uint32_t id() const;

    void set_id(uint32_t value);

    [[nodiscard]]
    uint32_t cancel() const;

    void set_cancel(uint32_t value);

    std::vector<uint32_t>& set_cancel();

    [[nodiscard]]
    const std::vector<uint32_t>& set_cancel() const;

    void set_set_cancel(const std::vector<uint32_t>& value);
    void set_set_cancel(std::vector<uint32_t>&& value);

    [[nodiscard]]
    uint32_t min_version() const;

    void set_min_version(uint32_t value);

    [[nodiscard]]
    uint32_t max_version() const;

    void set_max_version(uint32_t value);

    std::vector<std::string>& set_sub_version();

    [[nodiscard]]
    const std::vector<std::string>& set_sub_version() const;

    void set_set_sub_version(const std::vector<std::string>& value);
    void set_set_sub_version(std::vector<std::string>&& value);

    [[nodiscard]]
    uint32_t priority() const;

    void set_priority(uint32_t value);

    std::string& comment();

    [[nodiscard]]
    std::string const& comment() const;

    void set_comment(std::string const& value);
    void set_comment(std::string&& value);

    std::string& status_bar();

    [[nodiscard]]
    std::string const& status_bar() const;

    void set_status_bar(std::string const& value);
    void set_status_bar(std::string&& value);

    std::string& reserved();

    [[nodiscard]]
    std::string const& reserved() const;

    void set_reserved(std::string const& value);
    void set_reserved(std::string&& value);

    static
    expect<alert_payload> from_data(byte_reader& reader, uint32_t /*version*/) {
        auto version = reader.read_little_endian<uint32_t>();
        if (!version) return make_unexpected(version.error());

        auto relay_until = reader.read_little_endian<uint64_t>();
        if (!relay_until) return make_unexpected(relay_until.error());

        auto expiration = reader.read_little_endian<uint64_t>();
        if (!expiration) return make_unexpected(expiration.error());

        auto id = reader.read_little_endian<uint32_t>();
        if (!id) return make_unexpected(id.error());

        auto cancel = reader.read_little_endian<uint32_t>();
        if (!cancel) return make_unexpected(cancel.error());

        auto set_cancel_size = reader.read_size_little_endian();
        if (!set_cancel_size) return make_unexpected(set_cancel_size.error());

        std::vector<uint32_t> set_cancel;
        set_cancel.reserve(*set_cancel_size);
        for (size_t i = 0; i < *set_cancel_size; ++i) {
            auto value = reader.read_little_endian<uint32_t>();
            if (!value) return make_unexpected(value.error());
            set_cancel.push_back(*value);
        }

        auto const min_version = reader.read_little_endian<uint32_t>();
        if (!min_version) return make_unexpected(min_version.error());

        auto const max_version = reader.read_little_endian<uint32_t>();
        if (!max_version) return make_unexpected(max_version.error());

        auto const set_sub_version_size = reader.read_size_little_endian();
        if (!set_sub_version_size) return make_unexpected(set_sub_version_size.error());

        std::vector<std::string> set_sub_version;
        set_sub_version.reserve(*set_sub_version_size);
        for (size_t i = 0; i < *set_sub_version_size; ++i) {
            auto value = reader.read_string();
            if (!value) return make_unexpected(value.error());
            set_sub_version.emplace_back(std::move(*value));
        }

        auto const priority = reader.read_little_endian<uint32_t>();
        if (!priority) return make_unexpected(priority.error());

        auto const comment = reader.read_string();
        if (!comment) return make_unexpected(comment.error());

        auto const status_bar = reader.read_string();
        if (!status_bar) return make_unexpected(status_bar.error());

        auto const reserved = reader.read_string();
        if (!reserved) return make_unexpected(reserved.error());

        return alert_payload{
            *version,
            *relay_until,
            *expiration,
            *id,
            *cancel,
            std::move(set_cancel),
            *min_version,
            *max_version,
            std::move(set_sub_version),
            *priority,
            std::move(*comment),
            std::move(*status_bar),
            std::move(*reserved)
        };
    }

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        sink.write_4_bytes_little_endian(this->version_);
        sink.write_8_bytes_little_endian(relay_until_);
        sink.write_8_bytes_little_endian(expiration_);
        sink.write_4_bytes_little_endian(id_);
        sink.write_4_bytes_little_endian(cancel_);
        sink.write_variable_little_endian(set_cancel_.size());

        for (auto const& entry : set_cancel_) {
            sink.write_4_bytes_little_endian(entry);
}

        sink.write_4_bytes_little_endian(min_version_);
        sink.write_4_bytes_little_endian(max_version_);
        sink.write_variable_little_endian(set_sub_version_.size());

        for (auto const& entry : set_sub_version_) {
            sink.write_string(entry);
}

        sink.write_4_bytes_little_endian(priority_);
        sink.write_string(comment_);
        sink.write_string(status_bar_);
        sink.write_string(reserved_);
    }

    //void to_data(uint32_t version, writer& sink) const;
    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


    static
    const ec_uncompressed satoshi_public_key;

private:
    uint32_t version_{0};
    uint64_t relay_until_{0};
    uint64_t expiration_{0};
    uint32_t id_{0};
    uint32_t cancel_{0};
    std::vector<uint32_t> set_cancel_;
    uint32_t min_version_{0};
    uint32_t max_version_{0};
    std::vector<std::string> set_sub_version_;
    uint32_t priority_{0};
    std::string comment_;
    std::string status_bar_;
    std::string reserved_;
};

} // namespace kth::domain::message

#endif
