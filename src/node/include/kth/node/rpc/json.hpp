// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_JSON_HPP
#define KTH_NODE_RPC_JSON_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include <kth/node/rpc/error.hpp>

namespace kth::node::rpc {

// A parsed JSON-RPC request. `params` and `id` hold the raw JSON text of those
// members (owned copies) so handlers can re-parse params on demand and the
// dispatcher can echo the id back verbatim (number, string, or null).
struct request {
    std::string method;
    std::string params;         // raw JSON of "params" ("" when absent)
    std::string id = "null";    // raw JSON of "id"     ("null" when absent)
};

// Parse a JSON-RPC request body. On failure returns the rpc_error to report:
// parse_error for malformed JSON, invalid_request when "method" is missing.
std::expected<request, rpc_error> parse_request(std::string_view body);

// Positional params (a JSON array) decoded as strings. A missing/empty params,
// a non-array, or a non-string element each yields an empty string in that slot;
// handlers validate arity and emptiness themselves.
std::vector<std::string> params_strings(std::string_view params_json);

// Build the response envelopes. `result_raw` / `id_raw` are already-serialized
// JSON fragments and are emitted verbatim.
std::string build_success(std::string_view id_raw, std::string_view result_raw);
std::string build_error(std::string_view id_raw, int code, std::string_view message);

// Ergonomic wrapper over simdjson's low-level string_builder. It owns comma and
// colon placement plus object/array nesting, so a handler only describes the
// shape of its result. All RPC result serialization goes through this (simdjson).
struct writer {
    writer();

    writer& begin_object();
    writer& end_object();
    writer& begin_array();
    writer& end_array();

    // Object key. The next value() call supplies its value.
    writer& key(std::string_view k);

    // Scalar values (quoted+escaped for strings; verbatim for value_raw).
    writer& value(std::string_view s);
    writer& value(char const* s);
    writer& value(bool b);
    writer& value(std::int64_t n);
    writer& value(std::uint64_t n);
    writer& value(double d);
    writer& value_raw(std::string_view raw_json);
    writer& value_null();

    // key + value in one call.
    template <typename T>
    writer& field(std::string_view k, T const& v) {
        key(k);
        value(v);
        return *this;
    }
    writer& field_raw(std::string_view k, std::string_view raw_json) {
        key(k);
        value_raw(raw_json);
        return *this;
    }

    // Finalize to an owned JSON string.
    std::string str();

private:
    void pre_value();

    simdjson::builder::string_builder b_;
    std::vector<bool> first_;   // per nesting level: is the next element the first?
    bool after_key_ = false;    // a key() was just written; value follows a colon
};

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_JSON_HPP
