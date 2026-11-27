// DIAGNOSTICO (no es un test): evolucion de la memoria huerfana de los buffers compartidos bajo una carga.
// Imprime, en checkpoints, cuantos buffers hay, cuantos bits retienen y cuantos de esos bits ya no los
// referencia ninguna vista (dead). Es la evidencia para elegir la politica de compactacion (paso 7).
//
// Uso: diag_orphans <log2_n> <fraccion_updates> <ops> [theta=1000] [modo=0]
//   modo 0: actualizaciones uniformes; modo 1: concentradas en una ventana de 2^16 bits;
//   modo 2: uniformes pero con rafagas de consultas masivas sobre una region (provoca flatten).
// Compilar con -O2 -DNDEBUG -march=native (solo funciona con la version que tiene report_memory()).
#include <adaptive/adaptive.hpp>

#include <cstdio>
#include <cstdlib>
#include <vector>

static uint64_t s_state = 88172645463325252ULL;
static inline uint64_t rnd() {
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return s_state;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "uso: %s <log2_n> <fraccion_updates> <ops> [theta=1000] [modo=0]\n", argv[0]);
        return 1;
    }
    const int log_n = std::atoi(argv[1]);
    const double upd = std::atof(argv[2]);
    const uint64_t ops = std::strtoull(argv[3], nullptr, 10);
    const uint64_t theta = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 1000;
    const int mode = argc > 5 ? std::atoi(argv[5]) : 0;
    uint64_t n = uint64_t{1} << log_n;

    std::vector<uint64_t> w(n / 64);
    for (auto& x : w)
        x = rnd();
    adaptive::bit_vector<> bv(w.data(), n);
    std::vector<uint64_t>().swap(w);
    bv.set_theta(theta);

    const uint64_t thresh = static_cast<uint64_t>(upd * 1e9);
    const uint64_t hot_base = n / 2, hot_w = std::min<uint64_t>(n / 4, uint64_t{1} << 16);
    uint64_t chk = 0;

    std::printf("# n=2^%d upd=%g modo=%d theta=%llu  (MB = 2^20 bytes; 'dead' = bits de buffers sin vista)\n", log_n,
                upd, mode, static_cast<unsigned long long>(theta));
    std::printf("%10s %8s %7s %9s %10s %10s %7s %8s %9s %10s\n", "ops", "buffers", "views", "leaves", "buf_MB",
                "dead_MB", "dead%", "total_MB", "bits/bit", "dyn_nodes");
    auto report = [&](uint64_t done) {
        const adaptive::memory_report m = bv.report_memory();
        const double mb = 1.0 / (1 << 20);
        std::printf("%10llu %8llu %7llu %9llu %10.2f %10.2f %6.1f%% %8.2f %9.3f %10llu\n",
                    static_cast<unsigned long long>(done), static_cast<unsigned long long>(m.buffers),
                    static_cast<unsigned long long>(m.static_views), static_cast<unsigned long long>(m.leaves),
                    static_cast<double>(m.buffer_bytes) * mb, static_cast<double>(m.dead_bits()) / 8.0 * mb,
                    100.0 * m.dead_fraction(), static_cast<double>(bv.space_bytes()) * mb,
                    8.0 * static_cast<double>(bv.space_bytes()) / static_cast<double>(n),
                    static_cast<unsigned long long>(m.dynamic_nodes));
    };
    report(0);
    const uint64_t step = ops / 10 ? ops / 10 : 1;
    for (uint64_t i = 1; i <= ops; ++i) {
        if (rnd() % 1000000000ULL < thresh) {
            const uint64_t base = mode == 1 ? hot_base : 0, span = mode == 1 ? hot_w : n;
            if (rnd() & 1) {
                bv.insert(base + rnd() % (span + 1), rnd() & 1);
                ++n;
            } else {
                chk += static_cast<uint64_t>(bv.erase(base + rnd() % span) + 1);
                --n;
            }
        } else if (mode == 2 && (rnd() & 255) == 0) {
            // rafaga de consultas sobre una region: los subarboles de esa region se aplanan (flatten)
            const uint64_t lo = rnd() % n, width = std::min<uint64_t>(n - lo, uint64_t{1} << 18);
            for (int k = 0; k < 20000; ++k)
                chk += bv.rank1(lo + rnd() % width + 1 > n ? n : lo + rnd() % width + 1);
        } else {
            chk += bv.rank1(rnd() % n + 1);
        }
        if (i % step == 0)
            report(i);
    }
    std::fprintf(stderr, "(chk %llu)\n", static_cast<unsigned long long>(chk));
    return 0;
}
