#include <iostream>
#include <print>
#include <kth/domain/wallet/payment_address.hpp>

int main() {
    using kth::domain::wallet::payment_address;

    std::println("{}", "start");
    payment_address const address("bitcoincash:pvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu69reyzmqx");
    std::println("{}", "address created");
    std::println("{}", "address is valid: " << bool(address));
    std::println("{}", "address encoded cashaddr:      " << address.encoded_cashaddr(false));
    std::println("{}", "address encoded token address: " << address.encoded_cashaddr(true));
    std::println("{}", "address encoded legacy:        " << address.encoded_legacy());

    return 0;
}
