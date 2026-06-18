// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/log/statsd_sink.hpp>

#include <map>
#include <string>

#include <boost/log/attributes.hpp>
#include <boost/log/common.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/log/features/counter.hpp>
#include <kth/infrastructure/log/features/gauge.hpp>
#include <kth/infrastructure/log/features/metric.hpp>
#include <kth/infrastructure/log/features/rate.hpp>
#include <kth/infrastructure/log/features/timer.hpp>
#include <kth/infrastructure/log/file_collector_repository.hpp>
#include <kth/infrastructure/log/severity.hpp>
#include <kth/infrastructure/log/udp_client_sink.hpp>
#include <kth/infrastructure/unicode/ofstream.hpp>
#include <kth/infrastructure/utility/asio.hpp>

namespace kth::log {

using namespace kth::infrastructure::config;
using namespace ::asio::ip;
using namespace boost::log;
using namespace boost::log::expressions;
using namespace boost::log::sinks;
using namespace boost::log::sinks::file;

using text_file_sink = synchronous_sink<text_file_backend>;
using text_udp_sink = synchronous_sink<udp_client_sink>;

static auto const statsd_filter = has_attr(attributes::metric) &&
    (has_attr(attributes::counter) || has_attr(attributes::gauge) ||
        has_attr(attributes::timer));

void statsd_formatter(const record_view& record, formatting_ostream& stream) {
    // Get the LineID attribute value and put it into the stream.
    stream << record[attributes::metric] << ":";

    if (has_attribute<int64_t>(attributes::counter_type::get_name())(record)) {
        stream << record[attributes::counter] << "|c";
    }

    if (has_attribute<uint64_t>(attributes::gauge_type::get_name())(record)) {
        stream << record[attributes::gauge] << "|g";
    }

    if (has_attribute<asio::milliseconds>(attributes::timer_type::get_name())(record)) {
        stream << record[attributes::timer].get().count() << "|ms";
    }

    if (has_attribute<float>(attributes::rate_type::get_name())(record)) {
        stream << "|@" << record[attributes::rate];
    }
}

static
boost::shared_ptr<collector> file_collector(rotable_file const& rotation) {
    // rotation_size controls enable/disable so use zero as max sentinel.
    return kth::log::make_collector(
        rotation.archive_directory,
        rotation.maximum_archive_size == 0 ? max_size_t : rotation.maximum_archive_size,
        rotation.minimum_free_space,
        rotation.maximum_archive_files == 0 ? max_size_t : rotation.maximum_archive_files);
}

static
boost::shared_ptr<text_file_sink> add_text_file_sink(rotable_file const& rotation) {
    // Construct a log sink.
    auto const sink = boost::make_shared<text_file_sink>();
    auto const backend = sink->locked_backend();

    // Add a file stream for the sink to write to.
    backend->set_file_name_pattern(rotation.original_log);

    // Set archival parameters.
    if (rotation.rotation_size != 0) {
        backend->set_rotation_size(rotation.rotation_size);
        backend->set_file_collector(file_collector(rotation));
    }

    // Flush the sink after each logical line.
    backend->auto_flush(true);

    // Add the formatter to the sink.
    sink->set_formatter(&statsd_formatter);

    // Register the sink with the logging core.
    core::get()->add_sink(sink);
    return sink;
}

void initialize_statsd(rotable_file const& file) {
    add_text_file_sink(file)->set_filter(statsd_filter);
}

static
boost::shared_ptr<text_udp_sink> add_udp_sink(threadpool& pool, authority const& server) {
    auto socket = boost::make_shared<udp::socket>(pool.service());
    socket->open(udp::v6());

    auto endpoint = boost::make_shared<udp::endpoint>(server.asio_ip(), server.port());

    // Construct a log sink.
    auto const backend = boost::make_shared<udp_client_sink>(socket, endpoint);
    auto const sink = boost::make_shared<text_udp_sink>(backend);

    // Add the formatter to the sink.
    sink->set_formatter(&statsd_formatter);

    // Register the sink with the logging core.
    core::get()->add_sink(sink);
    return sink;
}

void initialize_statsd(threadpool& pool, authority const& server) {
    if (server) {
        add_udp_sink(pool, server)->set_filter(statsd_filter);
    }
}

} // namespace kth::log
