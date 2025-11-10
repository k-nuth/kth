// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/checkpoint.hpp>

#include <charconv>
#include <cstddef>
#include <iostream>
#include <regex>
#include <string>

#include <boost/lexical_cast.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

// using namespace boost;
#if ! defined(__EMSCRIPTEN__)
using namespace boost::program_options;
#endif

// checkpoint::checkpoint()
//     : hash_(kth::null_hash)
// {}

checkpoint::checkpoint(std::string_view value)
    : checkpoint()
{
    std::stringstream(std::string(value)) >> *this;
}

// checkpoint::checkpoint(checkpoint const& x)
//     : hash_(x.hash()), height_(x.height())
// {}

// This is intended for static initialization (i.e. of the internal defaults).
checkpoint::checkpoint(std::string_view hash, size_t height)
    : height_(height)
{
    if ( ! decode_hash(hash_, hash)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(std::string(hash)));
#else
        throw std::invalid_argument(std::string(hash));
#endif
    }
}

checkpoint::checkpoint(hash_digest const& hash, size_t height)
    : hash_(hash), height_(height)
{}

hash_digest const& checkpoint::hash() const {
    return hash_;
}

size_t const checkpoint::height() const {
    return height_;
}

std::string checkpoint::to_string() const {
    std::stringstream value;
    value << *this;
    return value.str();
}

infrastructure::config::checkpoint::list checkpoint::sort(list const& checks) {
    auto const comparitor = [](checkpoint const& left, checkpoint const& right) {
        return left.height() < right.height();
    };

    auto copy = checks;
    std::sort(copy.begin(), copy.end(), comparitor);
    return copy;
}

bool checkpoint::covered(size_t height, list const& checks) {
    return !checks.empty() && height <= checks.back().height();
}

bool checkpoint::validate(hash_digest const& hash, size_t height, list const& checks) {
    auto const match_invalid = [&height, &hash](const infrastructure::config::checkpoint& item) {
        return height == item.height() && hash != item.hash();
    };

    auto const it = std::find_if(checks.begin(), checks.end(), match_invalid);
    return it == checks.end();
}

// bool checkpoint::operator==(checkpoint const& x) const {
//     return height_ == x.height_ && hash_ == x.hash_;
// }

std::istream& operator>>(std::istream& input, checkpoint& argument) {
    std::string value;
    input >> value;

    static
    std::regex const regular("^([0-9a-f]{64})(:([0-9]{1,20}))?$");

    std::sregex_iterator it(value.begin(), value.end(), regular), end;
    if (it == end) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
        throw std::invalid_argument(value);
#endif
    }

    auto const& match = *it;
    if ( ! decode_hash(argument.hash_, match[1].str())) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
        throw std::invalid_argument(value);
#endif
    }

    try {
        argument.height_ = boost::lexical_cast<size_t>(match[3]);
    } catch (...) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
        throw std::invalid_argument(value);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, checkpoint const& argument) {
    output << encode_hash(argument.hash()) << ":" << argument.height();
    return output;
}

} // namespace kth::infrastructure::config
