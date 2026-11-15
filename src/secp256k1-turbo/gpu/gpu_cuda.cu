// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CUDA runtime for the GPU kernel: one thread per signature, the G comb
// table staged in shared memory per block, pinned host staging buffers and
// private streams. Device buffers grow on demand and are kept. A batch is
// laid out, uploaded, verified and downloaded chunk by chunk, the chunks
// alternating between streams, so the host work and the copies of one chunk
// overlap the kernels of the others.

#include <cuda_runtime.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>

#include "gpu_backend.hpp"

namespace {

// 64, 128 and 256 threads per block, and register caps for 3 or 4 blocks
// per SM, were measured within 3% of each other.
constexpr int block_size = 128;

__global__ void verify_kernel(gpu_raw_input const* __restrict__ in, unsigned char* __restrict__ out,
                              u32 const* __restrict__ g_global, u32 n, u32 schnorr) {
    __shared__ u32 g[GPU_G_WORDS];
    for (u32 i = threadIdx.x; i < GPU_G_WORDS; i += blockDim.x) g[i] = g_global[i];
    __syncthreads();
    u32 const i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    out[i] = static_cast<unsigned char>(gpu_verify_one(in + i, g, schnorr));
}

__global__ void field_test_kernel(gpu_field_test_in const* __restrict__ in, gpu_field_test_out* __restrict__ out, u32 n) {
    u32 const i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) gpu_field_test_one(in + i, out + i);
}

// A batch goes to the device in chunks: the first is small, so the device
// starts early; each next one is as large as all before it, up to
// max_chunk, so a large batch takes few launches. Returns the end of the
// chunk that starts at begin.
constexpr std::size_t min_chunk = 4096;
constexpr std::size_t max_chunk = 65536;
std::size_t chunk_end(std::size_t begin, std::size_t n) {
    std::size_t const size = begin < min_chunk ? min_chunk : (begin < max_chunk ? begin : max_chunk);
    return n - begin <= size ? n : begin + size;
}

// Streams per batch. The chunks of a batch are independent; on separate
// streams the small first ones run side by side instead of each waiting
// for the previous one's last thread (with two streams a 16,384-signature
// batch was 25% slower; more than four gained nothing).
constexpr int stream_count = 4;

// One set of buffers and streams; several of them let the batches of
// several host threads overlap on the device.
struct context {
    cudaStream_t streams[stream_count] = {};
    gpu_raw_input* d_in = nullptr;
    unsigned char* d_out = nullptr;
    gpu_raw_input* h_in = nullptr;  // pinned
    unsigned char* h_out = nullptr; // pinned
    std::size_t capacity = 0;
    bool busy = false;
};

constexpr int context_count = 4;

struct state {
    bool ok = false;
    std::string name;
    u32* d_g = nullptr;
    context ctx[context_count];
    std::mutex m;
    std::condition_variable cv;
};

bool reserve(context& c, std::size_t n) {
    if (n <= c.capacity) return true;
    std::size_t cap = c.capacity ? c.capacity : 1024;
    while (cap < n) cap *= 2;
    if (c.d_in) cudaFree(c.d_in);
    if (c.d_out) cudaFree(c.d_out);
    if (c.h_in) cudaFreeHost(c.h_in);
    if (c.h_out) cudaFreeHost(c.h_out);
    c.d_in = nullptr; c.d_out = nullptr; c.h_in = nullptr; c.h_out = nullptr;
    c.capacity = 0;
    if (cudaMalloc(&c.d_in, cap * sizeof(gpu_raw_input)) != cudaSuccess) return false;
    if (cudaMalloc(&c.d_out, cap) != cudaSuccess) return false;
    if (cudaHostAlloc(&c.h_in, cap * sizeof(gpu_raw_input), cudaHostAllocDefault) != cudaSuccess) return false;
    if (cudaHostAlloc(&c.h_out, cap, cudaHostAllocDefault) != cudaSuccess) return false;
    c.capacity = cap;
    return true;
}

void init(state& st) {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) { cudaGetLastError(); return; }
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, 0) != cudaSuccess) return;
    st.name = prop.name;
    if (cudaSetDevice(0) != cudaSuccess) return;
    for (context& c : st.ctx) {
        for (cudaStream_t& stream : c.streams) {
            if (cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) != cudaSuccess) return;
        }
    }
    if (cudaMalloc(&st.d_g, GPU_G_WORDS * sizeof(u32)) != cudaSuccess) return;
    if (cudaMemcpy(st.d_g, secp256k1_turbo::gpu::g_table_words(), GPU_G_WORDS * sizeof(u32), cudaMemcpyHostToDevice) != cudaSuccess) return;
    st.ok = true;
}

state& get_state() {
    static state s;
    static std::once_flag once;
    std::call_once(once, [] { init(s); });
    return s;
}

// Waits for a free context.
context& acquire(state& s) {
    std::unique_lock<std::mutex> lock(s.m);
    for (;;) {
        for (context& c : s.ctx) if (!c.busy) { c.busy = true; return c; }
        s.cv.wait(lock);
    }
}
void release(state& s, context& c) {
    { std::lock_guard<std::mutex> lock(s.m); c.busy = false; }
    s.cv.notify_one();
}

} // namespace

namespace secp256k1_turbo::gpu {

bool cuda_available() { return get_state().ok; }
std::string cuda_device_name() { return get_state().name; }

bool cuda_verify(std::size_t n, bool schnorr, fill_fn const& fill, std::uint8_t* out) {
    state& s = get_state();
    if (!s.ok) return false;
    if (n == 0) return true;
    context& c = acquire(s);
    bool ok = reserve(c, n);
    // A failed call stops the enqueuing; the streams are drained before
    // the context is released either way.
    int k = 0;
    for (std::size_t begin = 0; ok && begin < n; ++k) {
        std::size_t const end = chunk_end(begin, n);
        std::size_t const count = end - begin;
        cudaStream_t const stream = c.streams[k % stream_count];
        fill(c.h_in, begin, end);
        ok = cudaMemcpyAsync(c.d_in + begin, c.h_in + begin, count * sizeof(gpu_raw_input), cudaMemcpyHostToDevice, stream) == cudaSuccess;
        if (ok) {
            u32 const blocks = static_cast<u32>((count + block_size - 1) / block_size);
            verify_kernel<<<blocks, block_size, 0, stream>>>(c.d_in + begin, c.d_out + begin, s.d_g, static_cast<u32>(count), schnorr ? 1u : 0u);
            ok = cudaGetLastError() == cudaSuccess &&
                 cudaMemcpyAsync(c.h_out + begin, c.d_out + begin, count, cudaMemcpyDeviceToHost, stream) == cudaSuccess;
        }
        begin = end;
    }
    for (cudaStream_t stream : c.streams) {
        if (cudaStreamSynchronize(stream) != cudaSuccess) ok = false;
    }
    if (ok) std::memcpy(out, c.h_out, n);
    release(s, c);
    return ok;
}

bool cuda_field_test(gpu_field_test_in const* in, std::size_t n, gpu_field_test_out* out) {
    state& s = get_state();
    if (!s.ok || n == 0) return s.ok;
    gpu_field_test_in* d_in = nullptr;
    gpu_field_test_out* d_out = nullptr;
    bool ok = cudaMalloc(&d_in, n * sizeof(gpu_field_test_in)) == cudaSuccess &&
              cudaMalloc(&d_out, n * sizeof(gpu_field_test_out)) == cudaSuccess &&
              cudaMemcpy(d_in, in, n * sizeof(gpu_field_test_in), cudaMemcpyHostToDevice) == cudaSuccess;
    if (ok) {
        field_test_kernel<<<static_cast<u32>((n + 127) / 128), 128>>>(d_in, d_out, static_cast<u32>(n));
        ok = cudaGetLastError() == cudaSuccess && cudaDeviceSynchronize() == cudaSuccess &&
             cudaMemcpy(out, d_out, n * sizeof(gpu_field_test_out), cudaMemcpyDeviceToHost) == cudaSuccess;
    }
    if (d_in) cudaFree(d_in);
    if (d_out) cudaFree(d_out);
    return ok;
}

} // namespace secp256k1_turbo::gpu
