#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Refuse raw Asio timers in code a browser node compiles.

Nothing in the browser Worker runtime wakes for an Asio timer: one that is
waited on there never fires, and whatever waits on it hangs without an error.
Shared code waits through `kth::steady_timer` (platform_timer.hpp) or a
channel selection's `after()` (which uses the runtime's timer, asio
1.38.3-kth.3). The uses below are the known exceptions, each never compiled
or never reached in a browser node.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# What a browser node compiles. The node and the C-API add theirs when a browser
# node compiles them.
SOURCES = ("src/infrastructure", "src/domain", "src/database", "src/blockchain",
           "src/network")
PATTERN = re.compile(
    # A timer object; its nested types (::time_point, ::clock_type) are only types.
    r"(?<![\w:])(?:::)?asio::(?:steady_timer|system_timer|high_resolution_timer|basic_waitable_timer)\b(?!::)"
    r"|\bcancel_after\s*\(|\bcancel_at\s*\(")
# (file, text on the line): each never compiled, or never reached, in a browser node.
ALLOWED = {
    # The non-browser task group; the browser one is browser_task_group.hpp.
    ("src/infrastructure/include/kth/infrastructure/utility/task_group.hpp", "::asio::steady_timer timer(executor);"),
    # The native alias the browser one replaces.
    ("src/infrastructure/include/kth/infrastructure/utility/platform_timer.hpp", "namespace kth { using steady_timer = ::asio::steady_timer; }"),
    ("src/infrastructure/include/kth/infrastructure/utility/asio.hpp", "using timer = ::asio::basic_waitable_timer<steady_clock>;"),
    # Native branches of #if defined(KTH_BROWSER_NODE) in peer_session.cpp.
    ("src/network/src/peer_session.cpp", "::asio::cancel_at(deadline, ::asio::as_tuple(::asio::use_awaitable)));"),
    ("src/network/src/peer_session.cpp", "::asio::cancel_after(timeout, ::asio::as_tuple(::asio::use_awaitable)));"),
}


def violations(root=ROOT):
    found = []
    for base in SOURCES:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp", ".ipp", ".h") or "/test/" in path.as_posix():
                continue
            relative = path.relative_to(root).as_posix()
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                code = line.split("//", 1)[0]
                if "#include" in code or not PATTERN.search(code):
                    continue
                if (relative, line.strip()) in ALLOWED:
                    continue
                found.append(f"{relative}:{number}: {line.strip()}")
    return found


def main():
    found = violations()
    for item in found:
        print(f"raw Asio timer in code a browser node compiles: {item}")
    if found:
        print("Use kth::steady_timer (kth/infrastructure/utility/platform_timer.hpp) "
              "or a selection's after(); or add a reasoned exception to ALLOWED.")
        return 1
    print("no raw Asio timers outside the known native exceptions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
