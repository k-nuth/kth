// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/log/sink.hpp>


#if defined(KTH_LOG_LIBRARY_BOOST)

#include <map>
#include <string>

#include <boost/log/attributes.hpp>
#include <boost/log/common.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/smart_ptr/make_shared.hpp>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/log/attributes.hpp>
#include <kth/infrastructure/log/file_collector_repository.hpp>
#include <kth/infrastructure/log/severity.hpp>
#include <kth/infrastructure/unicode/ofstream.hpp>

#elif defined(KTH_LOG_LIBRARY_SPDLOG)
#include <print>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h> // or "../stdout_sinks.h" if no colors needed
#include <spdlog/sinks/basic_file_sink.h>
#endif

namespace kth::log {

#if defined(KTH_LOG_LIBRARY_BOOST)

using namespace boost::log;
using namespace boost::log::expressions;
using namespace boost::log::keywords;
using namespace boost::log::sinks;
using namespace boost::log::sinks::file;
using namespace boost::posix_time;

#define TIME_FORMAT "%Y-%m-%dT%H:%M:%S.%f"
#define TIME_STAMP attributes::timestamp.get_name()
#define TIME_FORMATTER format_date_time<ptime, char>(TIME_STAMP, TIME_FORMAT)
#define SEVERITY_FORMATTER attributes::severity
#define CHANNEL_FORMATTER "[" << attributes::channel << "]"
#define MESSAGE_FORMATTER smessage

#define LINE_FORMATTER boost::log::expressions::stream \
    << TIME_FORMATTER << " " \
    << SEVERITY_FORMATTER << " " \
    << CHANNEL_FORMATTER << " " \
    << MESSAGE_FORMATTER

using text_file_sink = synchronous_sink<text_file_backend>;
using text_stream_sink = synchronous_sink<text_ostream_backend>;

static
auto const base_filter =
    has_attr(attributes::channel) &&
    has_attr(attributes::severity) &&
    has_attr(attributes::timestamp);

static
auto const error_filter = base_filter && (
    (attributes::severity == severity::warning) ||
    (attributes::severity == severity::error) ||
    (attributes::severity == severity::fatal));

static
auto const info_filter = base_filter && (attributes::severity == severity::info);

static
auto const lean_filter = base_filter && (attributes::severity != severity::verbose);

static
std::map<severity, std::string> severity_mapping {
    { severity::verbose, "VERBOSE" },
    { severity::debug, "DEBUG" },
    { severity::info, "INFO" },
    { severity::warning, "WARNING" },
    { severity::error, "ERROR" },
    { severity::fatal, "FATAL" }
};

formatter& operator<<(formatter& stream, severity value) {
    stream << severity_mapping[value];
    return stream;
}

static
boost::shared_ptr<collector> file_collector(rotable_file const& rotation) {
    // rotation_size controls enable/disable so use zero as max sentinel.
    return kth::log::make_collector(
        rotation.archive_directory,
        rotation.maximum_archive_size == 0 ? max_size_t :
            rotation.maximum_archive_size,
        rotation.minimum_free_space,
        rotation.maximum_archive_files == 0 ? max_size_t :
            rotation.maximum_archive_files);
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
    sink->set_formatter(LINE_FORMATTER);

    // Register the sink with the logging core.
    core::get()->add_sink(sink);
    return sink;
}

template <typename Stream>
static
boost::shared_ptr<text_stream_sink> add_text_stream_sink(boost::shared_ptr<Stream>& stream) {
    // Construct a log sink.
    auto const sink = boost::make_shared<text_stream_sink>();
    auto const backend = sink->locked_backend();

    // Add a stream for the sink to write to.
    backend->add_stream(stream);

    // Flush the sink after each logical line.
    backend->auto_flush(true);

    // Add the formatter to the sink.
    sink->set_formatter(LINE_FORMATTER);

    // Register the sink with the logging core.
    core::get()->add_sink(sink);
    return sink;
}


void initialize() {
    class null_stream : public std::ostream {
    private:
        class null_buffer : public std::streambuf {
        public:
            int overflow(int value) override {
                return value;
            }
        } buffer_;
    public:
        null_stream()
            : std::ostream(&buffer_)
        {}
    };

    // Null stream instances used to disable log output.
    static auto debug_file = boost::make_shared<kth::ofstream>("/dev/null");
    static auto error_file = boost::make_shared<kth::ofstream>("/dev/null");
    static log::stream output_stream = boost::make_shared<null_stream>();
    static log::stream error_stream = boost::make_shared<null_stream>();
    initialize(debug_file, error_file, output_stream, error_stream, false);
}

void initialize(log::file& debug_file, log::file& error_file, bool verbose) {
    if (verbose) {
        add_text_stream_sink(debug_file)->set_filter(base_filter);
    } else {
        add_text_stream_sink(debug_file)->set_filter(lean_filter);
    }

    add_text_stream_sink(error_file)->set_filter(error_filter);
}

void initialize(log::file& debug_file, log::file& error_file, log::stream& output_stream, log::stream& error_stream, bool verbose) {
    initialize(debug_file, error_file, verbose);
    add_text_stream_sink(output_stream)->set_filter(info_filter);
    add_text_stream_sink(error_stream)->set_filter(error_filter);
}

void initialize(rotable_file const& debug_file, rotable_file const& error_file, bool verbose) {
    if (verbose) {
        add_text_file_sink(debug_file)->set_filter(base_filter);
    } else {
        add_text_file_sink(debug_file)->set_filter(lean_filter);
    }

    add_text_file_sink(error_file)->set_filter(error_filter);
}

void initialize(rotable_file const& debug_file, rotable_file const& error_file, log::stream& output_stream, log::stream& error_stream, bool verbose) {
    initialize(debug_file, error_file, verbose);
    add_text_stream_sink(output_stream)->set_filter(info_filter);
    add_text_stream_sink(error_stream)->set_filter(error_filter);
}

#elif defined(KTH_LOG_LIBRARY_SPDLOG)

void initialize(std::string const& debug_file, std::string const& error_file, bool stdout_enabled, bool verbose) {
    try
    {
        auto debug_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(debug_file, true);
        debug_file_sink->set_level(spdlog::level::debug);
        auto error_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(error_file, true);
        error_file_sink->set_level(spdlog::level::err);

        if (stdout_enabled) {
            auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            stdout_sink->set_level(spdlog::level::info);
            auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list({debug_file_sink, error_file_sink, stdout_sink}));
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
        } else {
            auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list({debug_file_sink, error_file_sink}));
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
        }

        spdlog::flush_every(std::chrono::seconds(2));
    }
    catch (spdlog::spdlog_ex const& ex) {
        std::println(stderr, "Log initialization failed: {}", ex.what());
    }
}

#endif


} // namespace kth::log
