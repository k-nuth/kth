// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TRACK_HPP
#define KTH_INFRASTRUCTURE_TRACK_HPP

#include <atomic>
#include <cstddef>
#include <string>

// Defines the log and tracking but does not use them.
// These are defined in kth so that they can be used in network and blockchain.

#define CONSTRUCT_TRACK(class_name) \
    track<class_name>(#class_name)

template <typename Shared>
class track
{
public:
    static std::atomic<size_t> instances;

protected:
    track(std::string const& class_name);
    ~track();

private:
    std::string const class_;
};

#include <kth/infrastructure/impl/utility/track.ipp>

#endif
