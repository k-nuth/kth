#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Converts the CashTokens prefix vectors of Bitcoin Cash Node (see SOURCE.json
# for the exact upstream commit) into a C++ table.
#   generate_token_vectors.py <output.hpp> [--exclude-valid=i,j] [--exclude-invalid=i,j] <valid.json> <invalid.json>

import json
import sys


def main():
    out = sys.argv[1]
    ex_valid, ex_invalid, paths = set(), set(), []
    for a in sys.argv[2:]:
        if a.startswith("--exclude-valid="):
            ex_valid = {int(x) for x in a.split("=", 1)[1].split(",") if x}
        elif a.startswith("--exclude-invalid="):
            ex_invalid = {int(x) for x in a.split("=", 1)[1].split(",") if x}
        else:
            paths.append(a)
    valid_path, invalid_path = paths
    valid = json.load(open(valid_path, encoding="utf8"))
    invalid = json.load(open(invalid_path, encoding="utf8"))
    L = [
        "// Auto-generated from test/scripts/bchn_token_vectors/data - DO NOT EDIT",
        "// Source: Bitcoin Cash Node src/test/data/token_tests_prefix_{valid,invalid}.json (MIT, The Bitcoin developers)",
        "// See test/scripts/bchn_token_vectors/SOURCE.json for the exact upstream commit.",
        "",
        "#pragma once",
        "",
        "#include <vector>",
        "",
        "namespace bchn_token_vectors {",
        "",
        "struct valid_t {",
        "    int index;",
        "    char const* prefix;",
        "    char const* category;     // big endian hex, as in the JSON",
        "    char const* amount;       // decimal string",
        "    bool has_nft;",
        "    char const* capability;   // none | mutable | minting (only when has_nft)",
        "    char const* commitment;   // hex (only when has_nft)",
        "};",
        "struct invalid_t {",
        "    int index;",
        "    char const* prefix;",
        "    char const* error;",
        "    char const* bchn_exception_message;",
        "};",
        "",
        "inline std::vector<valid_t> const& valid() {",
        "    static std::vector<valid_t> const v = {",
    ]
    for i, e in enumerate(valid):
        if i in ex_valid:
            continue
        d = e["data"]
        nft = d.get("nft")
        L.append("        {%d, %s, %s, %s, %s, %s, %s}," % (
            i, json.dumps(e["prefix"]), json.dumps(d["category"]), json.dumps(str(d.get("amount", "0"))),
            "true" if nft is not None else "false",
            json.dumps(nft.get("capability", "none") if nft else ""),
            json.dumps(nft.get("commitment", "") if nft else "")))
    L += ["    };", "    return v;", "}", "", "inline std::vector<invalid_t> const& invalid() {", "    static std::vector<invalid_t> const v = {"]
    for i, e in enumerate(invalid):
        if i in ex_invalid:
            continue
        L.append("        {%d, %s, %s, %s}," % (i, json.dumps(e["prefix"]), json.dumps(e["error"]), json.dumps(e["bchn_exception_message"])))
    L += ["    };", "    return v;", "}", "", "} // namespace bchn_token_vectors"]
    open(out, "wt", encoding="utf8").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
