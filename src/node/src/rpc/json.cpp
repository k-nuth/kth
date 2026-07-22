// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/json.hpp>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace kth::node::rpc {

namespace {

// Copy the raw JSON text of an on-demand value into `out`. Returns false if the
// value could not be materialized.
bool copy_raw(simdjson::ondemand::value value, std::string& out) {
    std::string_view raw;
    if (value.raw_json().get(raw) != simdjson::SUCCESS) {
        return false;
    }
    out.assign(raw);
    return true;
}

} // namespace

std::expected<request, rpc_error> parse_request(std::string_view body) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(body);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        return std::unexpected(make_error(error_code::parse_error, "Parse error"));
    }

    // "method" is mandatory and must be a string.
    std::string_view method;
    if (doc["method"].get_string().get(method) != simdjson::SUCCESS) {
        return std::unexpected(
            make_error(error_code::invalid_request, "Missing or invalid 'method'"));
    }

    request req;
    req.method.assign(method);

    // "params" is optional (any JSON type) — kept as raw text for the handler.
    simdjson::ondemand::value params;
    if (doc["params"].get(params) == simdjson::SUCCESS) {
        copy_raw(params, req.params);
    }

    // "id" is optional; echoed back verbatim. Absent => "null" (default).
    simdjson::ondemand::value id;
    if (doc["id"].get(id) == simdjson::SUCCESS) {
        copy_raw(id, req.id);
    }

    return req;
}

std::vector<std::string> params_strings(std::string_view params_json) {
    std::vector<std::string> out;
    if (params_json.empty()) {
        return out;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(params_json);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        return out;
    }

    simdjson::ondemand::array arr;
    if (doc.get_array().get(arr) != simdjson::SUCCESS) {
        return out;
    }

    for (auto element : arr) {
        simdjson::ondemand::value value;
        std::string_view text;
        if (element.get(value) == simdjson::SUCCESS &&
            value.get_string().get(text) == simdjson::SUCCESS) {
            out.emplace_back(text);
        } else {
            out.emplace_back();
        }
    }
    return out;
}

std::optional<std::uint64_t> params_uint(std::string_view params_json, std::size_t index) {
    if (params_json.empty()) {
        return std::nullopt;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(params_json);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        return std::nullopt;
    }

    simdjson::ondemand::array arr;
    if (doc.get_array().get(arr) != simdjson::SUCCESS) {
        return std::nullopt;
    }

    std::size_t i = 0;
    for (auto element : arr) {
        if (i != index) {
            ++i;
            continue;
        }
        simdjson::ondemand::value value;
        std::string_view raw;
        if (element.get(value) != simdjson::SUCCESS ||
            value.raw_json().get(raw) != simdjson::SUCCESS) {
            return std::nullopt;
        }
        // Accept a bare number ("0") or a quoted numeric string ("\"0\"").
        std::string_view text = raw;
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
            text = text.substr(1, text.size() - 2);
        }
        std::uint64_t out = 0;
        auto const [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        if (ec == std::errc{} && ptr == text.data() + text.size()) {
            return out;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string build_success(std::string_view id_raw, std::string_view result_raw) {
    writer w;
    w.begin_object();
    w.field_raw("result", result_raw.empty() ? "null" : result_raw);
    w.key("error").value_null();
    w.field_raw("id", id_raw.empty() ? "null" : id_raw);
    w.end_object();
    return w.str();
}

std::string build_error(std::string_view id_raw, int code, std::string_view message) {
    writer w;
    w.begin_object();
    w.key("result").value_null();
    w.key("error").begin_object();
    w.field("code", static_cast<std::int64_t>(code));
    w.field("message", message);
    w.end_object();
    w.field_raw("id", id_raw.empty() ? "null" : id_raw);
    w.end_object();
    return w.str();
}

// ---- writer ---------------------------------------------------------------

writer::writer() = default;

void writer::pre_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if ( ! first_.empty()) {
        if ( ! first_.back()) {
            b_.append_comma();
        }
        first_.back() = false;
    }
}

writer& writer::begin_object() {
    pre_value();
    b_.start_object();
    first_.push_back(true);
    return *this;
}

writer& writer::end_object() {
    b_.end_object();
    if ( ! first_.empty()) {
        first_.pop_back();
    }
    return *this;
}

writer& writer::begin_array() {
    pre_value();
    b_.start_array();
    first_.push_back(true);
    return *this;
}

writer& writer::end_array() {
    b_.end_array();
    if ( ! first_.empty()) {
        first_.pop_back();
    }
    return *this;
}

writer& writer::key(std::string_view k) {
    if ( ! first_.empty()) {
        if ( ! first_.back()) {
            b_.append_comma();
        }
        first_.back() = false;
    }
    b_.escape_and_append_with_quotes(k);
    b_.append_colon();
    after_key_ = true;
    return *this;
}

writer& writer::value(std::string_view s) {
    pre_value();
    b_.escape_and_append_with_quotes(s);
    return *this;
}

writer& writer::value(char const* s) {
    return value(std::string_view(s));
}

writer& writer::value(bool b) {
    pre_value();
    b_.append_raw(b ? "true" : "false");
    return *this;
}

writer& writer::value(std::int64_t n) {
    pre_value();
    b_.append(n);
    return *this;
}

writer& writer::value(std::uint64_t n) {
    pre_value();
    b_.append(n);
    return *this;
}

writer& writer::value(double d) {
    pre_value();
    b_.append(d);
    return *this;
}

writer& writer::value_raw(std::string_view raw_json) {
    pre_value();
    b_.append_raw(raw_json);
    return *this;
}

writer& writer::value_null() {
    pre_value();
    b_.append_raw("null");
    return *this;
}

std::string writer::str() {
    std::string_view sv;
    if (b_.view().get(sv) != simdjson::SUCCESS) {
        return "null";
    }
    return std::string(sv);
}

} // namespace kth::node::rpc
