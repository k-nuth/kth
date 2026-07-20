// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_FEATURES_METRIC_IPP
#define KTH_INFRASTRUCTURE_LOG_FEATURES_METRIC_IPP

namespace kth::log {
namespace features {

template <typename BaseType>
metric_feature<BaseType>::metric_feature()
  : BaseType(), metric_attribute_(metric_type())
{
    BaseType::add_attribute_unlocked(attributes::metric_type::get_name(),
        metric_attribute_);
}

template <typename BaseType>
metric_feature<BaseType>::metric_feature(const metric_feature& x)
  : BaseType(static_cast<const BaseType&>(x)),
    metric_attribute_(x.metric_attribute_)
{
    BaseType::attributes()[attributes::metric_type::get_name()] = metric_attribute_;
}

template <typename BaseType>
template <typename Arguments>
metric_feature<BaseType>::metric_feature(Arguments const& arguments)
  : BaseType(arguments),
    metric_attribute_(arguments[keywords::metric || metric_type()])
{
    BaseType::add_attribute_unlocked(attributes::metric_type::get_name(),
        metric_attribute_);
}

template <typename BaseType>
typename metric_feature<BaseType>::metric_type metric_feature<BaseType>::metric() const {
    BOOST_LOG_EXPR_IF_MT(boost::log::aux::shared_lock_guard<
        const threading_model> lock(this->get_threading_model());)

    return metric_attribute_.get();
}

template <typename BaseType>
void metric_feature<BaseType>::metric(const metric_type& value)
{
    BOOST_LOG_EXPR_IF_MT(boost::log::aux::exclusive_lock_guard<threading_model>
        lock(this->get_threading_model());)

    metric_attribute_.set(value);
}

template <typename BaseType>
const typename metric_feature<BaseType>::metric_attribute&
    metric_feature<BaseType>::get_metric_attribute() const {
    return metric_attribute_;
}

template <typename BaseType>
template <typename Arguments>
boost::log::record metric_feature<BaseType>::open_record_unlocked(
    Arguments const& arguments)
{
    return open_record_with_metric_unlocked(arguments,
        arguments[keywords::metric | boost::parameter::void_()]);
}

template <typename BaseType>
void metric_feature<BaseType>::swap_unlocked(metric_feature& x)
{
    BaseType::swap_unlocked(static_cast<BaseType&>(x));
    metric_attribute_.swap(x.metric_attribute_);
}

template <typename BaseType>
template <typename Arguments, typename Value>
boost::log::record metric_feature<BaseType>::open_record_with_metric_unlocked(
    Arguments const& arguments, const Value& value)
{
    metric_attribute_.set(value);
    return BaseType::open_record_unlocked(arguments);
}

template <typename BaseType>
template <typename Arguments>
boost::log::record metric_feature<BaseType>::open_record_with_metric_unlocked(
    Arguments const& arguments, boost::parameter::void_ /*unused*/)
{
    return BaseType::open_record_unlocked(arguments);
}

} // namespace features
} // namespace kth::log

#endif
