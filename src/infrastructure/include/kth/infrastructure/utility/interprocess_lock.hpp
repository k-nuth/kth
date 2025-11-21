// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_INTERPROCESS_LOCK_HPP
#define KTH_INFRASTRUCTURE_INTERPROCESS_LOCK_HPP

#include <filesystem>
#include <memory>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/infrastructure/unicode/file_lock.hpp>

namespace kth {

/// This class is not thread safe.
/// Guard a resource againt concurrent use by another instance of this app.
struct KI_API interprocess_lock {
    using path = kth::path;

    explicit
    interprocess_lock(path const& file);

    ~interprocess_lock();

    bool lock();
    bool unlock();

private:
    using lock_file = interprocess::file_lock;
    using lock_ptr = std::shared_ptr<lock_file>;

    static bool create(std::string const& file);
    static bool destroy(std::string const& file);

    lock_ptr lock_;
    std::string const file_;
};

} // namespace kth

#endif
