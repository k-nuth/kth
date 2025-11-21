#include <print>

// Test meta target (includes everything) - show versions from multiple modules
#include <kth/infrastructure/version.hpp>
#include <kth/domain/version.hpp>
#include <kth/consensus/version.hpp>
#include <kth/blockchain/version.hpp>
#include <kth/node/version.hpp>

int main() {
    std::println("Testing KTH meta target (kth::kth)...");
    std::println("=== KTH Component Versions ===");
    std::println("Infrastructure: {}", kth::infrastructure::version());
    std::println("Domain: {}", kth::domain::version());
    // Note: Not all modules may have version functions implemented
    std::println("Blockchain: {}", kth::blockchain::version());
    std::println("Node: {}", kth::node::version());
    std::println("Meta target includes all core components!");
    return 0;
}
