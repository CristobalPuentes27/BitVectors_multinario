// DIAGNOSTICO (no es un test): mide el costo de memoria del splitting en la implementacion actual.
// Cuenta asignaciones (new/delete) y bytes pedidos al alocador, el pico de memoria viva del heap y el tiempo,
// durante las primeras actualizaciones sobre un bitvector recien construido (todas las hojas estaticas).
// Es el "antes" cuantitativo para comparar contra la version con buffers flotantes.
//
// Uso: diag_split_cost [log2_n=22] [num_updates=20000]
// Compilar SIN sanitizers (reemplaza operator new/delete globalmente).
#include <adaptive/adaptive.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static size_t g_allocs = 0, g_bytes = 0, g_live = 0, g_peak = 0;

void* operator new(size_t n) {
    void* p = std::malloc(n + 16);
    if (!p)
        throw std::bad_alloc();
    *static_cast<size_t*>(p) = n;
    ++g_allocs;
    g_bytes += n;
    g_live += n;
    if (g_live > g_peak)
        g_peak = g_live;
    return static_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p)
        return;
    char* q = static_cast<char*>(p) - 16;
    g_live -= *reinterpret_cast<size_t*>(q);
    std::free(q);
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

int main(int argc, char** argv) {
    const int log_n = argc > 1 ? std::atoi(argv[1]) : 22;
    const uint64_t updates = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20000;
    const uint64_t n0 = uint64_t{1} << log_n;

    uint64_t s = 88172645463325252ULL; // xorshift64
    auto rnd = [&]() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    };

    std::vector<uint64_t> data(n0 / 64);
    for (auto& w : data)
        w = rnd();

    adaptive::bit_vector<> bv(data.data(), n0);
    const double base_bits = 8.0 * static_cast<double>(bv.space_bytes()) / static_cast<double>(n0);
    // base de comparacion: lo que ya estaba asignado antes de las actualizaciones
    const size_t a0 = g_allocs, b0 = g_bytes, live0 = g_live;
    g_peak = g_live;

    std::printf("# n=2^%d, bit_vector<> por defecto. Valores acumulados desde la primera actualizacion.\n", log_n);
    std::printf("# bits_por_bit inicial (space_bytes) = %.3f\n", base_bits);
    std::printf("%10s %12s %14s %16s %14s %10s\n", "updates", "allocs", "MB_pedidos", "MB_pico_extra", "bits/bit(sb)",
                "ms");

    uint64_t n = n0, next_report = 1;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t u = 1; u <= updates; ++u) {
        if (rnd() & 1)
            bv.insert(rnd() % (++n), rnd() & 1);
        else
            bv.erase(rnd() % (n--));
        if (u == next_report || u == updates) {
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("%10llu %12zu %14.2f %16.2f %14.3f %10.2f\n", (unsigned long long)u, g_allocs - a0,
                        static_cast<double>(g_bytes - b0) / 1048576.0,
                        static_cast<double>(g_peak > live0 ? g_peak - live0 : 0) / 1048576.0,
                        8.0 * static_cast<double>(bv.space_bytes()) / static_cast<double>(n), ms);
            next_report *= 10;
        }
    }
    return 0;
}
