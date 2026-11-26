// Tests de static_bv_node (vista) y shared_buffer (almacenamiento) contra el bitvector de referencia.
// Cubre tamanos de borde (limites de palabra/bloque/superbloque), varias densidades y varios BlockFactor.
// Estos son los tests que deben seguir pasando cuando static_bv_node pase a ser una "vista".
#include <adaptive/bit_vector/shared_buffer.hpp>
#include <adaptive/bit_vector/static_bv_node.hpp>

#include "test_static_check.hpp"
#include "test_util.hpp"

using namespace adaptive;

template <uint32_t BF> static void run_all(t::rng& r) {
    std::vector<uint64_t> sizes = {1,   2,    63,    64,    65,    127,    128,    129,    255,   256,
                                   257, 511,  512,   513,   1000,  4096,   10007,  65535,  65536, 65537,
                                   65536 + 255, 131072, 200003};
    if (t::effort() >= 1)
        sizes.insert(sizes.end(), {300001, 524289});
    for (uint64_t n : sizes) {
        for (int kind = 0; kind < t::k_num_kinds; ++kind) {
            t::set_context("static_bv_node<" + std::to_string(BF) + "> n=" + std::to_string(n) + " patron=" +
                           t::kind_name(kind));
            if (t::verbose())
                std::fprintf(stderr, "%s\n", t::context().c_str());
            auto bits = t::make_bits(r, n, kind);
            t::ref_bv ref(bits);

            // constructor desde puntero (copia)
            auto words = t::pack(bits);
            static_bv_node<BF> a(words.data(), n);
            check_static(a, ref, r);

            // constructor desde vector&&
            auto words2 = t::pack(bits);
            static_bv_node<BF> b(std::move(words2), n);
            check_static(b, ref, r);
            CHECK_EQ(b.buffer().refcount(), 1); // la vista es unica duena de su buffer

            // el buffer directamente (ambos constructores)
            shared_buffer<BF> c(words.data(), n);
            check_static(c, ref, r);
            auto words3 = t::pack(bits);
            shared_buffer<BF> d(std::move(words3), n);
            check_static(d, ref, r);

            if (t::fail_count() > 0)
                return;
        }
    }
}

// Ciclo de vida: el buffer sobrevive mientras quede una referencia y se libera con la ultima
// (ASan/LeakSanitizer detectan use-after-free y fugas).
template <uint32_t BF> static void test_refcount(t::rng& r) {
    t::set_context("shared_buffer<" + std::to_string(BF) + "> refcount");
    const uint64_t n = 70001;
    auto bits = t::make_bits(r, n, t::k_random);
    t::ref_bv ref(bits);
    auto words = t::pack(bits);

    auto* b = new shared_buffer<BF>(words.data(), n);
    CHECK_EQ(b->refcount(), 1);
    b->acquire();
    b->acquire();
    CHECK_EQ(b->refcount(), 3);
    shared_buffer<BF>::release(b);
    shared_buffer<BF>::release(b);
    CHECK_EQ(b->refcount(), 1);
    check_static(*b, ref, r); // sigue vivo y correcto tras soltar dos referencias
    shared_buffer<BF>::release(b); // ultima: se destruye

    // una vista libera su buffer al destruirse; con otra referencia retenida el buffer sigue valido
    auto* kept = new shared_buffer<BF>(words.data(), n);
    kept->acquire(); // referencia "externa"
    shared_buffer<BF>::release(kept);
    check_static(*kept, ref, r);
    shared_buffer<BF>::release(kept);
}

int main() {
    t::install_abort_handler();
    t::rng r(t::env_u64("SEED", 777));
    test_refcount<4>(r);
    test_refcount<1>(r);
    run_all<4>(r);
    run_all<1>(r);
    run_all<2>(r);
    run_all<8>(r);
    return t::summary("static_bv_node + shared_buffer");
}
