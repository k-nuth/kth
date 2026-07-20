// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONCEPTS_HPP
#define KTH_DOMAIN_CONCEPTS_HPP

#include <type_traits>

#include <kth/infrastructure/concepts.hpp>


// #define Reader typename //NOLINT
// #define Writer typename //NOLINT

namespace kth {

template <typename R, bool result = std::is_same<decltype(((R*)nullptr)->read_size_little_endian()), size_t>::value>  //NOLINT
constexpr
bool is_reader_helper(int /*unused*/) {
    return result;
}

template <typename R>
constexpr
bool is_reader_helper(...) {  //NOLINT
    return false;
}

template <typename R>
constexpr
bool is_reader() {
    return is_reader_helper<R>(0);
}

template <typename W, bool result = std::is_same<decltype(((W*)nullptr)->write_size_little_endian(0u)), void>::value>  //NOLINT
constexpr
bool is_writer_helper(int /*unused*/) {
    return result;
}

template <typename W>
constexpr
bool is_writer_helper(...) {  //NOLINT
    return false;
}

template <typename W>
constexpr
bool is_writer() {
    return is_writer_helper<W>(0);
}

} // namespace kth

#define KTH_IS_READER(R) typename std::enable_if<kth::is_reader<R>(), int>::type = 0  //NOLINT
#define KTH_IS_WRITER(W) typename std::enable_if<kth::is_writer<W>(), int>::type = 0  //NOLINT

#endif // KTH_DOMAIN_CONCEPTS_HPP
