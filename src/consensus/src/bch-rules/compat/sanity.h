// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2021-2025 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

bool glibc_sanity_test();
bool glibcxx_sanity_test();

/* The `TellGCC()` macro is used to suppress spurious warnings on newer GCC
 * whereby it warns about things that are clearly not the case in the
 * sourcecode. Note that for safety, this macro is turned into an assert() in
 * DEBUG builds. */
#if defined(DEBUG)
/* Debug builds (all compilers), treat as assert() */
#  include <cassert>
#  define TellGCC(expression) assert(expression)
#elif defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER)
/* GCC release mode; optimizer hint suppresses warnings*/
#  define TellGCC(expression) \
       do { \
           if (![&]() -> bool __attribute__((pure, noinline)) { return static_cast<bool>((expression)); }()) \
               __builtin_unreachable(); \
       } while (0)
#else
/* Release mode, all non-GCC compilers */
#  define TellGCC(expression) \
       do { \
           if constexpr (false) { static_cast<void>(static_cast<bool>((expression))); } \
       } while (0)
#endif
