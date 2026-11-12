// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Metal runtime for the GPU kernel (Apple GPUs). The kernel source is
// embedded and compiled by the system Metal compiler on first use; buffers
// use the shared storage mode of the unified memory, so no copies are made.
// A batch is one dispatch: there is no copy to overlap, laying it out takes
// about 1% of the kernel time, and split into chunks (one command buffer
// each) the tail of every chunk left the GPU partly idle, 3% overall.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "gpu_backend.hpp"

namespace {

constexpr NSUInteger metal_threadgroup = 64;

char const* const kernel_source =
#include "kernel_metal_source.inc"
;

struct context {
    id<MTLBuffer> in = nil;
    id<MTLBuffer> out = nil;
    std::size_t capacity = 0;
    bool busy = false;
};

constexpr int context_count = 4;

struct state {
    bool ok = false;
    std::string name;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    id<MTLComputePipelineState> field_test = nil;
    id<MTLBuffer> g = nil;
    context ctx[context_count];
    std::mutex m;
    std::condition_variable cv;
};

bool reserve(state& s, context& c, std::size_t n) {
    if (n <= c.capacity) return true;
    std::size_t cap = c.capacity ? c.capacity : 1024;
    while (cap < n) cap *= 2;
    c.in = [s.device newBufferWithLength:cap * sizeof(gpu_raw_input) options:MTLResourceStorageModeShared];
    c.out = [s.device newBufferWithLength:cap options:MTLResourceStorageModeShared];
    if (c.in == nil || c.out == nil) { c.capacity = 0; return false; }
    c.capacity = cap;
    return true;
}

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

void init(state& st) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev == nil) return;
        st.device = dev;
        st.name = [[dev name] UTF8String];
        NSError* err = nil;
        MTLCompileOptions* opts = [MTLCompileOptions new];
        // Tuning aid: KTH_SECP256K1_TURBO_METAL_DEFINES="A=1,B=2" defines macros for the kernel.
        if (char const* defs = std::getenv("KTH_SECP256K1_TURBO_METAL_DEFINES")) {
            NSMutableDictionary* macros = [NSMutableDictionary new];
            for (NSString* kv in [[NSString stringWithUTF8String:defs] componentsSeparatedByString:@","]) {
                NSArray* parts = [kv componentsSeparatedByString:@"="];
                if ([parts count] == 2) macros[parts[0]] = parts[1];
            }
            opts.preprocessorMacros = macros;
        }
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kernel_source] options:opts error:&err];
        if (lib == nil) {
            st.name += " (kernel compile failed: ";
            st.name += err ? [[err localizedDescription] UTF8String] : "?";
            st.name += ")";
            return;
        }
        id<MTLFunction> fn = [lib newFunctionWithName:@"verify_kernel"];
        if (fn == nil) return;
        st.pipeline = [dev newComputePipelineStateWithFunction:fn error:&err];
        if (st.pipeline == nil) return;
        id<MTLFunction> ft = [lib newFunctionWithName:@"field_test_kernel"];
        if (ft != nil) st.field_test = [dev newComputePipelineStateWithFunction:ft error:&err];
        st.queue = [dev newCommandQueue];
        if (st.queue == nil) return;
        st.g = [dev newBufferWithBytes:secp256k1_turbo::gpu::g_table_words() length:GPU_G_WORDS * sizeof(u32) options:MTLResourceStorageModeShared];
        if (st.g == nil) return;
        st.ok = true;
    }
}

state& get_state() {
    static state s;
    static std::once_flag once;
    std::call_once(once, [] { init(s); });
    return s;
}

} // namespace

namespace secp256k1_turbo::gpu {

bool metal_available() { return get_state().ok; }
std::string metal_device_name() { return get_state().name; }

bool metal_verify(std::size_t n, bool schnorr, fill_fn const& fill, std::uint8_t* out) {
    state& s = get_state();
    if (!s.ok) return false;
    if (n == 0) return true;
    context& c = acquire(s);
    bool ok = reserve(s, c, n);
    if (ok) @autoreleasepool {
        fill(static_cast<gpu_raw_input*>([c.in contents]), 0, n);
        id<MTLCommandBuffer> cmd = [s.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:s.pipeline];
        [enc setBuffer:c.in offset:0 atIndex:0];
        [enc setBuffer:c.out offset:0 atIndex:1];
        [enc setBuffer:s.g offset:0 atIndex:2];
        u32 const nn = static_cast<u32>(n);
        u32 const sch = schnorr ? 1u : 0u;
        [enc setBytes:&nn length:sizeof(nn) atIndex:3];
        [enc setBytes:&sch length:sizeof(sch) atIndex:4];
        static NSUInteger const tg_env = [] { char const* e = std::getenv("KTH_SECP256K1_TURBO_METAL_TG"); return e ? NSUInteger(std::atoi(e)) : NSUInteger(0); }();
        NSUInteger tg = [s.pipeline maxTotalThreadsPerThreadgroup];
        NSUInteger const want = tg_env ? tg_env : metal_threadgroup;
        if (tg > want) tg = want;
        [enc dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        ok = [cmd status] == MTLCommandBufferStatusCompleted;
        if (ok) std::memcpy(out, [c.out contents], n);
    }
    release(s, c);
    return ok;
}

bool metal_field_test(gpu_field_test_in const* in, std::size_t n, gpu_field_test_out* out) {
    state& s = get_state();
    if (!s.ok || s.field_test == nil) return false;
    if (n == 0) return true;
    @autoreleasepool {
        id<MTLBuffer> bin = [s.device newBufferWithBytes:in length:n * sizeof(gpu_field_test_in) options:MTLResourceStorageModeShared];
        id<MTLBuffer> bout = [s.device newBufferWithLength:n * sizeof(gpu_field_test_out) options:MTLResourceStorageModeShared];
        if (bin == nil || bout == nil) return false;
        id<MTLCommandBuffer> cmd = [s.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:s.field_test];
        [enc setBuffer:bin offset:0 atIndex:0];
        [enc setBuffer:bout offset:0 atIndex:1];
        u32 const nn = static_cast<u32>(n);
        [enc setBytes:&nn length:sizeof(nn) atIndex:2];
        [enc dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        if ([cmd status] != MTLCommandBufferStatusCompleted) return false;
        std::memcpy(out, [bout contents], n * sizeof(gpu_field_test_out));
    }
    return true;
}

} // namespace secp256k1_turbo::gpu
