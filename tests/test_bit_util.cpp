// Tests de bit_util: popcount, select_in_word, next_in_word y copy_bits.
// copy_bits es critico: las vistas sobre buffers compartidos lo usaran con desplazamientos arbitrarios.
#include <adaptive/bit_util.hpp>

#include "test_util.hpp"

using namespace adaptive;

static void test_popcount_select(t::rng& r) {
    t::set_context("popcount/select_in_word/next_in_word");
    std::vector<uint64_t> words = {1, 2, 0x8000000000000000ULL, ~uint64_t{0}, 0xAAAAAAAAAAAAAAAAULL,
                                   0x5555555555555555ULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL,
                                   0x0123456789ABCDEFULL};
    for (int i = 0; i < 20000; ++i) {
        uint64_t w = r.next();
        if (i % 3 == 0)
            w &= r.next(); // mas ralo
        if (i % 7 == 0)
            w |= r.next(); // mas denso
        if (w == 0)
            w = 1;
        words.push_back(w);
    }
    for (uint64_t w : words) {
        CHECK_EQ(bit_util::popcount(w), __builtin_popcountll(w));
        uint32_t k = 0;
        for (uint32_t p = 0; p < 64; ++p) {
            if ((w >> p) & 1) {
                ++k;
                CHECK_EQ(bit_util::select_in_word(w, k), p);
            }
        }
        CHECK_EQ(bit_util::next_in_word(w), __builtin_ctzll(w));
    }
}

// Estricto: buffers de tamano exacto (ASan detecta cualquier lectura/escritura fuera de rango).
static void test_copy_bits(t::rng& r, bool exact) {
    t::set_context(exact ? "copy_bits (buffers exactos)" : "copy_bits (destino con 1 palabra extra)");
    const int iters = t::effort() == 0 ? 30000 : 200000;
    for (int it = 0; it < iters; ++it) {
        uint64_t len;
        switch (r.below(4)) {
        case 0: len = r.below(130); break;
        case 1: len = 64 * r.below(8); break;
        case 2: len = 64 * r.below(8) + (r.below(3) - 1 + 64) % 64; break;
        default: len = r.below(700); break;
        }
        const uint64_t sb = r.below(200), tb = r.below(200);
        if (len == 0) {
            uint64_t dummy = 0;
            bit_util::copy_bits(&dummy, 0, &dummy, 0, 0);
            continue;
        }
        std::vector<uint64_t> src((sb + len + 63) / 64);
        std::vector<uint64_t> tgt((tb + len + 63) / 64 + (exact ? 0 : 1));
        for (auto& w : src)
            w = r.next();
        for (auto& w : tgt)
            w = r.next();
        const auto before = tgt;
        bit_util::copy_bits(tgt.data(), tb, src.data(), sb, len);
        bool ok = true;
        for (uint64_t k = 0; k < tb && ok; ++k)
            ok = t::bit_of(tgt, k) == t::bit_of(before, k);
        if (!ok)
            std::fprintf(stderr, "   bits previos modificados: sb=%llu tb=%llu len=%llu\n", (unsigned long long)sb,
                         (unsigned long long)tb, (unsigned long long)len);
        CHECK(ok);
        ok = true;
        for (uint64_t k = 0; k < len && ok; ++k)
            ok = t::bit_of(tgt, tb + k) == t::bit_of(src, sb + k);
        if (!ok)
            std::fprintf(stderr, "   contenido incorrecto: sb=%llu tb=%llu len=%llu\n", (unsigned long long)sb,
                         (unsigned long long)tb, (unsigned long long)len);
        CHECK(ok);
        if (t::fail_count() > 0)
            return;
    }
}

int main() {
    t::install_abort_handler();
    t::rng r(t::env_u64("SEED", 12345));
    test_popcount_select(r);
    test_copy_bits(r, true);
    test_copy_bits(r, false);
    return t::summary("bit_util");
}
