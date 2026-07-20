// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_FEATURES_GAUGE_IPP
#define KTH_INFRASTRUCTURE_LOG_FEATURES_GAUGE_IPP

#include <cstdint>

#include <boost/log/attributes.hpp>
#include <boost/scope_exit.hpp>

namespace kth::log {
namespace features {

template <typename BaseType>
gauge_feature<BaseType>::gauge_feature(const gauge_feature& x)
    : BaseType(static_cast<const BaseType&>(x))
{}

template <typename BaseType>
template <typename Arguments>
gauge_feature<BaseType>::gauge_feature(Arguments const& arguments)
    : BaseType(arguments)
{}

template <typename BaseType>
template <typename Arguments>
boost::log::record gauge_feature<BaseType>::open_record_unlocked(Arguments const& arguments) {
    auto& set = BaseType::attributes();
    auto tag = add_gauge_unlocked(set, arguments[keywords::gauge | boost::parameter::void_()]);

    BOOST_SCOPE_EXIT_TPL((&tag)(&set)) {
        if (tag != set.end()) {
            set.erase(tag);
        }
    }
    BOOST_SCOPE_EXIT_END

    return BaseType::open_record_unlocked(arguments);
}

template <typename BaseType>
template <typename Value>
boost::log::attribute_set::iterator
gauge_feature<BaseType>::add_gauge_unlocked(boost::log::attribute_set& set, const Value& value) {
    auto tag = set.end();
    auto pair = BaseType::add_attribute_unlocked(attributes::gauge_type::get_name(),
        boost::log::attributes::constant<uint64_t>(value));

    if (pair.second) {
        tag = pair.first;
    }

    return tag;
}

template <typename BaseType>
boost::log::attribute_set::iterator
gauge_feature<BaseType>::add_gauge_unlocked(boost::log::attribute_set& set, boost::parameter::void_ /*unused*/) {
    return set.end();
}

} // namespace features
} // namespace kth::log

#endif
