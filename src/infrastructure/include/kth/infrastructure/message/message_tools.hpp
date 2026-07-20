// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MESSAGE_TOOLS_HPP_
#define KTH_INFRASTRUCTURE_MESSAGE_TOOLS_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/define.hpp>

namespace kth::infrastructure::message {

KI_API size_t variable_uint_size(uint64_t value);

} // namespace kth::infrastructure::message

#endif // KTH_INFRASTRUCTURE_MESSAGE_TOOLS_HPP_
