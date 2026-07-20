// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_PLATFORM_HPP_
#define KTH_INFRASTRUCTURE_UTILITY_PLATFORM_HPP_

#include <filesystem>

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

#include <kth/infrastructure/utility/tristate.hpp>

namespace kth::platform {

#ifdef __GLIBC__    //TODO(fernando): replace using a BOOST macro or something like that
namespace {

kth::path rotational_path_base() {
    return "/sys/dev/block/";
}

kth::path rotational_path(uint16_t st_dev) {
    return rotational_path_base() /
           fmt::format("{}:{}", major(st_dev), minor(st_dev)) /
           "queue/rotational";
}

kth::path rotational_path() {
    return rotational_path_base() /
           "queue/rotational";
}

std::ifstream get_rotational_file(uint16_t st_dev) {
    std::ifstream f(rotational_path(st.st_dev), std::ios_base::in);
    if (f.is_open()) return f;
    f.open(rotational_path(), std::ios_base::in);
    return f;
}
#endif

tristate drive_is_rotational(kth::path const& file_path) {
#ifdef __GLIBC__    //TODO(fernando): replace using a BOOST macro or something like that
    struct stat st;
    if (stat(file_path, &st) != 0) return tristate::unknown;

    auto f = get_rotational_file(st.st_dev);
    if ( ! f.is_open()) return tristate::unknown;

    unsigned short val = 0xdead;
    f >> val;
    if (f.fail()) return tristate::unknown;
    return val == 1 ? tristate::yes : tristate::no;
#else
    return tristate::unknown;
#endif
}

} // namespace kth::platform

#endif // KTH_INFRASTRUCTURE_UTILITY_PLATFORM_HPP_
