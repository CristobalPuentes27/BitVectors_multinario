// Utilidades compartidas por los tests: RNG determinista, bitvector de
// referencia ingenuo, generadores de patrones de bits y macros CHECK.
#pragma once

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace t {

// ---------------------------------------------------------------- RNG
struct rng {
    uint64_t s;
    explicit rng(uint64_t seed = 1) : s(seed) {}
    uint64_t next() { // splitmix64
        uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return n == 0 ? 0 : next() % n; }
    bool chance(double p) { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0) < p; }
};

// ------------------------------------------------------ CHECK + contexto
inline int& fail_count() {
    static int f = 0;
    return f;
}
inline std::string& context() {
    static std::string c;
    return c;
}
inline void set_context(const std::string& c) { context() = c; }

inline void fail(const char* file, int line, const char* expr, long long a, long long b, bool has_values) {
    std::fprintf(stderr, "FAIL %s:%d: %s", file, line, expr);
    if (has_values) {
        std::fprintf(stderr, "   (obtenido=%lld esperado=%lld)", a, b);
    }
    std::fprintf(stderr, "\n   contexto: %s\n", context().c_str());
    std::fflush(stderr);
    if (++fail_count() > 25) {
        std::fprintf(stderr, "demasiados fallos, abortando\n");
        std::exit(1);
    }
}

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond))                                                                                                   \
            ::t::fail(__FILE__, __LINE__, #cond, 0, 0, false);                                                         \
    } while (0)

// CHECK_EQ(obtenido, esperado)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        const long long va_ = static_cast<long long>(a);                                                               \
        const long long vb_ = static_cast<long long>(b);                                                               \
        if (va_ != vb_)                                                                                                \
            ::t::fail(__FILE__, __LINE__, #a " == " #b, va_, vb_, true);                                               \
    } while (0)

// Si un assert() de la libreria aborta, mostramos en que escenario ocurrio.
inline void on_abort(int) {
    const std::string& c = context();
    const char msg[] = "\n*** ABORT (assert de la libreria). Contexto: ";
    ssize_t r1 = write(2, msg, sizeof(msg) - 1);
    ssize_t r2 = write(2, c.c_str(), c.size());
    ssize_t r3 = write(2, "\n", 1);
    (void)r1;
    (void)r2;
    (void)r3;
    _exit(134);
}
inline void install_abort_handler() { std::signal(SIGABRT, on_abort); }

inline uint64_t env_u64(const char* name, uint64_t def) {
    const char* v = std::getenv(name);
    return v ? std::strtoull(v, nullptr, 10) : def;
}
inline bool verbose() { return env_u64("VERBOSE", 0) != 0; }
// Escala de esfuerzo: 1 = normal, 0 = rapido (QUICK=1), >1 = mas exhaustivo (THOROUGH=n)
inline int effort() {
    if (env_u64("QUICK", 0))
        return 0;
    return static_cast<int>(env_u64("THOROUGH", 1));
}
inline int summary(const char* name) {
    if (fail_count() == 0) {
        std::printf("[ OK ] %s\n", name);
        return 0;
    }
    std::printf("[FAIL] %s (%d fallos)\n", name, fail_count());
    return 1;
}

// ------------------------------------------------- patrones de bits
enum { k_random, k_sparse, k_dense, k_zeros, k_ones, k_runs, k_last_one, k_first_one, k_one_zero, k_num_kinds };

inline const char* kind_name(int k) {
    static const char* names[] = {"random", "sparse", "dense",    "zeros",    "ones",
                                  "runs",   "last1",  "first1",   "one0"};
    return names[k];
}

inline std::vector<uint8_t> make_bits(rng& r, uint64_t n, int kind) {
    std::vector<uint8_t> b(n, 0);
    switch (kind) {
    case k_random:
        for (auto& x : b)
            x = r.next() & 1;
        break;
    case k_sparse:
        for (auto& x : b)
            x = r.chance(0.01);
        break;
    case k_dense:
        for (auto& x : b)
            x = !r.chance(0.01);
        break;
    case k_zeros:
        break;
    case k_ones:
        std::fill(b.begin(), b.end(), 1);
        break;
    case k_runs: {
        uint64_t pos = 0;
        uint8_t v = r.next() & 1;
        while (pos < n) {
            const uint64_t len = 1 + r.below(5000);
            for (uint64_t k = 0; k < len && pos < n; ++k, ++pos)
                b[pos] = v;
            v ^= 1;
        }
        break;
    }
    case k_last_one:
        if (n)
            b[n - 1] = 1;
        break;
    case k_first_one:
        if (n)
            b[0] = 1;
        break;
    case k_one_zero:
        std::fill(b.begin(), b.end(), 1);
        if (n)
            b[n / 2] = 0;
        break;
    }
    return b;
}

inline std::vector<uint64_t> pack(const std::vector<uint8_t>& bits, size_t extra_words = 0) {
    std::vector<uint64_t> w((bits.size() + 63) / 64 + extra_words, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            w[i / 64] |= uint64_t{1} << (i % 64);
    return w;
}
inline bool bit_of(const std::vector<uint64_t>& w, uint64_t i) { return (w[i / 64] >> (i % 64)) & 1; }

// ---------------------------------------------- bitvector de referencia
class ref_bv {
  public:
    std::vector<uint8_t> b;

    ref_bv() = default;
    explicit ref_bv(std::vector<uint8_t> bits) : b(std::move(bits)) {
        for (auto x : b)
            ones_ += x;
    }

    uint64_t size() const { return b.size(); }
    uint64_t ones() const { return ones_; }
    uint64_t zeros() const { return b.size() - ones_; }

    void insert(uint64_t i, bool v) {
        b.insert(b.begin() + static_cast<std::ptrdiff_t>(i), v ? 1 : 0);
        ones_ += v;
        dirty_ = true;
    }
    int erase(uint64_t i) {
        const int d = b[i] ? -1 : 0;
        ones_ += d;
        b.erase(b.begin() + static_cast<std::ptrdiff_t>(i));
        dirty_ = true;
        return d;
    }
    int write(uint64_t i, bool v) {
        const int d = static_cast<int>(v) - static_cast<int>(b[i]);
        b[i] = v;
        ones_ += d;
        dirty_ = true;
        return d;
    }

    // Convencion de la libreria: rank(i) cuenta en el prefijo [0, i); select es 1-based en j y 0-based en la posicion.
    uint64_t rank1(uint64_t i) const {
        sync();
        return pre_[i];
    }
    uint64_t rank0(uint64_t i) const { return i - rank1(i); }
    uint64_t select1(uint64_t j) const {
        sync();
        const auto it = std::lower_bound(pre_.begin(), pre_.end(), j);
        return static_cast<uint64_t>(it - pre_.begin()) - 1;
    }
    uint64_t select0(uint64_t j) const {
        sync();
        uint64_t lo = 1, hi = b.size(); // menor x con ceros(x) >= j
        while (lo < hi) {
            const uint64_t mid = (lo + hi) / 2;
            if (mid - pre_[mid] >= j)
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo - 1;
    }
    int64_t next1(uint64_t i) const { // incluye i; -1 si no hay
        const uint64_t r = rank1(i);
        return r == ones_ ? -1 : static_cast<int64_t>(select1(r + 1));
    }
    int64_t next0(uint64_t i) const {
        const uint64_t z = rank0(i);
        return z == zeros() ? -1 : static_cast<int64_t>(select0(z + 1));
    }
    std::vector<uint64_t> words(size_t extra = 0) const { return pack(b, extra); }

  private:
    uint64_t ones_ = 0;
    mutable bool dirty_ = true;
    mutable std::vector<uint64_t> pre_;
    void sync() const {
        if (!dirty_)
            return;
        pre_.assign(b.size() + 1, 0);
        for (size_t i = 0; i < b.size(); ++i)
            pre_[i + 1] = pre_[i] + b[i];
        dirty_ = false;
    }
};

// Puntos de prueba: todos si el rango es chico; si no, bordes + estructurales + aleatorios.
inline std::vector<uint64_t> points(rng& r, uint64_t lo, uint64_t hi, uint64_t max_count) {
    std::vector<uint64_t> v;
    if (hi < lo)
        return v;
    if (hi - lo + 1 <= max_count) {
        for (uint64_t x = lo; x <= hi; ++x)
            v.push_back(x);
        return v;
    }
    auto push = [&](uint64_t x) {
        if (x >= lo && x <= hi)
            v.push_back(x);
    };
    for (uint64_t k = 0; k < 4; ++k) {
        push(lo + k);
        push(hi - k);
    }
    for (uint64_t m : {uint64_t{64}, uint64_t{256}, uint64_t{1024}, uint64_t{8192}, uint64_t{65536}})
        for (uint64_t x = m; x <= hi && x < m * 40; x += m) {
            push(x - 1);
            push(x);
            push(x + 1);
        }
    while (v.size() < max_count)
        v.push_back(lo + r.below(hi - lo + 1));
    return v;
}

// Verifica un metodo read(i,len,out,out_bit): los bits [out_bit,out_bit+len) deben coincidir con la
// referencia y los bits anteriores a out_bit deben conservarse. Buffer con 1 palabra extra (como flatten()).
template <class ReadFn>
void check_read(ReadFn&& read, const ref_bv& ref, uint64_t i, uint64_t len, uint64_t out_bit, rng& r) {
    std::vector<uint64_t> out((out_bit + len + 63) / 64 + 1);
    for (auto& w : out)
        w = r.next();
    const auto before = out;
    read(i, len, out.data(), out_bit);
    bool ok = true;
    for (uint64_t k = 0; k < out_bit && ok; ++k)
        ok = bit_of(out, k) == bit_of(before, k);
    CHECK(ok); // bits previos preservados
    ok = true;
    for (uint64_t k = 0; k < len && ok; ++k)
        ok = bit_of(out, out_bit + k) == static_cast<bool>(ref.b[i + k]);
    CHECK(ok); // contenido copiado
}

} // namespace t
