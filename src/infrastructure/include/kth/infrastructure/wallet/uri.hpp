// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//TODO(fernando): use Boost.URL

#ifndef KTH_INFRASTUCTURE_WALLET_URI_HPP
#define KTH_INFRASTUCTURE_WALLET_URI_HPP

#include <map>
#include <string>

#include <kth/infrastructure/define.hpp>

namespace kth::infrastructure::wallet {

/**
 * A parsed URI according to RFC 3986.
 */
struct KI_API uri {
    /**
     * Decodes a URI from a string.
     * @param strict Set to false to tolerate unescaped special characters.
     */
    bool decode(std::string const& encoded, bool strict=true);
    std::string encoded() const;

    /**
     * Returns the lowercased URI scheme.
     */
    std::string scheme() const;
    void set_scheme(std::string const& scheme);

    /**
     * Obtains the unescaped authority part, if any (user@server:port).
     */
    std::string authority() const;
    bool has_authority() const;
    void set_authority(std::string const& authority);
    void remove_authority();

    /**
     * Obtains the unescaped path part.
     */
    std::string path() const;
    void set_path(std::string const& path);

    /**
     * Returns the unescaped query string, if any.
     */
    std::string query() const;
    bool has_query() const;
    void set_query(std::string const& query);
    void remove_query();

    /**
     * Returns the unescaped fragment string, if any.
     */
    std::string fragment() const;
    bool has_fragment() const;
    void set_fragment(std::string const& fragment);
    void remove_fragment();

    using query_map = std::map<std::string, std::string>;

    /**
     * Interprets the query string as a sequence of key-value pairs.
     * All query strings are valid, so this function cannot fail.
     * The results are unescaped. Both keys and values can be zero-length,
     * and if the same key is appears multiple times, the final one wins.
     */
    query_map decode_query() const;
    void encode_query(const query_map& map);

private:
    // All parts are stored with their original escaping:
    std::string scheme_;
    std::string authority_;
    std::string path_;
    std::string query_;
    std::string fragment_;

    bool has_authority_ = false;
    bool has_query_ = false;
    bool has_fragment_ = false;
};

} // namespace kth::infrastructure::wallet

#endif
