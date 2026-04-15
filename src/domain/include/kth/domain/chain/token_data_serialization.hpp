// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

// The `to_data` entry points that used to live here moved into
// `token_data.hpp` so the C-API binding generator — which parses a
// single header per class — can pick them up. This header stays as
// a compatibility shim; external callers can keep including it and
// will transparently get the same overloads.

#include <kth/domain/chain/token_data.hpp>
