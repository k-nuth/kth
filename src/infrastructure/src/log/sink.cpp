// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/log/sink.hpp>

#include <print>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace kth::log {

void initialize(std::string const& debug_file, std::string const& error_file, bool stdout_enabled, bool verbose) {
    try {
        auto debug_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(debug_file, true);
        debug_file_sink->set_level(verbose ? spdlog::level::trace : spdlog::level::debug);

        auto error_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(error_file, true);
        error_file_sink->set_level(spdlog::level::err);

        if (stdout_enabled) {
            auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            stdout_sink->set_level(spdlog::level::info);
            auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list({debug_file_sink, error_file_sink, stdout_sink}));
            logger->set_level(verbose ? spdlog::level::trace : spdlog::level::debug);
            spdlog::set_default_logger(logger);
        } else {
            auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list({debug_file_sink, error_file_sink}));
            logger->set_level(verbose ? spdlog::level::trace : spdlog::level::debug);
            spdlog::set_default_logger(logger);
        }

        spdlog::flush_every(std::chrono::seconds(2));
    }
    catch (spdlog::spdlog_ex const& ex) {
        std::println(stderr, "Log initialization failed: {}", ex.what());
    }
}

} // namespace kth::log
