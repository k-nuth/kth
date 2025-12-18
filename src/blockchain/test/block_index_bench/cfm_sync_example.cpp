// Simplified example for Boost.Unordered Slack discussion
// Question: Can we leverage concurrent_flat_map's synchronization
// to protect an associated external structure?

#include <boost/unordered/concurrent_flat_map.hpp>
#include <string>
#include <vector>
#include <cstdint>

// The data we want to store (simplified block metadata)
struct BlockData {
    uint32_t timestamp;
    uint32_t parent_index;  // Index of parent in the same vector
    uint64_t chain_work;
};

// What we currently have to do (two separate synchronization mechanisms):
class CurrentApproach {
    boost::concurrent_flat_map<std::string, uint32_t> hash_to_index_;
    std::vector<BlockData> blocks_;
    std::atomic<uint32_t> size_{0};  // For lock-free reads of existing elements

public:
    // Insert: need to coordinate both structures
    uint32_t insert(std::string const& hash, BlockData const& data) {
        uint32_t idx = size_.load(std::memory_order_relaxed);

        // Problem: These two operations are not atomic together
        // 1. Insert into vector
        blocks_.push_back(data);

        // 2. Insert into map (this has its own internal synchronization)
        hash_to_index_.insert({hash, idx});

        // 3. Publish size (readers use this to know valid range)
        size_.store(idx + 1, std::memory_order_release);

        return idx;
    }

    // Read by hash: need to go through the map first
    BlockData const* find(std::string const& hash) const {
        BlockData const* result = nullptr;
        hash_to_index_.visit(hash, [&](auto const& pair) {
            uint32_t idx = pair.second;
            if (idx < size_.load(std::memory_order_acquire)) {
                result = &blocks_[idx];
            }
        });
        return result;
    }

    // Traverse by index: direct vector access (lock-free for existing elements)
    BlockData const& get(uint32_t idx) const {
        return blocks_[idx];  // Valid if idx < size_.load(acquire)
    }
};

// What we would LIKE to do (hypothetical API):
class DesiredApproach {
    // Hypothetical: A way to associate external storage with the map's buckets
    // so that insertions into both are protected by the same synchronization

    // Option A: Custom allocator/storage that the map could use?
    // Option B: Hook into the map's bucket locking mechanism?
    // Option C: A "parallel array" feature in concurrent_flat_map?

    // Pseudocode of desired behavior:
    /*
    boost::concurrent_flat_map_with_storage<
        std::string,           // Key
        uint32_t,              // Value (index)
        BlockData              // Associated storage type
    > map_with_storage_;

    // Single atomic operation that:
    // 1. Allocates space in associated storage
    // 2. Inserts key->index mapping
    // 3. Returns reference to storage for initialization
    auto [idx, storage_ref] = map_with_storage_.insert_with_storage(hash);
    storage_ref = BlockData{...};

    // Or alternatively, insert both atomically:
    map_with_storage_.insert(hash, BlockData{...});
    // Internally: acquires bucket lock, appends to storage, inserts index, releases lock
    */
};

// The core question:
// Is there a way to use concurrent_flat_map's internal synchronization
// (the bucket-level locking used by visit/insert) to also protect
// writes to an associated vector<T>?
//
// Use case: We have a block index store where:
// - hash_to_index_ maps block_hash -> index (for O(1) lookup)
// - blocks_ vector stores the actual data (for O(1) traversal by index)
// - Reads of existing elements must be lock-free
// - Only writes need synchronization
//
// Currently we use atomic<size> + memory ordering, which works but
// feels like we're duplicating synchronization that the map already does.

int main() {
    CurrentApproach store;
    store.insert("genesis", {0, UINT32_MAX, 0});
    store.insert("block1", {100, 0, 1});
    store.insert("block2", {200, 1, 2});

    auto* block = store.find("block2");
    if (block) {
        // Use block->timestamp, etc.
    }

    return 0;
}
