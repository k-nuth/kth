// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_NONCOPYABLE_HPP
#define KTH_INFRASTRUCTURE_NONCOPYABLE_HPP

#include <kth/infrastructure/define.hpp>

namespace kth {

struct KI_API noncopyable {
    noncopyable(const noncopyable&) = delete;
    void operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
};

} // namespace kth

#endif
