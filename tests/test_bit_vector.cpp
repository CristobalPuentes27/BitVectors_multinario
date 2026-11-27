// Test diferencial del bit_vector completo (arbol multiario adaptativo) contra un bitvector ingenuo.
//
// Idea: se aplica la misma secuencia aleatoria de operaciones a la estructura y a la referencia, y se compara
// todo lo observable (size, ones, access, rank0/1, select0/1, next0/1, read, valores de retorno de write/erase).
// Se alternan rafagas de actualizaciones y de consultas para forzar los dos mecanismos adaptativos:
//   - splitting: actualizar sobre una hoja estatica (split_static),
//   - flattening: muchas consultas sobre un subarbol sin actualizaciones (flatten).
// Se usan configuraciones con parametros chicos (hojas de 128-1024 bits, aridad 16-32) para llegar a 3-4
// niveles con vectores de pocos cientos de miles de bits, ademas de la configuracion por defecto.
//
// Este test NO depende de como se implementen las hojas estaticas: es el que debe seguir pasando al introducir
// buffers flotantes / vistas.
#include <adaptive/adaptive.hpp>

#include <sstream>

#include "test_util.hpp"

using namespace adaptive;

// ------------------------------------------------------------ harness
template <class BV> class harness {
  public:
    BV bv;
    t::ref_bv ref;
    t::rng r;
    uint64_t hot_center = 0;

    explicit harness(uint64_t seed) : r(seed) {}

    void init(std::vector<uint8_t> bits) {
        ref = t::ref_bv(bits);
        auto w = t::pack(bits);
        bv = BV(w.data(), bits.size());
    }

    void check_sizes() {
        CHECK_EQ(bv.size(), ref.size());
        CHECK_EQ(bv.ones(), ref.ones());
    }

    // Invariantes de la contabilidad de memoria (paso 6); validos en puntos estables (entre operaciones).
    void check_report() {
        const memory_report m = bv.report_memory();
        CHECK_EQ(m.leaf_bits + m.view_bits, ref.size()); // los bits se reparten entre hojas copiadas y vistas
        CHECK_EQ(m.buffer_refs, m.static_views);         // toda referencia a un buffer es una vista del arbol
        CHECK_EQ(m.live_bits, m.view_bits);              // contador de los buffers == suma por recorrido
        CHECK(m.live_bits <= m.buffer_bits);             // lo huerfano nunca es negativo
        const uint64_t sb = bv.space_bytes() - sizeof(bv); // space_bytes() reparte cada buffer entre sus vistas
        CHECK(sb <= m.total_bytes() && m.total_bytes() - sb <= m.static_views);
    }

    // ---- actualizaciones
    void do_insert(uint64_t i, bool v) {
        bv.insert(i, v);
        ref.insert(i, v);
        check_sizes();
    }
    void do_erase(uint64_t i) {
        const int d = bv.erase(i);
        CHECK_EQ(d, ref.erase(i));
        check_sizes();
    }
    void do_write(uint64_t i, bool v) {
        const int d = bv.write(i, v);
        CHECK_EQ(d, ref.write(i, v));
        check_sizes();
    }

    // ---- consultas
    void query_once() {
        const uint64_t n = ref.size();
        if (n == 0)
            return;
        switch (r.below(8)) {
        case 0: {
            const uint64_t i = r.below(n);
            CHECK_EQ(bv.access(i), ref.b[i]);
            break;
        }
        case 1: {
            const uint64_t i = r.below(n + 1);
            CHECK_EQ(bv.rank1(i), ref.rank1(i));
            break;
        }
        case 2: {
            const uint64_t i = r.below(n + 1);
            CHECK_EQ(bv.rank0(i), ref.rank0(i));
            break;
        }
        case 3:
            if (ref.ones() > 0) {
                const uint64_t j = 1 + r.below(ref.ones());
                CHECK_EQ(bv.select1(j), ref.select1(j));
            }
            break;
        case 4:
            if (ref.zeros() > 0) {
                const uint64_t j = 1 + r.below(ref.zeros());
                CHECK_EQ(bv.select0(j), ref.select0(j));
            }
            break;
        case 5: {
            const uint64_t i = r.below(n);
            CHECK_EQ(bv.next1(i), ref.next1(i));
            break;
        }
        case 6: {
            const uint64_t i = r.below(n);
            CHECK_EQ(bv.next0(i), ref.next0(i));
            break;
        }
        default: {
            const uint64_t i = r.below(n + 1);
            const uint64_t len = r.chance(0.1) ? r.below(n - i + 1) : r.below(std::min<uint64_t>(n - i, 3000) + 1);
            t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { bv.read(a, l, o, ob); }, ref, i, len,
                          r.below(130), r);
            break;
        }
        }
    }

    // consultas concentradas en una region (para provocar flatten de subarboles concretos)
    void query_region(uint64_t count, uint64_t lo, uint64_t width) {
        const uint64_t n = ref.size();
        if (n == 0)
            return;
        for (uint64_t k = 0; k < count; ++k) {
            const uint64_t i = std::min(n - 1, lo + r.below(width));
            if (k & 1)
                CHECK_EQ(bv.access(i), ref.b[i]);
            else
                CHECK_EQ(bv.rank1(i + 1), ref.rank1(i + 1));
        }
    }

    // verificacion exhaustiva (tambien dispara flattens por la cantidad de accesos)
    void verify_full(bool all_positions) {
        check_sizes();
        check_report();
        const uint64_t n = ref.size();
        if (n == 0)
            return;
        t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { bv.read(a, l, o, ob); }, ref, 0, n, 0,
                      r);
        const uint64_t cap = all_positions ? n + 1 : 4000;
        for (uint64_t i : t::points(r, 0, n - 1, cap)) {
            CHECK_EQ(bv.access(i), ref.b[i]);
            CHECK_EQ(bv.next1(i), ref.next1(i));
            CHECK_EQ(bv.next0(i), ref.next0(i));
        }
        for (uint64_t i : t::points(r, 0, n, cap)) {
            CHECK_EQ(bv.rank1(i), ref.rank1(i));
            CHECK_EQ(bv.rank0(i), ref.rank0(i));
        }
        if (ref.ones() > 0)
            for (uint64_t j : t::points(r, 1, ref.ones(), cap))
                CHECK_EQ(bv.select1(j), ref.select1(j));
        if (ref.zeros() > 0)
            for (uint64_t j : t::points(r, 1, ref.zeros(), cap))
                CHECK_EQ(bv.select0(j), ref.select0(j));
    }

    bool ok() const { return t::fail_count() == 0; }
};

// ---------------------------------------------------- mezclas de carga
enum pos_mode { pos_uniform, pos_front, pos_back, pos_hotspot };

struct mix {
    const char* name;
    double p_insert, p_erase; // el resto es write
    pos_mode pos;
    uint64_t max_update_burst;
    uint64_t max_query_burst;
};

static uint64_t pick_pos(t::rng& r, pos_mode m, uint64_t count, uint64_t& hot_center) {
    // devuelve una posicion en [0, count)
    if (count == 0)
        return 0;
    switch (m) {
    case pos_uniform:
        return r.below(count);
    case pos_front:
        return r.below(std::max<uint64_t>(1, std::min<uint64_t>(count, 1 + count / 100)));
    case pos_back: {
        const uint64_t w = std::max<uint64_t>(1, std::min<uint64_t>(count, 1 + count / 100));
        return count - 1 - r.below(w);
    }
    case pos_hotspot: {
        if (r.chance(0.002))
            hot_center = r.below(count);
        const uint64_t w = std::min<uint64_t>(count, 700);
        const uint64_t lo = std::min(hot_center, count - w);
        return lo + r.below(w);
    }
    }
    return 0;
}

static const mix mixes[] = {
    {"balanceada", 0.40, 0.40, pos_uniform, 40, 400},
    {"crece", 0.80, 0.10, pos_uniform, 60, 200},
    {"decrece", 0.10, 0.80, pos_uniform, 60, 200},
    {"frente", 0.45, 0.45, pos_front, 50, 300},
    {"fondo", 0.45, 0.45, pos_back, 50, 300},
    {"hotspot", 0.45, 0.45, pos_hotspot, 80, 600},
    {"muchas_consultas", 0.45, 0.45, pos_uniform, 3, 3000},
    // sin borrados no hay underflow ni merge_with: no activa el bug conocido de leaf_bv_node (ver known_issues)
    {"sin_borrados", 0.70, 0.00, pos_uniform, 60, 300},
    {"sin_borrados_hotspot", 0.70, 0.00, pos_hotspot, 80, 600},
};

template <class BV>
static void run_scenario(const std::string& cfg, uint64_t seed, uint64_t n0, int kind, const mix& m, uint64_t theta,
                         int phases) {
    const std::string ctx = cfg + " seed=" + std::to_string(seed) + " n0=" + std::to_string(n0) +
                            " patron=" + t::kind_name(kind) + " mezcla=" + m.name + " theta=" + std::to_string(theta);
    t::set_context(ctx);
    if (t::verbose())
        std::fprintf(stderr, "%s\n", ctx.c_str());

    harness<BV> h(seed);
    h.init(t::make_bits(h.r, n0, kind));
    h.bv.set_theta(theta);
    h.verify_full(false);

    const int before = t::fail_count();
    for (int phase = 0; phase < phases && t::fail_count() == before; ++phase) {
        t::set_context(ctx + " fase=" + std::to_string(phase));
        // --- rafaga de actualizaciones
        const uint64_t ub = 1 + h.r.below(m.max_update_burst);
        for (uint64_t k = 0; k < ub; ++k) {
            const uint64_t n = h.ref.size();
            const double x = static_cast<double>(h.r.below(10000)) / 10000.0;
            if (x < m.p_insert) {
                h.do_insert(pick_pos(h.r, m.pos, n + 1, h.hot_center), h.r.next() & 1);
            } else if (x < m.p_insert + m.p_erase) {
                if (n > 1)
                    h.do_erase(pick_pos(h.r, m.pos, n, h.hot_center));
            } else if (n > 0) {
                h.do_write(pick_pos(h.r, m.pos, n, h.hot_center), h.r.next() & 1);
            }
        }
        h.check_report();
        // --- rafaga de consultas (variadas y, a veces, concentradas en una region)
        const uint64_t qb = h.r.below(m.max_query_burst + 1);
        if (h.r.chance(0.4)) {
            const uint64_t n = h.ref.size();
            h.query_region(qb, h.r.below(std::max<uint64_t>(1, n)), 1 + h.r.below(2000));
        } else {
            for (uint64_t k = 0; k < qb; ++k)
                h.query_once();
        }
        if (phase % 25 == 24)
            h.verify_full(false);
    }
    h.verify_full(true);
}

// ------------------------------------------------- escenarios especiales
// Propiedad exacta del split sin copia: partiendo de UN buffer, escribir (sin consultas: no hay flatten) solo
// convierte rangos de vistas en hojas copiadas. Nunca se crea otro buffer y los bits de las hojas son exactamente
// la memoria huerfana del buffer original.
template <class BV> static void scenario_single_buffer(const std::string& cfg, uint64_t seed, uint64_t n0) {
    t::set_context(cfg + " un solo buffer seed=" + std::to_string(seed) + " n0=" + std::to_string(n0));
    harness<BV> h(seed);
    h.init(t::make_bits(h.r, n0, t::k_random));
    {
        const memory_report m = h.bv.report_memory();
        CHECK_EQ(m.buffers, 1);
        CHECK_EQ(m.static_views, 1);
        CHECK_EQ(m.dead_bits(), 0);
        CHECK_EQ(m.leaf_bits, 0);
    }
    uint64_t prev_dead = 0;
    for (int k = 0; k < 60 && h.ok(); ++k) {
        h.do_write(h.r.below(h.ref.size()), h.r.next() & 1);
        const memory_report m = h.bv.report_memory();
        if (m.buffers == 0) {
            // cuando muere la ultima vista, el buffer se libera solo (refcount): no queda memoria huerfana
            CHECK_EQ(m.view_bits, 0);
            CHECK_EQ(m.leaf_bits, n0);
            h.check_report();
            break;
        }
        CHECK_EQ(m.buffers, 1);                  // ningun split creo un buffer nuevo
        CHECK_EQ(m.dead_bits(), m.leaf_bits);    // lo huerfano son exactamente los bits ya copiados a hojas
        CHECK_EQ(m.live_bits, n0 - m.leaf_bits);
        CHECK(m.dead_bits() >= prev_dead);       // solo crece: no hay flatten ni compactacion
        prev_dead = m.dead_bits();
        h.check_report();
    }
    CHECK(prev_dead > 0); // al menos un rango paso a hoja dinamica
    h.verify_full(false);
}

template <class BV> static void scenario_grow_from_empty(const std::string& cfg, uint64_t seed, uint64_t target) {
    t::set_context(cfg + " crecer desde vacio seed=" + std::to_string(seed));
    harness<BV> h(seed);
    h.check_sizes();
    for (uint64_t k = 0; k < target && h.ok(); ++k) {
        h.do_insert(h.r.below(h.ref.size() + 1), h.r.next() & 1);
        if (k % 50 == 0)
            for (int q = 0; q < 10; ++q)
                h.query_once();
        if (k % 5000 == 4999)
            h.verify_full(false);
    }
    h.verify_full(true);
}

template <class BV> static void scenario_shrink_to_empty(const std::string& cfg, uint64_t seed, uint64_t n0) {
    t::set_context(cfg + " vaciar por completo seed=" + std::to_string(seed) + " n0=" + std::to_string(n0));
    harness<BV> h(seed);
    h.init(t::make_bits(h.r, n0, t::k_random));
    uint64_t step = 0;
    while (h.ref.size() > 0 && h.ok()) {
        const uint64_t n = h.ref.size();
        const uint64_t i = (step % 3 == 0) ? 0 : (step % 3 == 1 ? n - 1 : h.r.below(n));
        h.do_erase(i);
        if (step++ % 97 == 0)
            for (int q = 0; q < 5; ++q)
                h.query_once();
        if (step % 4000 == 0)
            h.verify_full(false);
    }
    h.check_sizes();
    CHECK(h.bv.empty());
    // y puede volver a usarse tras quedar vacio
    for (int k = 0; k < 2000 && h.ok(); ++k)
        h.do_insert(h.r.below(h.ref.size() + 1), h.r.next() & 1);
    h.verify_full(true);
}

template <class BV> static void scenario_sequential(const std::string& cfg, uint64_t seed, uint64_t count) {
    // inserciones solo al final, solo al principio, y luego borrados solo del principio / del final
    t::set_context(cfg + " secuencial seed=" + std::to_string(seed));
    harness<BV> h(seed);
    h.init(t::make_bits(h.r, 3000, t::k_random));
    for (uint64_t k = 0; k < count && h.ok(); ++k)
        h.do_insert(h.ref.size(), h.r.next() & 1);
    h.verify_full(false);
    for (uint64_t k = 0; k < count && h.ok(); ++k)
        h.do_insert(0, h.r.next() & 1);
    h.verify_full(false);
    for (uint64_t k = 0; k < count && h.ok(); ++k)
        h.do_erase(0);
    h.verify_full(false);
    for (uint64_t k = 0; k < count && h.ok(); ++k)
        h.do_erase(h.ref.size() - 1);
    h.verify_full(true);
}

// muchos ciclos: consultas masivas (flatten) seguidas de unas pocas actualizaciones (split)
template <class BV> static void scenario_flatten_split_cycles(const std::string& cfg, uint64_t seed, uint64_t n0) {
    t::set_context(cfg + " ciclos flatten/split seed=" + std::to_string(seed) + " n0=" + std::to_string(n0));
    harness<BV> h(seed);
    h.init(t::make_bits(h.r, n0, t::k_random));
    h.bv.set_theta(100000); // flatten muy agresivo
    const int cycles = t::effort() == 0 ? 10 : 40;
    for (int c = 0; c < cycles && h.ok(); ++c) {
        t::set_context(cfg + " ciclos flatten/split ciclo=" + std::to_string(c));
        for (uint64_t k = 0; k < 4 * n0 / 64 + 500; ++k)
            h.query_once();
        for (int k = 0; k < 30; ++k) {
            const uint64_t n = h.ref.size();
            switch (h.r.below(3)) {
            case 0: h.do_insert(h.r.below(n + 1), h.r.next() & 1); break;
            case 1: if (n > 1) h.do_erase(h.r.below(n)); break;
            default: h.do_write(h.r.below(n), h.r.next() & 1); break;
            }
        }
    }
    h.verify_full(true);
}

template <class BV> static void scenario_copy_serialize(const std::string& cfg, uint64_t seed, uint64_t n0) {
    t::set_context(cfg + " copia/serializar seed=" + std::to_string(seed));
    harness<BV> h(seed);
    h.init(t::make_bits(h.r, n0, t::k_random));
    h.bv.set_theta(321);
    for (int k = 0; k < 3000; ++k) {
        const uint64_t n = h.ref.size();
        if (h.r.chance(0.5))
            h.do_insert(h.r.below(n + 1), h.r.next() & 1);
        else if (n > 1)
            h.do_erase(h.r.below(n));
        if (k % 10 == 0)
            h.query_once();
    }
    // copia: mismo contenido; independiente del original
    BV copy(h.bv);
    t::ref_bv ref_copy = h.ref;
    {
        const uint64_t n = ref_copy.size();
        CHECK_EQ(copy.size(), n);
        CHECK_EQ(copy.ones(), ref_copy.ones());
        t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { copy.read(a, l, o, ob); }, ref_copy, 0, n,
                      0, h.r);
        for (int k = 0; k < 200; ++k) {
            const uint64_t i = h.r.below(n);
            CHECK_EQ(copy.access(i), ref_copy.b[i]);
            CHECK_EQ(copy.rank1(i + 1), ref_copy.rank1(i + 1));
        }
        copy.insert(0, true); // modificar la copia no debe afectar al original
        CHECK_EQ(h.bv.size(), n);
        h.verify_full(false);
        copy.erase(0);
    }
    // serializar y cargar
    std::stringstream ss;
    const uint64_t written = h.bv.serialize(ss);
    CHECK_EQ(written, ss.str().size());
    BV loaded;
    loaded.load(ss);
    CHECK_EQ(loaded.size(), h.ref.size());
    CHECK_EQ(loaded.ones(), h.ref.ones());
    CHECK_EQ(loaded.theta(), 321);
    t::check_read([&](uint64_t a, uint64_t l, uint64_t* o, uint64_t ob) { loaded.read(a, l, o, ob); }, h.ref, 0,
                  h.ref.size(), 0, h.r);
    // el cargado sigue siendo plenamente funcional
    h.bv = std::move(loaded);
    for (int k = 0; k < 2000 && h.ok(); ++k) {
        const uint64_t n = h.ref.size();
        if (h.r.chance(0.5))
            h.do_insert(h.r.below(n + 1), h.r.next() & 1);
        else if (n > 1)
            h.do_erase(h.r.below(n));
        h.query_once();
    }
    h.verify_full(true);
}

// ----------------------------------------------------------- suites
template <class BV>
static void run_suite(const std::string& cfg, std::vector<uint64_t> sizes, int phases, uint64_t grow_target,
                      uint64_t seq_count) {
    const int eff = t::effort();
    const int seeds = eff == 0 ? 1 : 2 * eff;
    const uint64_t thetas[] = {1, 100, 1000, 100000};
    uint64_t counter = 0;
    // ONLY_MIX=<nombre>: solo esa mezcla (y sin los escenarios especiales, que si borran). Util para aislar.
    const char* only_mix = std::getenv("ONLY_MIX");
    for (int s = 0; s < seeds; ++s) {
        const uint64_t seed = t::env_u64("SEED", 1000) + static_cast<uint64_t>(s) * 7919;
        for (uint64_t n0 : sizes) {
            for (const mix& m : mixes) {
                if (only_mix && std::string(only_mix) != m.name)
                    continue;
                // rotamos patron de bits y theta para cubrir combinaciones sin explosion combinatoria
                const int kind = static_cast<int>((counter) % t::k_num_kinds);
                const uint64_t theta = thetas[(counter / 3) % 4];
                ++counter;
                run_scenario<BV>(cfg, seed + counter, n0, kind, m, theta, phases);
                if (t::fail_count() > 0)
                    return;
            }
        }
        if (only_mix)
            continue;
        scenario_single_buffer<BV>(cfg, seed, sizes.back());
        scenario_grow_from_empty<BV>(cfg, seed, grow_target);
        scenario_shrink_to_empty<BV>(cfg, seed, sizes[sizes.size() / 2]);
        scenario_sequential<BV>(cfg, seed, seq_count);
        scenario_flatten_split_cycles<BV>(cfg, seed, sizes.back() / 2);
        scenario_copy_serialize<BV>(cfg, seed, sizes[sizes.size() / 2]);
        if (t::fail_count() > 0)
            return;
    }
    std::printf("   suite %-28s ok\n", cfg.c_str());
    std::fflush(stdout);
}

int main() {
    t::install_abort_handler();
    const bool quick = t::effort() == 0;
    const int phases = quick ? 40 : 120;

    // NOTA sobre las configuraciones: el constructor de dynamic_bv_node asume (ADAPTIVE_ASSUME) que los hijos
    // creados al partir una hoja estatica miden al menos child_bound/4 bits. Con hojas de <=256 bits la
    // granularidad de palabra (64 bits) rompe esa suposicion aunque el codigo sea correcto para tamanos reales,
    // por eso las hojas de prueba son de >= 512 bits.

    // niveles (hojas 512 bits, aridad 16): 512, 8192, 131072, 2097152
    using cfg_a = bit_vector<16, 512, 10>;
    run_suite<cfg_a>("bit_vector<16,512,10>",
                     {1, 511, 512, 513, 1000, 8191, 8192, 8193, 40000, 131071, 131072, 131073, 300000}, phases,
                     quick ? 8000 : 30000, quick ? 2000 : 8000);

    // epsilon bajo => flatten mas permisivo (subarboles de hasta size/4). Niveles: 1024, 16384, 262144
    using cfg_eps = bit_vector<16, 1024, 4>;
    run_suite<cfg_eps>("bit_vector<16,1024,4>", {1023, 1024, 1025, 16383, 16384, 16385, 100000, 270000}, phases,
                       quick ? 8000 : 30000, quick ? 2000 : 8000);

    // aridad 32. Niveles: 512, 16384, 524288
    using cfg_32 = bit_vector<32, 512, 10>;
    run_suite<cfg_32>("bit_vector<32,512,10>", {511, 512, 513, 16383, 16384, 16385, 100000, 540000}, phases,
                      quick ? 8000 : 30000, quick ? 2000 : 8000);

    // configuracion por defecto (aridad 64, hojas de 8192 bits): niveles en 8192, 524288, 33554432
    using cfg_default = bit_vector<>;
    run_suite<cfg_default>("bit_vector<> (por defecto)",
                           {1, 8191, 8192, 8193, 100000, 524287, 524288, 524289, 1500000}, quick ? 25 : 60,
                           quick ? 12000 : 40000, quick ? 3000 : 10000);

    return t::summary("bit_vector");
}
