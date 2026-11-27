// DIAGNOSTICO (no es un test): tiempo por operacion y memoria con una carga mixta, para comparar versiones del
// codigo (original / paso N). Usa SOLO la API publica, asi que compila contra cualquier version del include.
//
// Uso: diag_mixed <log2_n> <fraccion_updates> <ops> [theta=1000] [modo=0]
//   modo 0: actualizaciones en posiciones uniformes; modo 1: concentradas en una ventana de 2^16 bits.
// Reporta (una linea): ns/op de la carga mixta, ns/consulta de access/rank/select sobre la estructura ya
// calentada, bits por bit logico (space_bytes), RSS tras construir y pico de RSS (VmHWM).
// Las secuencias de operaciones son identicas entre versiones (RNG propio con semilla fija): los checksums
// deben coincidir. Compilar con -O3 -DNDEBUG -march=native y ejecutar un proceso por configuracion.
#include <adaptive/adaptive.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static uint64_t s_state = 88172645463325252ULL;
static inline uint64_t rnd() {
    s_state ^= s_state << 13;
    s_state ^= s_state >> 7;
    s_state ^= s_state << 17;
    return s_state;
}

static double rss_mb(const char* key) { // VmRSS / VmHWM en MB
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    char line[256];
    double v = 0;
    const size_t kl = std::strlen(key);
    while (std::fgets(line, sizeof line, f))
        if (!std::strncmp(line, key, kl)) {
            v = std::atof(line + kl + 1) / 1024.0;
            break;
        }
    std::fclose(f);
    return v;
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
    const auto tb = std::chrono::steady_clock::now();
    adaptive::bit_vector<> bv(w.data(), n);
    const double build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count();
    std::vector<uint64_t>().swap(w);
    bv.set_theta(theta);
    const double rss_build = rss_mb("VmRSS:");

    uint64_t chk = 0;
    const uint64_t thresh = static_cast<uint64_t>(upd * 1e9);
    const uint64_t hot_base = n / 2, hot_w = std::min<uint64_t>(n / 4, uint64_t{1} << 16);
    auto mixed = [&](uint64_t k) {
        for (uint64_t i = 0; i < k; ++i) {
            if (rnd() % 1000000000ULL < thresh) {
                const uint64_t base = mode ? hot_base : 0, span = mode ? hot_w : n;
                if (rnd() & 1) {
                    bv.insert(base + rnd() % (span + 1), rnd() & 1);
                    ++n;
                } else {
                    chk += static_cast<uint64_t>(bv.erase(base + rnd() % span) + 1);
                    --n;
                }
            } else {
                switch (rnd() % 3) {
                case 0: chk += bv.access(rnd() % n); break;
                case 1: chk += bv.rank1(rnd() % n + 1); break;
                default: chk += bv.select1(1 + rnd() % bv.ones()); break;
                }
            }
        }
    };
    auto since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t).count();
    };

    mixed(ops / 4); // calentamiento: la estructura adaptativa converge a su estado estacionario
    auto t0 = std::chrono::steady_clock::now();
    mixed(ops);
    const double mixed_ns = since(t0) / static_cast<double>(ops);
    const double rss_peak = rss_mb("VmHWM:"); // antes de reservar los arreglos de consultas de abajo

    const uint64_t q = std::min<uint64_t>(ops, 2000000);
    std::vector<uint64_t> pos(q), sel(q);
    for (uint64_t i = 0; i < q; ++i) {
        pos[i] = rnd() % n;
        sel[i] = 1 + rnd() % bv.ones();
    }
    // cada fase de consultas se repite 3 veces y se reporta el minimo (reduce el ruido)
    auto best = [&](auto&& fn) {
        double m = 1e18;
        for (int rep = 0; rep < 3; ++rep) {
            const auto t = std::chrono::steady_clock::now();
            for (uint64_t i = 0; i < q; ++i)
                chk += fn(i);
            m = std::min(m, since(t) / static_cast<double>(q));
        }
        return m;
    };
    const double acc_ns = best([&](uint64_t i) { return static_cast<uint64_t>(bv.access(pos[i])); });
    const double rank_ns = best([&](uint64_t i) { return bv.rank1(pos[i] + 1); });
    const double sel_ns = best([&](uint64_t i) { return bv.select1(sel[i]); });

    std::printf("n=2^%d upd=%g modo=%d | build %.0f ms | mixto %.1f ns | access %.1f rank %.1f select %.1f ns | "
                "%.3f bits/bit | RSS tras construir %.1f MB, pico tras la carga %.1f MB | chk %llu\n",
                log_n, upd, mode, build_ms, mixed_ns, acc_ns, rank_ns, sel_ns,
                8.0 * static_cast<double>(bv.space_bytes()) / static_cast<double>(n), rss_build, rss_peak,
                static_cast<unsigned long long>(chk));
    return 0;
}
