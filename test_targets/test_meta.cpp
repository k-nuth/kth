#include <iostream>

// Test meta target (includes everything) - show versions from multiple modules
#include <kth/blockchain/version.hpp>
#include <kth/consensus/version.hpp>
#include <kth/domain/version.hpp>
#include <kth/infrastructure/version.hpp>
#include <kth/node/version.hpp>

int main() {
    std::cout << "Testing KTH meta target (kth::kth)..." << std::endl;
    std::cout << "=== KTH Component Versions ===" << std::endl;
    std::cout << "Infrastructure: " << kth::infrastructure::version() << std::endl;
    std::cout << "Domain: " << kth::domain::version() << std::endl;
    // Note: Not all modules may have version functions implemented
    std::cout << "Blockchain: " << kth::blockchain::version() << std::endl;
    std::cout << "Node: " << kth::node::version() << std::endl;
    std::cout << "Meta target includes all core components!" << std::endl;
    return 0;
}
