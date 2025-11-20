// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LOG_SOURCE_HPP_
#define KTH_INFRASTRUCTURE_LOG_SOURCE_HPP_

#include <string>

// #include <fmt/ostream.h>
#include <fmt/std.h>

#if defined(KTH_LOG_LIBRARY_BINLOG)
#include <binlog/binlog.hpp>
#elif defined(KTH_LOG_LIBRARY_BOOST)
#include <boost/log/attributes/clock.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#else
#include <boost/preprocessor/repetition/repeat.hpp>
#include <spdlog/spdlog.h>
#endif // defined(KTH_LOG_LIBRARY_BINLOG)

#include <kth/infrastructure/define.hpp>

#if defined(KTH_LOG_LIBRARY_BOOST)
#include <kth/infrastructure/log/attributes.hpp>
#include <kth/infrastructure/log/severity.hpp>

namespace kth::log {
using severity_source = boost::log::sources::severity_channel_logger_mt<severity, std::string>;

BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(source, severity_source) {
    static auto const name = attributes::timestamp.get_name();
    severity_source logger;
    logger.add_attribute(name, boost::log::attributes::utc_clock());
    return logger;
}
} // namespace kth::log
#endif // defined(KTH_LOG_LIBRARY_BOOST)

#define KTH_PP_ARG_N( _1, _2, _3, _4, _5, _6, _7, _8, _9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
                 _21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
                 _41,_42,_43,_44,_45,_46,_47,_48,_49,_50,_51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
                 _61,_62,_63,  N, ...) N

#ifdef _MSC_VER // Microsoft compilers
#define KTH_PP_EXPAND(x) x
#define KTH_PP_NARG(...) KTH_PP_EXPAND(KTH_PP_ARG_N(__VA_ARGS__, 63,62,61,60,                                                 \
                    59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,40, \
                    39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20, \
                    19,18,17,16,15,14,13,12,11,10,9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#else // Others
#define KTH_PP_RSEQ_N() 63,62,61,60,                                                 \
                    59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,40, \
                    39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20, \
                    19,18,17,16,15,14,13,12,11,10,9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#define KTH_PP_NARG(...) KTH_PP_NARG_(__VA_ARGS__,KTH_PP_RSEQ_N())
#define KTH_PP_NARG_(...) KTH_PP_ARG_N(__VA_ARGS__)
#endif

#if defined(KTH_LOG_LIBRARY_BINLOG)
//TODO: this is a proof of concept, it is not enabled in production

#include <binlog/binlog.hpp>

//  BINLOG_TRACE,
//  BINLOG_DEBUG,
//  BINLOG_INFO,
//  BINLOG_WARNING,
//  BINLOG_ERROR
//  BINLOG_CRITICAL

#define LOG_VERBOSE(modul,...)
#define LOG_DEBUG(...)
#define LOG_INFO(modul,...)
#define LOG_WARNING(modul,...)
#define LOG_ERROR(modul,...)
#define LOG_FATAL(modul,...)


// #define LOG_VERBOSE(modul,...) BINLOG_TRACE(modul, __VA_ARGS__)
// // #define LOG_DEBUG(modul,...)   BINLOG_DEBUG(modul, __VA_ARGS__)
// #define LOG_DEBUG(...)   BINLOG_DEBUG(__VA_ARGS__)
// #define LOG_INFO(modul,...)    BINLOG_INFO(modul, __VA_ARGS__)
// #define LOG_WARNING(modul,...) BINLOG_WARNING(modul, __VA_ARGS__)
// #define LOG_ERROR(modul,...)   BINLOG_ERROR(modul, __VA_ARGS__)
// #define LOG_FATAL(modul,...)   BINLOG_CRITICAL(modul, __VA_ARGS__)


#elif defined(KTH_LOG_LIBRARY_BOOST)

#define KI_LOG_SEVERITY(id, level) \
    BOOST_LOG_CHANNEL_SEV(kth::log::source::get(), id, kth::log::severity::level)

// #define LOG_VERBOSE(modul) KI_LOG_SEVERITY(modul, verbose)
// #define LOG_DEBUG(modul) KI_LOG_SEVERITY(modul, debug)
// #define LOG_INFO(modul) KI_LOG_SEVERITY(modul, info)
// #define LOG_WARNING(modul) KI_LOG_SEVERITY(modul, warning)
// #define LOG_ERROR(modul) KI_LOG_SEVERITY(modul, error)
// #define LOG_FATAL(modul) KI_LOG_SEVERITY(modul, fatal)

#define CONCAT(a, b) a ## b
#define CONCAT2(a, b) CONCAT(a, b)

#define KTH_LOG_HELPER_0(modul,severiry) KI_LOG_SEVERITY(modul,severiry)
#define KTH_LOG_HELPER_1(modul,severiry,A1) KI_LOG_SEVERITY(modul,severiry) << A1
#define KTH_LOG_HELPER_2(modul,severiry,A1,A2) KI_LOG_SEVERITY(modul,severiry) << A1 << A2
#define KTH_LOG_HELPER_3(modul,severiry,A1,A2,A3) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3
#define KTH_LOG_HELPER_4(modul,severiry,A1,A2,A3,A4) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4
#define KTH_LOG_HELPER_5(modul,severiry,A1,A2,A3,A4,A5) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5
#define KTH_LOG_HELPER_6(modul,severiry,A1,A2,A3,A4,A5,A6) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6
#define KTH_LOG_HELPER_7(modul,severiry,A1,A2,A3,A4,A5,A6,A7) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7
#define KTH_LOG_HELPER_8(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8
#define KTH_LOG_HELPER_9(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9
#define KTH_LOG_HELPER_10(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10
#define KTH_LOG_HELPER_11(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11
#define KTH_LOG_HELPER_12(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12
#define KTH_LOG_HELPER_13(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13
#define KTH_LOG_HELPER_14(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14
#define KTH_LOG_HELPER_15(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15
#define KTH_LOG_HELPER_16(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16
#define KTH_LOG_HELPER_17(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17
#define KTH_LOG_HELPER_18(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18
#define KTH_LOG_HELPER_19(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19
#define KTH_LOG_HELPER_20(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20
#define KTH_LOG_HELPER_21(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21
#define KTH_LOG_HELPER_22(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22
#define KTH_LOG_HELPER_23(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23
#define KTH_LOG_HELPER_24(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24
#define KTH_LOG_HELPER_25(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25
#define KTH_LOG_HELPER_26(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26
#define KTH_LOG_HELPER_27(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27
#define KTH_LOG_HELPER_28(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27 << A28
#define KTH_LOG_HELPER_29(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28,A29) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27 << A28 << A29
#define KTH_LOG_HELPER_30(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28,A29,A30) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27 << A28 << A29 << A30
#define KTH_LOG_HELPER_31(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28,A29,A30,A31) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27 << A28 << A29 << A30 << A31
#define KTH_LOG_HELPER_32(modul,severiry,A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15,A16,A17,A18,A19,A20,A21,A22,A23,A24,A25,A26,A27,A28,A29,A30,A31,A32) KI_LOG_SEVERITY(modul,severiry) << A1 << A2 << A3 << A4 << A5 << A6 << A7 << A8 << A9 << A10 << A11 << A12 << A13 << A14 << A15 << A16 << A17 << A18 << A19 << A20 << A21 << A22 << A23 << A24 << A25 << A26 << A27 << A28 << A29 << A30 << A31 << A32


// Workaround for  MSVC
#define KTH_PP_PASS_ON(...) __VA_ARGS__
#define KTH_LOG_HELPER(modul,severity,...) KTH_PP_PASS_ON(CONCAT2(KTH_LOG_HELPER_, KTH_PP_NARG(__VA_ARGS__))(modul,severity,__VA_ARGS__))

#define LOG_VERBOSE(modul,...) KTH_LOG_HELPER(modul, verbose, __VA_ARGS__)
#define LOG_DEBUG(modul,...) KTH_LOG_HELPER(modul, debug, __VA_ARGS__)
#define LOG_INFO(modul,...) KTH_LOG_HELPER(modul, info, __VA_ARGS__)
#define LOG_WARNING(modul,...) KTH_LOG_HELPER(modul, warning, __VA_ARGS__)
#define LOG_ERROR(modul,...) KTH_LOG_HELPER(modul, error, __VA_ARGS__)
#define LOG_FATAL(modul,...) KTH_LOG_HELPER(modul, fatal, __VA_ARGS__)

// #define LOG_VERBOSE(modul) KI_LOG_SEVERITY(modul, verbose)
// #define LOG_DEBUG(modul) KI_LOG_SEVERITY(modul, debug)
// #define LOG_INFO(modul) KI_LOG_SEVERITY(modul, info)
// #define LOG_WARNING(modul) KI_LOG_SEVERITY(modul, warning)
// #define LOG_ERROR(modul) KI_LOG_SEVERITY(modul, error)
// #define LOG_FATAL(modul) KI_LOG_SEVERITY(modul, fatal)

#else

#define KTH_FOLD(z, n, text)  text
#define KTH_STRREP(str, n) BOOST_PP_REPEAT(n, KTH_FOLD, str)
#define LOG_VERBOSE(...) spdlog::trace(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)
#define LOG_INFO(...) spdlog::info(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)
#define LOG_WARNING(...) spdlog::warn(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)
#define LOG_FATAL(...) spdlog::critical(KTH_STRREP("{}", KTH_PP_NARG(__VA_ARGS__)), __VA_ARGS__)

#endif // defined(KTH_LOG_LIBRARY_BINLOG)

#endif // KTH_INFRASTRUCTURE_LOG_SOURCE_HPP_