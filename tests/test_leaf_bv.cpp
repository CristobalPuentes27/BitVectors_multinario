// Tests de leaf_bv_node (hoja dinamica de tamano acotado) contra la referencia.
#include <adaptive/bit_vector/leaf_bv_node.hpp>

#include "test_util.hpp"

using namespace adaptive;

template <uint32_t MB> static void check_leaf(const leaf_bv_node<MB>& L, const t::ref_bv& ref, t::rng& r, bool full) {
    const uint64_t n = ref.size();
    CHECK_EQ(L.size(), n);
    CHECK_EQ(L.ones(), ref.ones());
    const uint64_t cap = full ? n + 1 : 40;
    if (n > 0)
        for (uint64_t i : t::points(r, 0, n - 1, cap)) {
            CHECK_EQ(L.access(static_cast<uint32_t>(i)), ref.b[i]);
            CHECK_EQ(L.template next<true>(static_cast<uint32_t>(i)), ref.next1(i));
            CHECK_EQ(L.template next<false>(static_cast<uint32_t>(i)), ref.next0(i));
        }
    for (uint64_t i : t::points(r, 0, n, cap)) {
        CHECK_EQ(L.template rank<true>(static_cast<uint32_t>(i)), ref.rank1(i));
        CHECK_EQ(L.template rank<false>(static_cast<uint32_t>(i)), ref.rank0(i));
    }
    if (ref.ones() > 0)
        for (uint64_t j : t::points(r, 1, ref.ones(), cap))
            CHECK_EQ(L.template select<true>(static_cast<uint32_t>(j)), ref.select1(j));
    if (ref.zeros() > 0)
        for (uint64_t j : t::points(r, 1, ref.zeros(), cap))
            CHECK_EQ(L.template select<false>(static_cast<uint32_t>(j)), ref.select0(j));
    if (full) {
        for (int k = 0; k < 20; ++k) {
            const uint64_t i = r.below(n + 1);
            const uint64_t len = r.below(n - i + 1);
            t::check_read(
                [&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) {
                    L.read(static_cast<uint32_t>(a), static_cast<uint32_t>(l), o, ob);
                },
                ref, i, len, r.below(100), r);
        }
    }
}

template <uint32_t MB> static void run(t::rng& r) {
    const std::string nm = "leaf_bv_node<" + std::to_string(MB) + ">";

    // 1) Construccion con offset arbitrario (la usan split_into y merge_with)
    for (int it = 0; it < 300; ++it) {
        t::set_context(nm + " ctor con offset, it=" + std::to_string(it));
        const uint64_t total = MB + 300;
        auto bits = t::make_bits(r, total, static_cast<int>(r.below(t::k_num_kinds)));
        auto words = t::pack(bits);
        const uint64_t size = r.below(MB + 1);
        const uint64_t off = r.below(total - size + 1);
        leaf_bv_node<MB> L(words.data(), off, static_cast<uint32_t>(size));
        t::ref_bv ref(std::vector<uint8_t>(bits.begin() + off, bits.begin() + off + size));
        if (L.ones() != ref.ones()) {
            // BUG CONOCIDO: el constructor cuenta en ones_ los bits basura que copy_bits deja sobre `size`
            // en la ultima palabra. Se reporta una sola vez y se omite el resto para no abortar por assert.
            std::fprintf(stderr, "   (ctor con offset: size=%llu off=%llu)\n", (unsigned long long)size,
                         (unsigned long long)off);
            CHECK_EQ(L.ones(), ref.ones());
            break;
        }
        check_leaf(L, ref, r, true);
    }

    // 2) Secuencias aleatorias de insert/erase/write con crecimiento y decrecimiento
    const int rounds = t::effort() == 0 ? 3 : 8;
    for (int round = 0; round < rounds; ++round) {
        t::set_context(nm + " operaciones aleatorias ronda " + std::to_string(round));
        leaf_bv_node<MB> L;
        t::ref_bv ref;
        double p_ins = (round % 2 == 0) ? 0.7 : 0.3;
        for (int step = 0; step < 6000; ++step) {
            if (step % 1000 == 0)
                p_ins = 1.0 - p_ins;
            const uint64_t n = ref.size();
            const double x = static_cast<double>(r.below(1000)) / 1000.0;
            if (x < 0.15 && n > 0) {
                const uint64_t i = r.below(n);
                const bool v = r.next() & 1;
                CHECK_EQ(L.write(static_cast<uint32_t>(i), v), ref.write(i, v));
            } else if (r.chance(p_ins) && n < MB) {
                const uint64_t i = r.chance(0.2) ? (r.chance(0.5) ? 0 : n) : r.below(n + 1);
                const bool v = r.next() & 1;
                L.insert(static_cast<uint32_t>(i), v);
                ref.insert(i, v);
            } else if (n > 0) {
                const uint64_t i = r.chance(0.2) ? (r.chance(0.5) ? 0 : n - 1) : r.below(n);
                CHECK_EQ(L.erase(static_cast<uint32_t>(i)), ref.erase(i));
            }
            check_leaf(L, ref, r, step % 97 == 0);
            if (t::fail_count() > 0)
                return;
        }
        // llenarla completa y vaciarla
        while (ref.size() < MB) {
            const uint64_t i = r.below(ref.size() + 1);
            const bool v = r.next() & 1;
            L.insert(static_cast<uint32_t>(i), v);
            ref.insert(i, v);
        }
        check_leaf(L, ref, r, true);
        while (ref.size() > 0) {
            const uint64_t i = r.below(ref.size());
            CHECK_EQ(L.erase(static_cast<uint32_t>(i)), ref.erase(i));
        }
        check_leaf(L, ref, r, true);
        if (t::fail_count() > 0)
            return;
    }
}

int main() {
    t::install_abort_handler();
    t::rng r(t::env_u64("SEED", 4242));
    run<256>(r);
    run<8192>(r);
    return t::summary("leaf_bv_node");
}
