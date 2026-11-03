// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_INFRASTRUCTURE_EXPECT_HPP
#define KTH_INFRASTRUCTURE_EXPECT_HPP

#include <expected>
#include <kth/infrastructure/error.hpp>

namespace kth {

/// Common result type: a value or an error from Knuth's general category.
template <typename T>
using expect = std::expected<T, code>;

/// Construct a failed result using Knuth's common error type.
inline std::unexpected<code> fail(code ec) noexcept {
    return std::unexpected<code>{ec};
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_EXPECT_HPP
