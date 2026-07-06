// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/script.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

// static
expect<script> script::parse_from(std::string_view mnemonic) {
    std::string trimmed{mnemonic};
    boost::trim(trimmed);
    chain::script s;
    if ( ! s.from_string(trimmed) && ! trimmed.empty()) {
        return std::unexpected(kth::error::illegal_value);
    }
    return script{s};
}

// static
expect<script> script::from_data_chunk(data_chunk const& value) {
    byte_reader reader(value);
    auto script_exp = chain::script::from_data(reader, false);
    if ( ! script_exp) {
        return std::unexpected(kth::error::illegal_value);
    }
    return script{std::move(*script_exp)};
}

// static
expect<script> script::parse_tokens(std::vector<std::string> const& tokens) {
    return parse_from(join(tokens));
}

data_chunk script::to_data() const {
    return kth::to_data_chunk(value_, false);
}

std::string script::to_string() const {
    static constexpr auto flags = machine::script_flags::all_rules;
    return value_.to_string(flags);
}

} // namespace kth::domain::config
