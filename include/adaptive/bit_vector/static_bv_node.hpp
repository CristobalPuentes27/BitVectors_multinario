/*

adaptive -- an implementation of adaptive dynamic bitvectors and packed arrays.
Copyright (C) 2024 Gonzalo Navarro
Copyright (C) 2026 Diego Arias

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

Author's contact: Gonzalo Navarro, Dept. of Computer Science, University of
Chile. Beauchef 851, Santiago, Chile. gnavarro@dcc.uchile.cl

*/

#pragma once

#include <cstdint>
#include <vector>

#include "adaptive/assert.hpp"
#include "adaptive/bit_vector/shared_buffer.hpp"

namespace adaptive {

// static_bv_node: hoja estatica. Es una VISTA sobre el rango [begin, begin+len) de un shared_buffer.
//
// El buffer guarda los bits y los directorios de rank/select en coordenadas globales. La vista agrega:
//   begin_      posicion global donde empieza la vista,
//   len_        cantidad de bits de la vista,
//   base_ones_  unos del buffer ANTES de begin_ (rank global en begin_),
//   ones_       unos dentro de la vista.
// Con eso todas las consultas se traducen en O(1) sin tocar los directorios:
//   rank1(i)   = buffer.rank1(begin + i) - base_ones
//   select1(j) = buffer.select1(base_ones + j) - begin
//   select0(j) = buffer.select0((begin - base_ones) + j) - begin
//   next(i)    = buffer.next(begin + i), descartando lo que cae fuera de [begin, begin+len)
//
// Las vistas de un mismo buffer son independientes entre si; el buffer se libera cuando se destruye la ultima.
// El arbol aun crea solo vistas de rango completo (begin = 0, len = size del buffer): el comportamiento del
// arbol no cambia hasta que split_static empiece a crear vistas parciales (paso 5).
template <uint32_t BlockFactor = 4> class static_bv_node {
  public:
    using buffer_type = shared_buffer<BlockFactor>;

    static constexpr uint32_t block_factor = buffer_type::block_factor;
    static constexpr uint32_t word_bits = buffer_type::word_bits;
    static constexpr uint32_t w16 = buffer_type::w16;
    static constexpr uint32_t superblock_bits = buffer_type::superblock_bits;
    static constexpr uint32_t block_bits = buffer_type::block_bits;

    // Dueno de un buffer nuevo: copian los bits (igual que antes) y la vista cubre todo el buffer.
    static_bv_node(const uint64_t* words, uint64_t size);
    static_bv_node(std::vector<uint64_t>&& words, uint64_t size);

    // Vista sobre [begin, begin+len) de un buffer existente. Comparte el buffer (refcount++); quien llama
    // conserva su propia referencia y debe soltarla con buffer_type::release si ya no la necesita.
    static_bv_node(buffer_type* buffer, uint64_t begin, uint64_t len);

    ~static_bv_node();

    // La propiedad del buffer es por refcount; las copias se hacen explicitamente con subview().
    static_bv_node(const static_bv_node&) = delete;
    static_bv_node& operator=(const static_bv_node&) = delete;

    // Nueva vista sobre [begin, begin+len) de ESTA vista (coordenadas locales). Comparte el buffer.
    static_bv_node subview(uint64_t begin, uint64_t len) const;

    uint64_t size() const;
    uint64_t ones() const;
    // El buffer se reparte en partes iguales entre las referencias que lo comparten: si todas las referencias
    // son vistas del arbol, la suma sobre las vistas cuenta el buffer exactamente una vez (huerfanos incluidos).
    uint64_t space_bytes() const;
    // Puntero a la palabra donde empieza la vista. Requiere begin alineado a palabra. Los bits mas alla de
    // size() en la ultima palabra pertenecen a otras vistas y NO estan enmascarados.
    const uint64_t* data() const;
    const buffer_type& buffer() const;
    uint64_t begin() const;
    uint64_t base_ones() const;

    bool access(uint64_t i) const;
    void read(uint64_t i, uint64_t len, uint64_t* out, uint64_t out_bit) const;

    template <bool Bit> uint64_t rank(uint64_t i) const;
    template <bool Bit> uint64_t select(uint64_t j) const;
    template <bool Bit> int64_t next(uint64_t i) const;

  private:
    buffer_type* buffer_;
    uint64_t begin_;
    uint64_t len_;
    uint64_t base_ones_;
    uint64_t ones_;
};

template <uint32_t BlockFactor>
static_bv_node<BlockFactor>::static_bv_node(const uint64_t* words, uint64_t size)
    : buffer_(new buffer_type(words, size)), begin_(0), len_(size), base_ones_(0), ones_(buffer_->ones()) {}

template <uint32_t BlockFactor>
static_bv_node<BlockFactor>::static_bv_node(std::vector<uint64_t>&& words, uint64_t size)
    : buffer_(new buffer_type(std::move(words), size)), begin_(0), len_(size), base_ones_(0),
      ones_(buffer_->ones()) {}

template <uint32_t BlockFactor>
static_bv_node<BlockFactor>::static_bv_node(buffer_type* buffer, uint64_t begin, uint64_t len)
    : buffer_(buffer), begin_(begin), len_(len), base_ones_(0), ones_(0) {
    ADAPTIVE_ASSUME(buffer != nullptr);
    ADAPTIVE_ASSUME(len > 0);
    ADAPTIVE_ASSUME(begin <= buffer->size() && len <= buffer->size() - begin);

    buffer_->acquire();
    base_ones_ = buffer_->template rank<true>(begin_);
    ones_ = buffer_->template rank<true>(begin_ + len_) - base_ones_;
}

template <uint32_t BlockFactor> static_bv_node<BlockFactor>::~static_bv_node() {
    buffer_type::release(buffer_);
}

template <uint32_t BlockFactor>
static_bv_node<BlockFactor> static_bv_node<BlockFactor>::subview(uint64_t begin, uint64_t len) const {
    ADAPTIVE_ASSUME(len > 0);
    ADAPTIVE_ASSUME(begin <= len_ && len <= len_ - begin);

    return static_bv_node(buffer_, begin_ + begin, len);
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::size() const {
    return len_;
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::ones() const {
    return ones_;
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::space_bytes() const {
    return sizeof(*this) + buffer_->space_bytes() / buffer_->refcount();
}

template <uint32_t BlockFactor> const uint64_t* static_bv_node<BlockFactor>::data() const {
    ADAPTIVE_ASSUME(begin_ % word_bits == 0);
    return buffer_->data() + begin_ / word_bits;
}

template <uint32_t BlockFactor> const shared_buffer<BlockFactor>& static_bv_node<BlockFactor>::buffer() const {
    return *buffer_;
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::begin() const {
    return begin_;
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::base_ones() const {
    return base_ones_;
}

template <uint32_t BlockFactor> bool static_bv_node<BlockFactor>::access(uint64_t i) const {
    ADAPTIVE_ASSUME(i < len_);

    return buffer_->access(begin_ + i);
}

template <uint32_t BlockFactor>
void static_bv_node<BlockFactor>::read(uint64_t i, uint64_t len, uint64_t* out, uint64_t out_bit) const {
    ADAPTIVE_ASSUME(i <= len_);
    ADAPTIVE_ASSUME(len <= len_ - i);

    buffer_->read(begin_ + i, len, out, out_bit);
}

template <uint32_t BlockFactor> template <bool Bit> uint64_t static_bv_node<BlockFactor>::rank(uint64_t i) const {
    ADAPTIVE_ASSUME(i <= len_);

    if constexpr (Bit) {
        // El buffer ya maneja el borde begin + i == size del buffer (devuelve todos sus unos).
        return buffer_->template rank<true>(begin_ + i) - base_ones_;
    } else {
        return i - rank<true>(i);
    }
}

template <uint32_t BlockFactor> template <bool Bit> uint64_t static_bv_node<BlockFactor>::select(uint64_t j) const {
    ADAPTIVE_ASSUME(j > 0);
    ADAPTIVE_ASSUME(j <= (Bit ? ones_ : len_ - ones_));

    const uint64_t base = Bit ? base_ones_ : begin_ - base_ones_; // bits del tipo pedido antes de begin_
    return buffer_->template select<Bit>(base + j) - begin_;
}

template <uint32_t BlockFactor> template <bool Bit> int64_t static_bv_node<BlockFactor>::next(uint64_t i) const {
    ADAPTIVE_ASSUME(i < len_);

    const int64_t p = buffer_->template next<Bit>(begin_ + i);
    if (p < 0 || static_cast<uint64_t>(p) >= begin_ + len_) {
        return -1;
    }
    return p - static_cast<int64_t>(begin_);
}

} // namespace adaptive
