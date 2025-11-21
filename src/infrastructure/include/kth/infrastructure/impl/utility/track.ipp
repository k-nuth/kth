// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TRACK_IPP
#define KTH_INFRASTRUCTURE_TRACK_IPP

#include <atomic>
#include <cstddef>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/log/source.hpp>
#include <kth/infrastructure/utility/assert.hpp>

template <typename Shared>
std::atomic<size_t> track<Shared>::instances(0);

template <typename Shared>
track<Shared>::track(std::string const& DEBUG_ONLY(class_name))
#ifndef NDEBUG
  : class_(class_name)
#endif
{
#ifndef NDEBUG
    // spdlog::debug("[system] {}({})", class_, ++instances);
    spdlog::debug("[system] {}", class_);
#endif
}

template <typename Shared>
track<Shared>::~track() {
#ifndef NDEBUG
    spdlog::debug("[system] ~{}({})", class_, --instances);
#endif
}

#endif
