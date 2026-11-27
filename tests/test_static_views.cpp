// Tests de static_bv_node como VISTA parcial [begin, begin+len) sobre un shared_buffer compartido (paso 4).
//
// Cada vista se compara contra una referencia construida solo con su subrango, por lo que se verifican
// access/rank/select/next/read con los desplazamientos begin y base_ones, incluyendo los bordes:
// vistas que empiezan en 0, que terminan exactamente en el final del buffer, de 1 bit, y que cruzan
// limites de palabra, bloque y superbloque (65536 bits).
#include <adaptive/bit_vector/shared_buffer.hpp>
#include <adaptive/bit_vector/static_bv_node.hpp>

#include <cstdlib>
#include <memory>
#include <utility>

#include "test_static_check.hpp"
#include "test_util.hpp"

using namespace adaptive;

using range = std::pair<uint64_t, uint64_t>; // (begin, len)

static std::vector<range> make_ranges(t::rng& r, uint64_t n) {
    std::vector<range> v;
    v.push_back({0, n});
    if (n > 1) {
        v.push_back({0, 1});
        v.push_back({n - 1, 1});
        v.push_back({0, n - 1});
        v.push_back({1, n - 1});
        v.push_back({n / 2, n - n / 2});
    }
    // alrededor de limites estructurales: empezando en ellos y terminando en ellos
    for (uint64_t m : {uint64_t{64}, uint64_t{256}, uint64_t{1024}, uint64_t{65536}}) {
        for (int d = -1; d <= 1; ++d) {
            const uint64_t edge = m + d;
            if (edge < n) { // empieza en el limite
                v.push_back({edge, 1 + r.below(std::min<uint64_t>(n - edge, 700))});
            }
            if (edge >= 1 && edge <= n) { // termina en el limite
                const uint64_t len = 1 + r.below(std::min<uint64_t>(edge, 700));
                v.push_back({edge - len, len});
            }
        }
    }
    // aleatorias, mayormente chicas
    for (int k = 0; k < 12; ++k) {
        const uint64_t b = r.below(n);
        const uint64_t max_len = r.chance(0.7) ? std::min<uint64_t>(n - b, 600) : n - b;
        v.push_back({b, 1 + r.below(max_len)});
    }
    // un par de vistas grandes que cruzan varios superbloques
    if (n > 3) {
        for (int k = 0; k < 2; ++k) {
            const uint64_t len = n / 3 + r.below(n - n / 3);
            v.push_back({r.below(n - len + 1), len});
        }
    }
    return v;
}

static std::vector<uint8_t> slice(const std::vector<uint8_t>& bits, uint64_t begin, uint64_t len) {
    return std::vector<uint8_t>(bits.begin() + static_cast<std::ptrdiff_t>(begin),
                                bits.begin() + static_cast<std::ptrdiff_t>(begin + len));
}

template <uint32_t BF>
static void check_view(const static_bv_node<BF>& v, const std::vector<uint8_t>& bits, uint64_t begin, uint64_t len,
                       t::rng& r) {
    t::ref_bv ref(slice(bits, begin, len));
    CHECK_EQ(v.begin(), begin);
    CHECK_EQ(v.size(), len);
    check_static(v, ref, r);
}

template <uint32_t BF> static void run(t::rng& r) {
    const std::string nm = "static_bv_node<" + std::to_string(BF) + "> vistas";
    std::vector<uint64_t> sizes = {1, 2, 65, 1000, 65536 + 3, 131072 + 777};
    if (t::effort() >= 1)
        sizes.push_back(300001);

    for (uint64_t n : sizes) {
        for (int kind = 0; kind < t::k_num_kinds; ++kind) {
            const std::string base_ctx = nm + " n=" + std::to_string(n) + " patron=" + t::kind_name(kind);
            t::set_context(base_ctx);
            if (t::verbose())
                std::fprintf(stderr, "%s\n", base_ctx.c_str());
            auto bits = t::make_bits(r, n, kind);
            auto words = t::pack(bits);
            auto* buf = new shared_buffer<BF>(words.data(), n);

            // 1) vistas sobre rangos arbitrarios del mismo buffer
            for (auto [b, l] : make_ranges(r, n)) {
                t::set_context(base_ctx + " vista(" + std::to_string(b) + "," + std::to_string(l) + ")");
                static_bv_node<BF> v(buf, b, l);
                check_view(v, bits, b, l, r);
                CHECK_EQ(buf->refcount(), 2); // creador + esta vista
                CHECK_EQ(buf->live_bits(), l);       // contabilidad: solo esta vista cubre bits
                CHECK_EQ(buf->dead_bits(), n - l);
                if (b % 64 == 0) { // data() apunta a la palabra donde empieza la vista
                    bool ok = true;
                    for (uint64_t k = 0; k < l / 64 && ok; ++k)
                        ok = v.data()[k] == words[b / 64 + k];
                    CHECK(ok);
                }
                if (t::fail_count() > 0)
                    return;
            }
            CHECK_EQ(buf->refcount(), 1); // todas las vistas destruidas
            CHECK_EQ(buf->live_bits(), 0);
            CHECK_EQ(buf->dead_bits(), n);

            // 2) subvistas anidadas (vista de una vista) y vista de rango completo
            {
                t::set_context(base_ctx + " subvistas anidadas");
                static_bv_node<BF> whole(buf, 0, n);
                for (int k = 0; k < 10; ++k) {
                    const uint64_t b1 = r.below(n);
                    const uint64_t l1 = 1 + r.below(n - b1);
                    auto v = whole.subview(b1, l1);
                    const uint64_t b2 = r.below(l1);
                    const uint64_t l2 = 1 + r.below(l1 - b2);
                    auto w = v.subview(b2, l2);
                    check_view(v, bits, b1, l1, r);
                    check_view(w, bits, b1 + b2, l2, r);
                    CHECK_EQ(buf->refcount(), 4); // creador + whole + v + w
                    CHECK_EQ(buf->live_bits(), n + l1 + l2); // las vistas pueden solaparse: cada una suma su largo
                }
                if (t::fail_count() > 0)
                    return;
            }
            CHECK_EQ(buf->refcount(), 1);
            CHECK_EQ(buf->live_bits(), 0);

            // 3) particion en k vistas contiguas (lo que hara split_static); el creador suelta el buffer primero
            {
                t::set_context(base_ctx + " particion");
                const uint64_t k = std::min<uint64_t>(n, 2 + r.below(40));
                std::vector<uint64_t> cuts = {0, n};
                while (cuts.size() < k + 1) {
                    const uint64_t c = 1 + r.below(n - 1 > 0 ? n - 1 : 1);
                    if (c < n && std::find(cuts.begin(), cuts.end(), c) == cuts.end())
                        cuts.push_back(c);
                    if (n == 1)
                        break;
                }
                std::sort(cuts.begin(), cuts.end());

                std::vector<std::unique_ptr<static_bv_node<BF>>> parts;
                std::vector<range> where;
                uint64_t total_len = 0, total_ones = 0;
                for (size_t i = 0; i + 1 < cuts.size(); ++i) {
                    where.push_back({cuts[i], cuts[i + 1] - cuts[i]});
                    parts.push_back(std::make_unique<static_bv_node<BF>>(buf, cuts[i], cuts[i + 1] - cuts[i]));
                    total_len += parts.back()->size();
                    total_ones += parts.back()->ones();
                }
                CHECK_EQ(total_len, n); // cubren todo el buffer sin solaparse
                CHECK_EQ(total_ones, buf->ones());
                CHECK_EQ(buf->refcount(), 1 + parts.size());
                CHECK_EQ(buf->live_bits(), n); // la particion cubre todo: nada huerfano
                CHECK_EQ(buf->dead_bits(), 0);

                shared_buffer<BF>::release(buf); // el creador se va; las vistas mantienen vivo el buffer
                buf = nullptr;
                CHECK_EQ(parts[0]->buffer().refcount(), parts.size());

                // contabilidad de espacio: con k vistas, la suma cuenta el buffer una sola vez
                const uint64_t buffer_bytes = parts[0]->buffer().space_bytes();
                uint64_t sum = 0;
                for (auto& p : parts)
                    sum += p->space_bytes();
                const uint64_t expected = parts.size() * sizeof(static_bv_node<BF>) + buffer_bytes;
                CHECK(sum <= expected && expected - sum < parts.size());

                // las vistas funcionan tras irse el creador, y se destruyen en orden aleatorio;
                // cada vista destruida deja su rango como memoria huerfana mientras otras sigan vivas
                uint64_t destroyed_bits = 0;
                while (!parts.empty()) {
                    const size_t idx = r.below(parts.size());
                    CHECK_EQ(parts[idx]->buffer().refcount(), parts.size());
                    CHECK_EQ(parts[idx]->buffer().live_bits(), n - destroyed_bits);
                    CHECK_EQ(parts[idx]->buffer().dead_bits(), destroyed_bits);
                    destroyed_bits += where[idx].second;
                    check_view(*parts[idx], bits, where[idx].first, where[idx].second, r);
                    parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(idx));
                    where.erase(where.begin() + static_cast<std::ptrdiff_t>(idx));
                    if (t::fail_count() > 0)
                        return;
                }
                // al destruir la ultima vista el buffer se libera (LeakSanitizer lo comprueba)
            }
        }
    }
}

// El dueno original (constructor con words) se destruye antes que las vistas derivadas de el.
template <uint32_t BF> static void test_owner_dies_first(t::rng& r) {
    t::set_context("static_bv_node<" + std::to_string(BF) + "> el dueno muere primero");
    const uint64_t n = 131072 + 99;
    auto bits = t::make_bits(r, n, t::k_random);
    auto words = t::pack(bits);
    std::unique_ptr<static_bv_node<BF>> a, b;
    {
        static_bv_node<BF> owner(words.data(), n);
        a.reset(new static_bv_node<BF>(owner.subview(0, 70000)));
        b.reset(new static_bv_node<BF>(owner.subview(70000, n - 70000)));
        CHECK_EQ(owner.buffer().refcount(), 3);
    } // owner destruido
    CHECK_EQ(a->buffer().refcount(), 2);
    check_view(*a, bits, 0, 70000, r);
    check_view(*b, bits, 70000, n - 70000, r);
}

int main() {
    t::install_abort_handler();
    t::rng r(t::env_u64("SEED", 2468));
    test_owner_dies_first<4>(r);
    run<4>(r);
    run<1>(r);
    if (t::effort() >= 1) {
        run<2>(r);
        run<8>(r);
    }
    return t::summary("static_bv_node (vistas parciales)");
}
