// WebAssembly wrapper around the real 3310 DSP engine (no JUCE).
#include "Telephone.h"
extern "C" {
// minimal libc pieces (no libc in wasm32 freestanding)
void* memcpy(void* d, const void* s, unsigned long n) { auto* a = (unsigned char*) d; auto* b = (const unsigned char*) s; while (n--) *a++ = *b++; return d; }
void* memmove(void* d, const void* s, unsigned long n) { auto* a = (unsigned char*) d; auto* b = (const unsigned char*) s; if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; } return d; }
void* memset(void* d, int c, unsigned long n) { auto* a = (unsigned char*) d; while (n--) *a++ = (unsigned char) c; return d; }
}
extern unsigned char __heap_base;
static unsigned long heapPtr = 0;
extern "C" void* malloc(unsigned long n)
{
    if (!heapPtr) heapPtr = (unsigned long) &__heap_base;
    heapPtr = (heapPtr + 15) & ~15ul;
    const unsigned long p = heapPtr; heapPtr += n;
    const unsigned long need = (heapPtr + 65535) / 65536;
    const unsigned long have = __builtin_wasm_memory_size(0);
    if (need > have) __builtin_wasm_memory_grow(0, need - have);
    return (void*) p;
}
extern "C" void free(void*) {}
void* operator new(unsigned long n) { return malloc(n); }
void* operator new[](unsigned long n) { return malloc(n); }
void operator delete(void*) noexcept {}
void operator delete(void*, unsigned long) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete[](void*, unsigned long) noexcept {}

static n3310::Engine* eng = nullptr;
static constexpr int kMax = 4096;
static float bufL[kMax], bufR[kMax];

extern "C" {
__attribute__((export_name("init"))) int init (float fs) { heapPtr = 0; eng = new n3310::Engine(); eng->prepare (fs); return eng->latencySamples(); }
__attribute__((export_name("set_mode"))) void set_mode (int m) { if (eng) eng->setModeIndex (m); }
__attribute__((export_name("set_mix"))) void set_mix (float m) { if (eng) eng->setMix (m); }
__attribute__((export_name("buf_l"))) float* buf_l() { return bufL; }
__attribute__((export_name("buf_r"))) float* buf_r() { return bufR; }
__attribute__((export_name("process"))) void process (int n) { if (eng && n > 0 && n <= kMax) eng->process (bufL, bufR, n); }
}
