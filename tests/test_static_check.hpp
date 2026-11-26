// Chequeo generico de una estructura estatica (static_bv_node, shared_buffer o una vista) contra la referencia.
#pragma once

#include <algorithm>

#include "test_util.hpp"

// Sirve tanto para static_bv_node como para shared_buffer (misma interfaz de consulta).
template <class S> inline void check_static(const S& s, const t::ref_bv& ref, t::rng& r) {
    const uint64_t n = ref.size();
    CHECK_EQ(s.size(), n);
    CHECK_EQ(s.ones(), ref.ones());

    for (uint64_t i : t::points(r, 0, n - 1, 20000))
        CHECK_EQ(s.access(i), ref.b[i]);

    for (uint64_t i : t::points(r, 0, n, 20000)) {
        CHECK_EQ(s.template rank<true>(i), ref.rank1(i));
        CHECK_EQ(s.template rank<false>(i), ref.rank0(i));
    }
    if (ref.ones() > 0)
        for (uint64_t j : t::points(r, 1, ref.ones(), 20000))
            CHECK_EQ(s.template select<true>(j), ref.select1(j));
    if (ref.zeros() > 0)
        for (uint64_t j : t::points(r, 1, ref.zeros(), 20000))
            CHECK_EQ(s.template select<false>(j), ref.select0(j));

    for (uint64_t i : t::points(r, 0, n - 1, 5000)) {
        CHECK_EQ(s.template next<true>(i), ref.next1(i));
        CHECK_EQ(s.template next<false>(i), ref.next0(i));
    }

    for (int k = 0; k < 150; ++k) {
        const uint64_t i = r.below(n + 1);
        const uint64_t len = r.chance(0.2) ? r.below(n - i + 1) : r.below(std::min<uint64_t>(n - i, 400) + 1);
        t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { s.read(a, l, o, ob); }, ref, i, len,
                      r.below(130), r);
    }
    t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { s.read(a, l, o, ob); }, ref, 0, n, 0, r);
}

