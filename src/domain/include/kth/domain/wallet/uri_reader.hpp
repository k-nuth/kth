// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


//TODO(fernando): use Boost.URL

#ifndef KTH_WALLET_URI_READER_HPP
#define KTH_WALLET_URI_READER_HPP

#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/wallet/uri.hpp>

namespace kth::domain::wallet {

using namespace kth::infrastructure::wallet;

/**
 * Interface for URI deserialization.
 * The URI parser calls these methods as it extracts each URI component.
 * A false return from any setter is expected to terminate the parser.
 */
struct KD_API uri_reader {
    /**
     * Parses any URI string into its individual components.
     * @param[in]  uri     The URI to parse.
     * @param[in]  strict  Only accept properly-escaped parameters.
     * @return The parsed URI or a default instance if the `uri` is malformed
     * according to the  `UriReader`.
     */
    template <class UriReader>
    static
    UriReader parse(std::string const& uri, bool strict = true) {
        wallet::uri parsed;
        if ( ! parsed.decode(uri, strict)) {
            return UriReader();
        }

        UriReader out;
        out.set_strict(strict);
        out.set_scheme(parsed.scheme());
        if (parsed.has_authority() && !out.set_authority(parsed.authority())) {
            return UriReader();
        }

        if ( ! parsed.path().empty() && !out.set_path(parsed.path())) {
            return UriReader();
        }

        if (parsed.has_fragment() && !out.set_fragment(parsed.fragment())) {
            return UriReader();
        }

        auto const query = parsed.decode_query();
        for (auto const& term : query) {
            auto const& key = term.first;
            auto const& value = term.second;
            if ( ! key.empty() && !out.set_parameter(key, value)) {
                return UriReader();
            }
        }

        return out;
    }

    /// uri_reader interface.
    virtual void set_strict(bool strict) = 0;
    virtual bool set_scheme(std::string const& scheme) = 0;
    virtual bool set_authority(std::string const& authority) = 0;
    virtual bool set_path(std::string const& path) = 0;
    virtual bool set_fragment(std::string const& fragment) = 0;
    virtual bool set_parameter(std::string const& key, std::string const& value) = 0;
};

} // namespace kth::domain::wallet

#endif
