#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Converts the ABLA test vectors of Bitcoin Cash Node (see SOURCE.json for the
# exact upstream commit, MIT license, copyright The Bitcoin developers) into
# a C++ table. Usage:
#   generate_abla_test_vectors.py <output.hpp> [--exclude=<file.json>,...] <data/*.json...>

import json
import os
import sys


def u64(v):
    return "%sULL" % int(v)


def main():
    out_path = sys.argv[1]
    exclude = set()
    files = []
    for arg in sys.argv[2:]:
        if arg.startswith("--exclude="):
            exclude.update(arg[len("--exclude="):].split(","))
        else:
            files.append(arg)
    files = sorted(f for f in files if os.path.basename(f) not in exclude)
    lines = []
    lines.append("// Auto-generated from test/scripts/abla_test_vectors/data - DO NOT EDIT")
    lines.append("// Source: Bitcoin Cash Node src/test/data/abla_test_vectors (MIT, Copyright (c) 2023 The Bitcoin developers)")
    lines.append("// See test/scripts/abla_test_vectors/SOURCE.json for the exact upstream commit.")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("#include <vector>")
    lines.append("")
    lines.append("namespace abla_vectors {")
    lines.append("")
    lines.append("struct state_t { uint64_t n; uint64_t epsilon; uint64_t beta; };")
    lines.append("struct step_t { uint64_t blocksize; state_t next_state; uint64_t next_limit; };")
    lines.append("struct lookahead_t { uint64_t lookahead; state_t state; uint64_t limit; };")
    lines.append("struct config_t { uint64_t epsilon0, beta0, n0, zeta, gamma_reciprocal, delta, theta_reciprocal; bool disable_2gb_limit; };")
    lines.append("struct vector_t {")
    lines.append("    char const* file;")
    lines.append("    char const* name;")
    lines.append("    char const* description;")
    lines.append("    config_t config;")
    lines.append("    state_t initial_state;")
    lines.append("    uint64_t initial_limit;")
    lines.append("    bool is_lookahead;")
    lines.append("    lookahead_t lookahead;")
    lines.append("    std::vector<step_t> steps;")
    lines.append("};")
    lines.append("")
    lines.append("inline std::vector<vector_t> const& all() {")
    lines.append("    static std::vector<vector_t> const v = {")
    for path in files:
        fname = os.path.basename(path)
        with open(path, "rt", encoding="utf8") as f:
            t = json.load(f)
        name = t.get("testName", fname.rsplit(".", 1)[0])
        desc = t.get("testDescription", "")
        c = t["ABLAConfig"]
        opts = c.get("options", "")
        s = t["ABLAStateInitial"]
        tv = t["testVector"]
        is_la = len(tv) == 1 and "lookahead" in tv[0]
        la = "{0, {0, 0, 0}, 0}"
        steps = []
        if is_la:
            e = tv[0]
            ls = e["ABLAStateForLookaheadBlock"]
            la = "{%s, {%s, %s, %s}, %s}" % (u64(e["lookahead"]), u64(ls["n"]), u64(ls["epsilon"]), u64(ls["beta"]), u64(e["blocksizeLimitForLookaheadBlock"]))
        else:
            for e in tv:
                ns = e["ABLAStateForNextBlock"]
                steps.append("{%s, {%s, %s, %s}, %s}" % (u64(e["blocksize"]), u64(ns["n"]), u64(ns["epsilon"]), u64(ns["beta"]), u64(e["blocksizeLimitForNextBlock"])))
        lines.append("        {")
        lines.append("            %s, %s, %s," % (json.dumps(fname), json.dumps(name), json.dumps(desc)))
        lines.append("            {%s, %s, %s, %s, %s, %s, %s, %s}," % (u64(c["epsilon0"]), u64(c["beta0"]), u64(c["n0"]), u64(c["zeta"]), u64(c["gammaReciprocal"]), u64(c["delta"]), u64(c["thetaReciprocal"]), "true" if "-disable2GBLimit" in opts else "false"))
        lines.append("            {%s, %s, %s}, %s, %s, %s," % (u64(s["n"]), u64(s["epsilon"]), u64(s["beta"]), u64(t["blocksizeLimitInitial"]), "true" if is_la else "false", la))
        lines.append("            {")
        for i in range(0, len(steps), 3):
            lines.append("                " + ", ".join(steps[i:i+3]) + ",")
        lines.append("            }")
        lines.append("        },")
    lines.append("    };")
    lines.append("    return v;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace abla_vectors")
    with open(out_path, "wt", encoding="utf8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
