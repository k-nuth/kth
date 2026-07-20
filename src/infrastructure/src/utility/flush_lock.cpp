// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/flush_lock.hpp>

#include <filesystem>
#include <memory>

#include <kth/infrastructure/unicode/file_lock.hpp>
#include <kth/infrastructure/unicode/ifstream.hpp>
#include <kth/infrastructure/unicode/ofstream.hpp>

namespace kth {

// static
bool flush_lock::create(std::string const& file) {
    kth::ofstream stream(file);
    return stream.good();
}

// static
bool flush_lock::exists(std::string const& file) {
    // kth::ifstream stream(file);
    // return stream.good();
    return std::filesystem::exists(file);
}

// static
bool flush_lock::destroy(std::string const& file) {
    return std::filesystem::remove(file);
}

flush_lock::flush_lock(path const& file)
    : file_(file.string()), locked_(false)
{}

bool flush_lock::try_lock() {
    return !exists(file_);
}

// Lock is idempotent, returns true if locked on return.
bool flush_lock::lock_shared()
{
    if (locked_) {
        return true;
}

    locked_ = create(file_);
    return locked_;
}

// Unlock is idempotent, returns true if unlocked on return.
bool flush_lock::unlock_shared()
{
    if ( ! locked_) {
        return true;
}

    locked_ = !destroy(file_);
    return !locked_;
}

} // namespace kth
