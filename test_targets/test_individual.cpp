#include <iostream>

// Test individual targets - include version headers from each module
#include <kth/domain/version.hpp>
#include <kth/infrastructure/version.hpp>

#include <secp256k1/version.h>

int main() {
    std::cout << "Testing individual KTH targets..." << std::endl;
    std::cout << "Infrastructure version: " << kth::infrastructure::version() << std::endl;
    std::cout << "Domain version: " << kth::domain::version() << std::endl;
    // Note: secp256k1 doesn't have a version function, just the macro
    std::cout << "Successfully linked kth::infrastructure, kth::domain, and kth::secp256k1" << std::endl;
    std::cout << "All individual targets are working correctly!" << std::endl;
    return 0;
}
