// Tests del split SIN COPIA (paso 5): dynamic_bv_node construido desde una hoja estatica (vista).
//
// Verifica sobre un padre que es una vista PARCIAL en medio de un buffer (con bits vecinos a ambos lados):
//  - los hijos estaticos son vistas del mismo buffer, en las posiciones correctas, sin copiar bits;
//  - los hijos pequenos (hojas dinamicas) tienen exactamente sus bits y su conteo de unos. Los bits que siguen al
//    final de la vista son todos 1 a proposito: cualquier fuga de la "cola" de una palabra se veria en `ones`;
//  - el contenido completo, los tamanos y el refcount son los esperados.
// Este test NO depende de la correccion del constructor de leaf_bv_node (usa hojas construidas por el split).
#include <adaptive/bit_vector/bv_node_ptr.hpp>
#include <adaptive/bit_vector/dynamic_bv_node.hpp>
#include <adaptive/bit_vector/leaf_bv_node.hpp>
#include <adaptive/bit_vector/shared_buffer.hpp>
#include <adaptive/bit_vector/static_bv_node.hpp>

#include "test_util.hpp"

using namespace adaptive;

template <uint32_t Arity, uint32_t ML> struct types {
    using leaf = leaf_bv_node<ML>;
    using stat = static_bv_node<4>;
    class dyn;
    using node = bv_node_ptr<leaf, stat, dyn>;
    class dyn : public dynamic_bv_node<node, Arity> {
      public:
        using dynamic_bv_node<node, Arity>::dynamic_bv_node;
    };
};

template <uint32_t Arity, uint32_t ML>
static void one_case(t::rng& r, uint64_t level, bool is_root, uint64_t n, int kind, const std::string& ctx) {
    using T = types<Arity, ML>;
    using node = typename T::node;
    using stat = typename T::stat;
    t::set_context(ctx);
    if (t::verbose())
        std::fprintf(stderr, "%s\n", ctx.c_str());

    // buffer = [prefijo aleatorio][ la vista de n bits ][ sufijo de puros unos ]
    const uint64_t pre = 64 * (1 + r.below(30));
    const uint64_t post = 200 + r.below(300);
    std::vector<uint8_t> bits = t::make_bits(r, pre, t::k_random);
    const auto mid = t::make_bits(r, n, kind);
    bits.insert(bits.end(), mid.begin(), mid.end());
    bits.insert(bits.end(), post, 1);
    auto words = t::pack(bits);

    auto* buf = new shared_buffer<4>(words.data(), bits.size());
    stat view(buf, pre, n);
    shared_buffer<4>::release(buf); // solo la vista mantiene vivo el buffer
    CHECK_EQ(view.buffer().refcount(), 1);

    t::ref_bv ref(mid);
    node nd = node::dynamic(view, level, is_root);
    auto& d = nd.as_dynamic();
    CHECK_EQ(d.size(), n);
    CHECK_EQ(d.ones(), ref.ones()); // suma de los unos de los hijos
    CHECK(d.child_count() >= 2);

    // hijos: suman n; estaticos = vistas del mismo buffer sin copia; hojas con su conteo exacto
    uint64_t prefix = 0, static_children = 0;
    for (uint32_t k = 0; k < d.child_count(); ++k) {
        auto& c = d.child(k);
        const uint64_t len = c.size();
        uint64_t expect_ones = 0;
        for (uint64_t i = 0; i < len; ++i)
            expect_ones += mid[prefix + i];
        if (len < ML) {
            CHECK_EQ(c.as_leaf().ones(), expect_ones); // detecta bits de la cola contados de mas
            CHECK_EQ(c.as_leaf().size(), len);
        } else {
            ++static_children;
            auto& s = c.as_static();
            CHECK(&s.buffer() == &view.buffer());    // mismo buffer
            CHECK_EQ(s.begin(), pre + prefix);        // en la posicion correcta
            CHECK(s.data() == view.data() + prefix / 64); // apunta a los mismos bits: no hubo copia
            CHECK_EQ(s.ones(), expect_ones);
            CHECK_EQ(s.size(), len);
        }
        prefix += len;
    }
    CHECK_EQ(prefix, n);
    CHECK_EQ(view.buffer().refcount(), 1 + static_children);

    // contenido completo
    t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { d.read(a, l, o, ob); }, ref, 0, n, 0, r);
    for (int k = 0; k < 40; ++k) {
        const uint64_t i = r.below(n + 1);
        const uint64_t len = r.below(std::min<uint64_t>(n - i, 900) + 1);
        t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { d.read(a, l, o, ob); }, ref, i, len,
                      r.below(100), r);
    }

    nd.reset(); // destruye el dinamico: las vistas hijas sueltan sus referencias
    CHECK_EQ(view.buffer().refcount(), 1);
}

template <uint32_t Arity, uint32_t ML> static void run(t::rng& r) {
    const std::string nm = "split_views<" + std::to_string(Arity) + "," + std::to_string(ML) + ">";
    const int reps = t::effort() == 0 ? 6 : 20;
    const uint64_t max_bits = 6'000'000; // acota memoria/tiempo (la referencia usa 1 byte por bit)
    for (uint64_t level = 1; level <= 3; ++level) {
        uint64_t bound = ML; // weight_upperbound(level - 1)
        for (uint64_t i = 1; i < level; ++i)
            bound *= Arity;
        for (int rep = 0; rep < reps; ++rep) {
            const int kind = static_cast<int>(r.below(t::k_num_kinds));
            // nodo NO raiz: aridad fija; el padre mide entre Arity*bound/4 y Arity*bound (con margen)
            const uint64_t lo = Arity * bound / 4 + 64 * Arity;
            const uint64_t hi = std::min<uint64_t>(Arity * bound - 64 * Arity, max_bits);
            if (lo >= hi)
                continue; // nivel demasiado grande para este test
            const uint64_t n = lo + r.below(hi - lo + 1);
            one_case<Arity, ML>(r, level, false, n, kind,
                                nm + " no-raiz nivel=" + std::to_string(level) + " n=" + std::to_string(n) +
                                    " patron=" + t::kind_name(kind));
            if (t::fail_count() > 0)
                return;
        }
        for (int rep = 0; rep < reps; ++rep) {
            const int kind = static_cast<int>(r.below(t::k_num_kinds));
            // raiz: aridad segun n; n entre bound*3 y casi bound*Arity
            const uint64_t lo = 3 * bound;
            const uint64_t hi = std::min<uint64_t>(Arity * bound - 64 * Arity, max_bits);
            if (lo >= hi)
                continue;
            const uint64_t n = lo + r.below(hi - lo + 1);
            one_case<Arity, ML>(r, level, true, n, kind,
                                nm + " raiz nivel=" + std::to_string(level) + " n=" + std::to_string(n) +
                                    " patron=" + t::kind_name(kind));
            if (t::fail_count() > 0)
                return;
        }
    }
}

int main() {
    t::install_abort_handler();
    t::rng r(t::env_u64("SEED", 97531));
    run<16, 512>(r);
    run<32, 512>(r);
    run<16, 1024>(r);
    run<64, 8192>(r);
    return t::summary("split sin copia (dynamic_bv_node desde vista)");
}
