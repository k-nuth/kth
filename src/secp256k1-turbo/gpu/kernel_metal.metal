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
