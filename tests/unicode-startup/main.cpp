// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <kth/infrastructure/unicode/unicode.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <locale>

#ifdef _MSC_VER
namespace {
void diagnostic(wchar_t const* text) {
    if (!text) return;
    char buffer[1024];
    auto const size = WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, sizeof(buffer), nullptr, nullptr);
    DWORD written;
    if (size > 1) WriteFile(GetStdHandle(STD_ERROR_HANDLE), buffer, size - 1, &written, nullptr);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), "\n", 1, &written, nullptr);
}
wchar_t const* phase = L"enter main";
void mark(wchar_t const* value) { phase = value; diagnostic(value); }
void invalid_parameter(wchar_t const* expression, wchar_t const* function,
                       wchar_t const* file, unsigned, uintptr_t) {
    diagnostic(L"CRT invalid parameter");
    diagnostic(phase);
    diagnostic(function);
    diagnostic(expression);
    diagnostic(file);
    std::_Exit(90);
}
}
#endif

KTH_USE_MAIN

int kth::main(int argc, char* argv[]) {
#ifdef _MSC_VER
    _set_thread_local_invalid_parameter_handler(invalid_parameter);
#endif
    // Numeric formatting must stay independent of the Windows UTF-8 encoding
    // locale. This uses the same global numpunct facet as numeric streams.
    if (std::format(std::locale(), "{:L}", 1234567) != "1234567") return 11;
#ifdef _MSC_VER
    if (argc == 2 && std::strcmp(argv[1], "--closed-stdout") == 0) {
        if (std::fclose(stdout) != 0) return 7;
        return set_utf8_stdout() == std::unexpected(unicode_error::stdio_failed) ? 0 : 8;
    }
#endif
#ifdef _MSC_VER
    mark(L"set UTF8 stdio");
#endif
    if (!set_utf8_stdio()) return 9;
#ifdef _MSC_VER
    mark(L"set binary stdin");
#endif
    if (!set_binary_stdin()) return 9;
#ifdef _MSC_VER
    mark(L"set binary stdout");
#endif
    if (!set_binary_stdout()) return 9;
#ifdef _MSC_VER
    mark(L"check arguments/environment");
#endif
    char const* const text = "acción_日本_😀";
    if (argc != 2 || std::strcmp(argv[1], text) != 0) return 2;
    auto const environment = std::getenv("KTH_UNICODE_STARTUP");
    if (environment == nullptr || std::strcmp(environment, text) != 0) return 3;
#ifdef _MSC_VER
    mark(L"convert Unicode");
#endif
    auto const wide = to_utf16(text);
    if (!wide || to_utf8(*wide) != text) return 4;
    if (to_utf16("\xc0\xaf") != std::unexpected(unicode_error::invalid_encoding)) return 5;
#ifdef _MSC_VER
    mark(L"fwrite");
#endif
    if (std::fwrite(text, 1, std::strlen(text), stdout) != std::strlen(text)) return 6;
#ifdef _MSC_VER
    mark(L"flush stdout");
#endif
    if (std::fflush(stdout) != 0) return 10;
#ifdef _MSC_VER
    mark(L"return from main");
#endif
    return 0;
}
