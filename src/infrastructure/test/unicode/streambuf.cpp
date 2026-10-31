#include <test_helpers.hpp>

#include <ios>
#include <limits>
#include <sstream>
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
