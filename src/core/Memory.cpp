#include "core/Memory.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> g_live{0};
std::atomic<std::size_t> g_peak{0};
std::atomic<std::size_t> g_allocs{0};
std::atomic<std::size_t> g_frees{0};

// Every allocation, aligned or not, goes through one scheme so that every
// delete overload can free it. Layout:
//   [ padding ][ Header ][ user block ... ]
//   ^raw                 ^user (aligned)
struct Header {
    void*       raw;
    std::size_t size;
};
static_assert(sizeof(Header) == 16);

constexpr std::size_t kMinAlign = 16;

void* allocate(std::size_t size, std::size_t align) noexcept {
    if (align < kMinAlign) align = kMinAlign;
    void* raw = std::malloc(size + align + sizeof(Header));
    if (!raw) return nullptr;

    const auto base = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
    const auto user = (base + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    auto* hdr = reinterpret_cast<Header*>(user - sizeof(Header));
    hdr->raw  = raw;
    hdr->size = size;

    const std::size_t live = g_live.fetch_add(size, std::memory_order_relaxed) + size;
    std::size_t peak = g_peak.load(std::memory_order_relaxed);
    while (live > peak &&
           !g_peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    return reinterpret_cast<void*>(user);
}

void release(void* ptr) noexcept {
    if (!ptr) return;
    auto* hdr = reinterpret_cast<Header*>(static_cast<char*>(ptr) - sizeof(Header));
    g_live.fetch_sub(hdr->size, std::memory_order_relaxed);
    g_frees.fetch_add(1, std::memory_order_relaxed);
    std::free(hdr->raw);
}

void* allocateOrThrow(std::size_t size, std::size_t align) {
    if (void* p = allocate(size, align)) return p;
    throw std::bad_alloc{};
}

} // namespace

// --- new ---
void* operator new(std::size_t n)                                   { return allocateOrThrow(n, 0); }
void* operator new[](std::size_t n)                                 { return allocateOrThrow(n, 0); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept   { return allocate(n, 0); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocate(n, 0); }
void* operator new(std::size_t n, std::align_val_t a)               { return allocateOrThrow(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a)             { return allocateOrThrow(n, static_cast<std::size_t>(a)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept   { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate(n, static_cast<std::size_t>(a)); }

// --- delete ---
void operator delete(void* p) noexcept                                  { release(p); }
void operator delete[](void* p) noexcept                                { release(p); }
void operator delete(void* p, std::size_t) noexcept                     { release(p); }
void operator delete[](void* p, std::size_t) noexcept                   { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept           { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept         { release(p); }
void operator delete(void* p, std::align_val_t) noexcept                { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept              { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept   { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept   { release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }

namespace carnage::mem {

bool init() { return true; }

Stats stats() {
    return {g_live.load(), g_peak.load(), g_allocs.load(), g_frees.load()};
}

void shutdown() {
    const Stats s = stats();
    std::fprintf(stderr,
                 "[mem] allocs=%zu frees=%zu live=%zu B peak=%zu B\n",
                 s.allocCount, s.freeCount, s.liveBytes, s.peakBytes);
}

} // namespace carnage::mem
