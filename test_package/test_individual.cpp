#include <print>

// Test individual targets - include version headers from each module
#include <kth/infrastructure/version.hpp>
#include <kth/domain/version.hpp>
#include <secp256k1.h>

int main() {
    std::println("Testing individual KTH targets...");
    std::println("Infrastructure version: {}", kth::infrastructure::version());
    std::println("Domain version: {}", kth::domain::version());
    std::println("Secp256k1: Successfully included main header");
    std::println("Successfully linked kth::infrastructure, kth::domain, and kth::secp256k1");
    std::println("All individual targets are working correctly!");
    return 0;
}
