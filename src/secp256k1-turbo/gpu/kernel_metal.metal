// Metal entry point appended to kernel_core.h at build time (gpu/embed.cmake).
kernel void verify_kernel(device const gpu_raw_input* in [[buffer(0)]],
                          device uchar* out [[buffer(1)]],
                          GPU_GTAB g [[buffer(2)]],
                          constant u32& n [[buffer(3)]],
                          constant u32& schnorr [[buffer(4)]],
                          uint i [[thread_position_in_grid]]) {
    if (i >= n) return;
    out[i] = (uchar)gpu_verify_one(in + i, g, schnorr);
}

kernel void field_test_kernel(device const gpu_field_test_in* in [[buffer(0)]],
                              device gpu_field_test_out* out [[buffer(1)]],
                              constant u32& n [[buffer(2)]],
                              uint i [[thread_position_in_grid]]) {
    if (i >= n) return;
    gpu_field_test_one(in + i, out + i);
}

// Double SHA-256 of n 64-byte inputs: 16 words in, 8 words out, each.
kernel void sha256d64_kernel(device const u32* in [[buffer(0)]],
                             device u32* out [[buffer(1)]],
                             constant u32& n [[buffer(2)]],
                             uint i [[thread_position_in_grid]]) {
    if (i >= n) return;
    gpu_sha256d_pair(in + 16 * i, in + 16 * i + 8, out + 8 * i);
}

// One level of a merkle tree: n hashes in, (n + 1) / 2 out. Several threads can
// find two equal siblings at once, so the flag is set with an atomic operation.
kernel void merkle_level_kernel(device const u32* in [[buffer(0)]],
                                device u32* out [[buffer(1)]],
                                device atomic_uint* mutated [[buffer(2)]],
                                constant u32& n [[buffer(3)]],
                                uint i [[thread_position_in_grid]]) {
    if (i >= (n + 1) / 2) return;
    if (gpu_merkle_node(in, n, i, out)) atomic_fetch_or_explicit(mutated, 1u, memory_order_relaxed);
}
