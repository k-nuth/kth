// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/endpoint.hpp>


#include <cstdint>
#include <iostream>
#include <regex>
#include <string>

#include <boost/lexical_cast.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/config/endpoint.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/asio.hpp>

namespace kth::infrastructure::config {

#if ! defined(__EMSCRIPTEN__)
using namespace boost::program_options;
#endif

endpoint::endpoint()
    : endpoint("localhost")
{}

endpoint::endpoint(endpoint const& x)
    : scheme_(x.scheme()), host_(x.host()), port_(x.port())
{}

endpoint::endpoint(std::string_view value) {
    std::stringstream(std::string(value)) >> *this;
}

endpoint::endpoint(authority const& authority)
    : endpoint(authority.to_string())
{}

endpoint::endpoint(std::string_view host, uint16_t port)
    : host_(host), port_(port)
{}

#if ! defined(__EMSCRIPTEN__)
endpoint::endpoint(asio::endpoint const& host)
    : endpoint(host.address(), host.port())
{}

endpoint::endpoint(asio::address const& ip, uint16_t port)
    : host_(ip.to_string()), port_(port)
{}
#endif

std::string const& endpoint::scheme() const {
    return scheme_;
}

std::string const& endpoint::host() const {
    return host_;
}

uint16_t endpoint::port() const {
    return port_;
}

std::string endpoint::to_string() const {
    std::stringstream value;
    value << *this;
    return value.str();
}

endpoint::operator bool const() const {
    // Return true if initialized.
    // TODO: this is a quick hack, along with http/https.
    return ! scheme_.empty();
}

bool endpoint::operator==(endpoint const& x) const {
    return host_ == x.host_ && port_ == x.port_ && scheme_ == x.scheme_;
}

std::istream& operator>>(std::istream& input, endpoint& argument) {
    std::string value;
    input >> value;

    // std::regex requires gcc 4.9, so we are using boost::regex for now.
    // Knuth: we use std::regex, becase we drop support por GCC<5
    static
    std::regex const regular(R"(^((tcp|udp|http|https|inproc):\/\/)?(\[([0-9a-f:\.]+)\]|([^:]+))(:([0-9]{1,5}))?$)");

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
    argument.scheme_ = match[2];
    argument.host_ = match[3];
    std::string port(match[7]);

    try {
        argument.port_ = port.empty() ? 0 : boost::lexical_cast<uint16_t>(port);
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

// std::ostream& operator<<(std::ostream& output, endpoint const& argument) {
//     if ( ! argument.scheme().empty()) {
//         output << argument.scheme() << "://";
//     }

//     output << argument.host();

//     if (argument.port() != 0) {
//         output << ":" << argument.port();
//     }

//     return output;
// }

} // namespace kth::infrastructure::config
