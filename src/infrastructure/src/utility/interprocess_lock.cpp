// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/interprocess_lock.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include <kth/infrastructure/unicode/file_lock.hpp>
#include <kth/infrastructure/unicode/ofstream.hpp>

namespace kth {

// static
bool interprocess_lock::create(std::string const& file) {
    kth::ofstream stream(file);
    return stream.good();
}

// static
bool interprocess_lock::destroy(std::string const& file) {
    return std::filesystem::remove(file);
}

interprocess_lock::interprocess_lock(path const& file)
    : file_(file.string())
{}

interprocess_lock::~interprocess_lock() {
    unlock();
}

// Lock is not idempotent, returns false if already locked.
// This succeeds if no other process has exclusive or sharable ownership.
bool interprocess_lock::lock() {
    if ( ! create(file_)) {
        return false;
    }

    lock_ = std::make_shared<lock_file>(file_);
    return lock_->try_lock();
}

// Unlock is idempotent, returns true if unlocked on return.
// This may leave the lock file behind, which is not a problem.
bool interprocess_lock::unlock() {
    if ( ! lock_) {
        return true;
    }

    lock_.reset();
    return destroy(file_);
}

} // namespace kth
