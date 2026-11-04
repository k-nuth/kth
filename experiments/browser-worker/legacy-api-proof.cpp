#include <kth/capi/domain/chain/header.h>
#include <kth/capi/hash.h>
#include <cstdio>
int main() {
    kth_hash_t zero{};
    auto header = kth_domain_chain_header_construct(4, &zero, &zero, 123, 456, 789);
    if (!header || kth_domain_chain_header_version(header)!=4 ||
        kth_domain_chain_header_timestamp(header)!=123 ||
        kth_domain_chain_header_bits(header)!=456 || kth_domain_chain_header_nonce(header)!=789) return 1;
    auto copy = kth_domain_chain_header_copy(header);
    if (!copy || !kth_domain_chain_header_equals(header,copy) ||
        !kth_hash_equal(kth_domain_chain_header_hash(header), kth_domain_chain_header_hash(copy))) return 2;
    kth_domain_chain_header_destruct(copy);
    kth_domain_chain_header_destruct(header);
    kth_header_mut_t invalid = nullptr;
    unsigned char short_wire[] = {1};
    auto error = kth_domain_chain_header_construct_from_data(short_wire,1,1,&invalid);
    if (error==kth_ec_success || invalid) return 3;
    std::puts("ordinary WASM C API construction/copy/hash/destruction/error control passed");
}
