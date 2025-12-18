// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/work.hpp>

#include <memory>
#include <string>
#include <utility>

#include <kth/infrastructure/utility/threadpool.hpp>

namespace kth {

work::work(threadpool& pool, std::string const& name)
    : name_(name)
    , executor_(pool.executor())
    , strand_(::asio::make_strand(executor_))
    , sequence_(executor_)
{}

} // namespace kth
