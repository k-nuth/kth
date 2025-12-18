#include <print>

// Test meta target (includes everything) - show centralized version info
#include <kth/domain/version.hpp>

int main() {
    std::println("Testing KTH meta target (kth::kth)...");
    std::println("=== KTH Version Info ===");
    std::println("Version: {}", kth::version);
    std::println("Currency: {} ({})", kth::currency_name, kth::currency);
    std::println("Client: {}", kth::client_name);
    std::println("Meta target includes all core components!");
    return 0;
}
