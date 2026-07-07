// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ALERT_HPP
#define KTH_DOMAIN_MESSAGE_ALERT_HPP

#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API alert {
    using ptr = std::shared_ptr<alert>;
    using const_ptr = std::shared_ptr<const alert>;

    alert() = default;
    alert(data_chunk const& payload, data_chunk const& signature);
    alert(data_chunk&& payload, data_chunk&& signature);

    [[nodiscard]]
    friend bool operator==(alert const&, alert const&) = default;

    data_chunk& payload();

    [[nodiscard]]
    data_chunk const& payload() const;

    void set_payload(data_chunk const& value);
    void set_payload(data_chunk&& value);

    data_chunk& signature();

    [[nodiscard]]
    data_chunk const& signature() const;

    void set_signature(data_chunk const& value);
    void set_signature(data_chunk&& value);

    [[nodiscard]]
    static
    expect<alert> from_data(byte_reader& reader, uint32_t /*version*/);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

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
    data_chunk payload_;
    data_chunk signature_;
};

} // namespace kth::domain::message

#endif
