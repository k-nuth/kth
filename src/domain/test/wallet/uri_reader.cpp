// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <optional>

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: uri reader tests

// Test helper that relies on bitcoin_uri.
static
bitcoin_uri parse(std::string const& uri, bool strict = true) {
    return uri_reader::parse<bitcoin_uri>(uri, strict);
}

// Same, but returns just the success bit — spelled out so tests can
// write `REQUIRE(parse_valid(x))` without inventing a `bool` cast on
// the type.
static
bool parse_valid(std::string const& uri, bool strict = true) {
    return parse(uri, strict).valid();
}

// Demonstrate custom uri_reader.
struct custom_reader : public uri_reader {
    custom_reader()
        : strict_(true), authority_(false) {
    }

    bool is_valid() const {
        return !myscheme.empty() && !authority_;
    }

    void set_strict(bool strict) {
        strict_ = strict;
    }

    virtual bool set_scheme(std::string const& scheme) {
        myscheme = scheme;
        return true;
    }

    virtual bool set_authority(std::string const& authority) {
        // This URI doesn't support an authority component.
        authority_ = true;
        return false;
    }

    virtual bool set_path(std::string const& path) {
        mypath = path;
        return true;
    }

    virtual bool set_fragment(std::string const& fragment) {
        // myfragment = boost::in_place(fragment);
        myfragment = std::optional<std::string>{std::in_place, fragment};
        return true;
    }

    virtual bool set_parameter(std::string const& key, std::string const& value) {
        if (key == "myparam1") {
            // myparam1 = boost::in_place(value);
            myparam1 = std::optional<std::string>{std::in_place, value};

        } else if (key == "myparam2") {
            // myparam2 = boost::in_place(value);
            myparam2 = std::optional<std::string>{std::in_place, value};
        } else {
            return !strict_;
        }

        return true;
    }

    std::string myscheme;
    std::string myauthority;
    std::string mypath;

    // Use optionals when there is a semantic distinction between no value and default value.
    std::optional<std::string> myfragment;
    std::optional<std::string> myparam1;
    std::optional<std::string> myparam2;

private:
    bool strict_;
    bool authority_;
};

TEST_CASE("uri reader parse typical uri test", "[uri reader]") {
    auto const uri = parse("bitcoin:113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD?amount=0.1");
    REQUIRE(uri.valid());
    REQUIRE((uri.payment() && uri.payment()->encoded_legacy() == "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD"));
    REQUIRE(uri.amount() == 10000000u);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r().empty());
}

TEST_CASE("uri reader parse positive scheme test", "[uri reader]") {
    REQUIRE(parse_valid("bitcoin:"));
    REQUIRE(parse_valid("Bitcoin:"));
    REQUIRE(parse_valid("bitcOin:"));
    REQUIRE(parse_valid("BITCOIN:"));
}

TEST_CASE("uri reader parse negative scheme test", "[uri reader]") {
    REQUIRE( ! parse_valid("bitcorn:"));
}

TEST_CASE("uri reader parse positive empty name parameter test", "[uri reader]") {
    REQUIRE(parse_valid("bitcoin:?"));
    REQUIRE(parse_valid("bitcoin:?&"));
    REQUIRE(parse_valid("bitcoin:?=y"));
    REQUIRE(parse_valid("bitcoin:?="));
}

TEST_CASE("uri reader parse positive unknown optional parameter test", "[uri reader]") {
    REQUIRE(parse_valid("bitcoin:?x=y"));
    REQUIRE(parse_valid("bitcoin:?x="));
    REQUIRE(parse_valid("bitcoin:?x"));

    auto const uri = parse("bitcoin:?ignore=true");
    REQUIRE(uri.valid());
    REQUIRE(uri.address().empty());
    REQUIRE(uri.amount() == 0);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r().empty());
    REQUIRE(uri.parameter("ignore").empty());
}

TEST_CASE("uri reader parse negative unknown required parameter test", "[uri reader]") {
    auto const uri = parse("bitcoin:?req-ignore=false");
    REQUIRE( ! uri.valid());
}

TEST_CASE("uri reader parse address test", "[uri reader]") {
    auto const uri = parse("bitcoin:113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD");
    REQUIRE(uri.valid());
    REQUIRE((uri.payment() && uri.payment()->encoded_legacy() == "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD"));
    REQUIRE(uri.amount() == 0);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r().empty());
}

TEST_CASE("uri reader parse uri encoded address test", "[uri reader]") {
    auto const uri = parse("bitcoin:%3113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD");
    REQUIRE(uri.valid());
    REQUIRE((uri.payment() && uri.payment()->encoded_legacy() == "113Pfw4sFqN1T5kXUnKbqZHMJHN9oyjtgD"));
}

TEST_CASE("uri reader parse negative address test", "[uri reader]") {
    REQUIRE( ! parse_valid("bitcoin:&"));
    REQUIRE( ! parse_valid("bitcoin:19l88"));
    REQUIRE( ! parse_valid("bitcoin:19z88"));
}

TEST_CASE("uri reader parse amount only test", "[uri reader]") {
    auto const uri = parse("bitcoin:?amount=4.2");
    REQUIRE(uri.valid());
    REQUIRE( ! uri.payment());
    REQUIRE(uri.amount() == 420000000u);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r().empty());
}

TEST_CASE("uri reader parse minimal amount test", "[uri reader]") {
    auto const uri = parse("bitcoin:?amount=.");
    REQUIRE(uri.valid());
    REQUIRE(uri.amount() == 0);
}

TEST_CASE("uri reader parse invalid amount test", "[uri reader]") {
    REQUIRE( ! parse_valid("bitcoin:amount=4.2.1"));
    REQUIRE( ! parse_valid("bitcoin:amount=bob"));
}

TEST_CASE("uri reader parse label only test", "[uri reader]") {
    auto const uri = parse("bitcoin:?label=test");
    REQUIRE(uri.valid());
    REQUIRE( ! uri.payment());
    REQUIRE(uri.amount() == 0);
    REQUIRE(uri.label() == "test");
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r().empty());
}

TEST_CASE("uri reader parse reserved symbol with lowercase percent test", "[uri reader]") {
    auto const uri = parse("bitcoin:?label=%26%3d%6b");
    REQUIRE(uri.label() == "&=k");
}

TEST_CASE("uri reader parse negative percent encoding test", "[uri reader]") {
    REQUIRE( ! parse_valid("bitcoin:label=%3"));
    REQUIRE( ! parse_valid("bitcoin:label=%3G"));
}

TEST_CASE("uri reader parse encoded multibyte utf8 test", "[uri reader]") {
    auto const uri = parse("bitcoin:?label=%E3%83%95");
    REQUIRE(uri.label() == "フ");
}

TEST_CASE("uri reader parse non strict encoded multibyte utf8 with unencoded label space test", "[uri reader]") {
    auto const uri = parse("bitcoin:?label=Some テスト", false);
    REQUIRE(uri.label() == "Some テスト");
}

TEST_CASE("uri reader parse negative strict encoded multibyte utf8 with unencoded label space test", "[uri reader]") {
    REQUIRE( ! parse_valid("bitcoin:?label=Some テスト", true));
}

TEST_CASE("uri reader parse message only test", "[uri reader]") {
    auto const uri = parse("bitcoin:?message=Hi%20Alice");
    REQUIRE( ! uri.payment());
    REQUIRE(uri.amount() == 0);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message() == "Hi Alice");
    REQUIRE(uri.r().empty());
}

TEST_CASE("uri reader parse payment protocol only test", "[uri reader]") {
    auto const uri = parse("bitcoin:?r=http://www.example.com?purchase%3Dshoes");
    REQUIRE( ! uri.payment());
    REQUIRE(uri.amount() == 0);
    REQUIRE(uri.label().empty());
    REQUIRE(uri.message().empty());
    REQUIRE(uri.r() == "http://www.example.com?purchase=shoes");
}

TEST_CASE("uri reader parse custom reader optional parameter type test", "[uri reader]") {
    auto const custom = uri_reader::parse<custom_reader>("foo:part/abc?myparam1=1&myparam2=2#myfrag");
    REQUIRE(custom.is_valid());
    REQUIRE(custom.myscheme == "foo");
    REQUIRE(custom.mypath == "part/abc");
    REQUIRE(custom.myfragment);
    REQUIRE(custom.myfragment.value() == "myfrag");
    REQUIRE(custom.myparam1);
    REQUIRE(custom.myparam1.value() == "1");
    REQUIRE(custom.myparam2);
    REQUIRE(custom.myparam2.value() == "2");
}

TEST_CASE("uri reader parse custom reader unsupported component invalid", "[uri reader]") {
    REQUIRE( ! uri_reader::parse<custom_reader>("foo://bar:42/part/abc?myparam1=1&myparam2=2#myfrag").is_valid());
}

TEST_CASE("uri reader parse custom reader strict test", "[uri reader]") {
    REQUIRE( ! uri_reader::parse<custom_reader>("foo:?unknown=fail-when-strict").is_valid());
}

TEST_CASE("uri reader parse custom reader not strict test", "[uri reader]") {
    REQUIRE(uri_reader::parse<custom_reader>("foo:?unknown=not-fail-when-not-strict", false).is_valid());
}

// End Test Suite
