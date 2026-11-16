#include <test_helpers.hpp>

#include <ios>
#include <limits>
#include <sstream>
#include <utility>
#include <kth/infrastructure/unicode/unicode_streambuf.hpp>

TEST_CASE("unicode streambuf refuses invalid buffer sizes before allocation", "[unicode]") {
    std::wstringbuf wide;
    REQUIRE_THROWS_AS(kth::unicode_streambuf(&wide, 0), std::ios_base::failure);
    auto const overflowing = std::numeric_limits<size_t>::max() / 4 + 1;
    REQUIRE_THROWS_AS(kth::unicode_streambuf(&wide, overflowing), std::ios_base::failure);
}

TEST_CASE("unicode streambuf preserves buffered ASCII output", "[unicode]") {
    std::wstringbuf wide;
    {
        kth::unicode_streambuf buffer(&wide, 2);
        std::ostream output(&buffer);
        output << "buffered output" << std::flush;
        REQUIRE(output.good());
    }
    CHECK(wide.str() == L"buffered output");
}

TEST_CASE("unicode streambuf distinguishes invalid input from clean EOF", "[unicode]") {
    std::wstring const text{L'A', static_cast<wchar_t>(0xd800)};
    std::wstringbuf wide(text);
    kth::unicode_streambuf buffer(&wide, 1);
    std::istream input(&buffer);
    REQUIRE(input.get() == 'A');
    CHECK(input.get() == std::char_traits<char>::eof());
    CHECK(input.bad());
}

TEST_CASE("unicode streambuf reports clean EOF without badbit", "[unicode]") {
    std::wstringbuf wide(L"A");
    kth::unicode_streambuf buffer(&wide, 1);
    std::istream input(&buffer);
    REQUIRE(input.get() == 'A');
    CHECK(input.get() == std::char_traits<char>::eof());
    CHECK(input.eof());
    CHECK_FALSE(input.bad());
}

TEST_CASE("unicode streambuf can write again after a full-buffer conversion failure", "[unicode]") {
    std::wstringbuf wide;
    kth::unicode_streambuf buffer(&wide, 1);
    REQUIRE(buffer.sputn("\xc0\xaf" "a", 3) == 3);
    REQUIRE(buffer.sputc('b') == std::char_traits<char>::eof());
    REQUIRE(buffer.sputc('O') != std::char_traits<char>::eof());
    REQUIRE(buffer.sputc('K') != std::char_traits<char>::eof());
    REQUIRE(buffer.pubsync() == 0);
    CHECK(wide.str() == L"OK");
}

TEST_CASE("unicode streambuf can write again after a full-buffer short write", "[unicode]") {
    struct short_writer final : std::wstringbuf {
        bool fail_next = true;
        std::streamsize xsputn(wchar_t const* data, std::streamsize size) override {
            if (std::exchange(fail_next, false)) {
                return 0;
            }
            return std::wstringbuf::xsputn(data, size);
        }
    } wide;
    kth::unicode_streambuf buffer(&wide, 1);
    std::ostream output(&buffer);
    output.write("abcd", 4);
    REQUIRE(output.bad());
    output.clear();
    output << "OK" << std::flush;
    REQUIRE(output.good());
    CHECK(wide.str() == L"OK");
}
