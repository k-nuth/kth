// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONCEPTS_HPP
#define KTH_INFRASTRUCTURE_CONCEPTS_HPP

#include <concepts>
#include <type_traits>

template <typename T>
concept trivially_copyable = std::is_trivially_copyable_v<T>;

#endif // KTH_INFRASTRUCTURE_CONCEPTS_HPP
