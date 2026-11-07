#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Converts the transaction vectors of Bitcoin Cash Node (tx_valid.json,
# tx_invalid.json; see SOURCE.json for the exact upstream commit) into a C++
# table: serialized transaction, position in the JSON array, the comment
# section the vector sits under, and its verify flags. Prevout/script columns
# are not kept: this table feeds only the checks that do not run scripts.
#   generate_tx_vectors.py <output.hpp> <tx_valid.json> <tx_invalid.json>

import json
import sys


def emit(name, path, lines):
    with open(path, "rt", encoding="utf8") as f:
        data = json.load(f)
    lines.append("inline std::vector<vector_t> const& %s() {" % name)
    lines.append("    static std::vector<vector_t> const v = {")
    comment = ""
    for i, e in enumerate(data):
        if len(e) == 1 and isinstance(e[0], str):
            comment = e[0]
            continue
        assert len(e) == 3 and isinstance(e[1], str) and isinstance(e[2], str)
        lines.append("        {%d, %s, %s, %s, %d}," % (i, json.dumps(comment), json.dumps(e[1]), json.dumps(e[2]), len(e[0])))
    lines.append("    };")
    lines.append("    return v;")
    lines.append("}")
    lines.append("")


def main():
    out, valid, invalid = sys.argv[1:4]
    lines = [
        "// Auto-generated from test/scripts/bchn_tx_vectors/data - DO NOT EDIT",
        "// Source: Bitcoin Cash Node src/test/data/tx_valid.json and tx_invalid.json (MIT, The Bitcoin developers)",
        "// See test/scripts/bchn_tx_vectors/SOURCE.json for the exact upstream commit.",
        "",
        "#pragma once",
        "",
        "#include <vector>",
        "",
        "namespace bchn_tx_vectors {",
        "",
        "struct vector_t {",
        "    int json_index;          // position in the JSON array (comments count)",
        "    char const* comment;     // last single-string entry before the vector",
        "    char const* tx_hex;",
        "    char const* flags;",
        "    int num_inputs;",
        "};",
        "",
    ]
    emit("tx_valid", valid, lines)
    emit("tx_invalid", invalid, lines)
    lines.append("} // namespace bchn_tx_vectors")
    with open(out, "wt", encoding="utf8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
