// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PATH_HPP_
#define KTH_INFRASTRUCTURE_PATH_HPP_

#include <filesystem>

namespace fs = std::filesystem;

namespace kth {

//Note(fernando): std::filesystem::u8path will be deprecated in C++20
// https://stackoverflow.com/questions/54004000/why-is-stdfilesystemu8path-deprecated-in-c20

using path = std::filesystem::path;

} // namespace kth

#endif // KTH_INFRASTRUCTURE_PATH_HPP_
