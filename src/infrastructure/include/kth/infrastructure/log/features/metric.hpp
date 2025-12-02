// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_FEATURES_METRIC_HPP
#define KTH_INFRASTRUCTURE_LOG_FEATURES_METRIC_HPP

#include <boost/log/sources/features.hpp>
#include <boost/log/sources/threading_models.hpp>
#include <boost/log/utility/strictest_lock.hpp>

namespace kth::log {

namespace attributes {

BOOST_LOG_ATTRIBUTE_KEYWORD(metric, "Metric", std::string)

} // namespace attributes

namespace features {

template <typename BaseType>
class metric_feature
  : public BaseType
{
public:
    using char_type = typename BaseType::char_type;
    using threading_model = typename BaseType::threading_model;

    using metric_type = std::string;
    using metric_attribute = boost::log::attributes::mutable_constant<metric_type>;

    using open_record_lock = typename boost::log::strictest_lock<boost::lock_guard<threading_model> >::type;

    using swap_lock = typename boost::log::strictest_lock<
        typename BaseType::swap_lock,
        boost::log::aux::exclusive_lock_guard<threading_model>
    >::type;

public:
    metric_feature();
    metric_feature(const metric_feature& x);

    template <typename Arguments>
    explicit
    metric_feature(Arguments const& arguments);

    metric_type metric() const;
    void metric(const metric_type& value);

protected:
    metric_attribute const& get_metric_attribute() const;

    template <typename Arguments>
    boost::log::record open_record_unlocked(Arguments const& arguments);

    void swap_unlocked(metric_feature& x);

private:
    template <typename Arguments, typename Value>
    boost::log::record open_record_with_metric_unlocked(
        Arguments const& arguments, const Value& value);

    template <typename Arguments>
    boost::log::record open_record_with_metric_unlocked(
        Arguments const& arguments, boost::parameter::void_ /*unused*/);

private:
    metric_attribute metric_attribute_;
};

struct metric
{
    template <typename BaseType>
    struct apply
    {
        using type = metric_feature<BaseType>;
    };
};

} // namespace features

namespace keywords {

BOOST_PARAMETER_KEYWORD(tag, metric)

} // namespace keywords

} // namespace kth::log

#include <kth/infrastructure/impl/log/features/metric.ipp>

#endif
