// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_FEATURES_TIMER_HPP
#define KTH_INFRASTRUCTURE_LOG_FEATURES_TIMER_HPP

#include <chrono>

#include <boost/log/sources/features.hpp>
#include <boost/log/sources/threading_models.hpp>
#include <boost/log/utility/strictest_lock.hpp>

namespace kth::log {
namespace attributes {

BOOST_LOG_ATTRIBUTE_KEYWORD(timer, "Timer", std::chrono::milliseconds)

} // namespace attributes

namespace features {

template <typename BaseType>
class timer_feature : public BaseType
{
public:
    using char_type = typename BaseType::char_type;
    using threading_model = typename BaseType::threading_model;

public:
    timer_feature() = default;

    timer_feature(timer_feature const& x);

    template <typename Arguments>
    explicit
    timer_feature(Arguments const& arguments);

    using open_record_lock = typename boost::log::strictest_lock<
        boost::lock_guard<threading_model>,
        typename BaseType::open_record_lock,
        typename BaseType::add_attribute_lock,
        typename BaseType::remove_attribute_lock
    >::type;

protected:
    template <typename Arguments>
    boost::log::record open_record_unlocked(Arguments const& arguments);

private:
    template <typename Value>
    boost::log::attribute_set::iterator add_timer_unlocked(boost::log::attribute_set& set, const Value& value);

    boost::log::attribute_set::iterator add_timer_unlocked(boost::log::attribute_set& set, boost::parameter::void_ /*unused*/);
};

struct timer
{
    template <typename BaseType>
    struct apply
    {
        using type = timer_feature<BaseType>;
    };
};

} // namespace features

namespace keywords {

BOOST_PARAMETER_KEYWORD(tag, timer)

} // namespace keywords

} // namespace kth::log

#include <kth/infrastructure/impl/log/features/timer.ipp>

#endif
