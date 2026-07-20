// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MESSAGE_MESSAGES_HPP
#define KTH_INFRASTUCTURE_MESSAGE_MESSAGES_HPP

// #include <algorithm>
// #include <cstdint>
// #include <cstddef>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::infrastructure::message {

KI_API size_t variable_uint_size(uint64_t value);

} // namespace kth::infrastructure::message

#endif
