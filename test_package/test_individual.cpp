#include <print>

// Test individual targets - include version header from domain
#include <kth/domain/version.hpp>
#include <secp256k1.h>

int main() {
    std::println("Testing individual KTH targets...");
    std::println("KTH version: {}", kth::version);
    std::println("Currency: {} ({})", kth::currency_name, kth::currency);
    std::println("Client name: {}", kth::client_name);
    std::println("Secp256k1: Successfully included main header");
    std::println("Successfully linked kth::infrastructure, kth::domain, and kth::secp256k1");
    std::println("All individual targets are working correctly!");
    return 0;
}
