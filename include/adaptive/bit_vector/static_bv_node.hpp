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

// static_bv_node: hoja estatica. Es una VISTA sobre un shared_buffer.
//
// Estado actual (paso 3 del plan): la vista siempre cubre el buffer completo y es su unica duena
// (refcount == 1), asi que el comportamiento es identico al de la version con almacenamiento propio.
// Todas las operaciones se delegan al buffer. Los siguientes pasos agregan el rango [begin, begin+len)
// y el rank base para que varias vistas compartan un buffer (rank/select/next/access/read con offsets).
template <uint32_t BlockFactor = 4> class static_bv_node {
  public:
    using buffer_type = shared_buffer<BlockFactor>;

    static constexpr uint32_t block_factor = buffer_type::block_factor;
    static constexpr uint32_t word_bits = buffer_type::word_bits;
    static constexpr uint32_t w16 = buffer_type::w16;
    static constexpr uint32_t superblock_bits = buffer_type::superblock_bits;
    static constexpr uint32_t block_bits = buffer_type::block_bits;

    // Copian los bits a un buffer nuevo (igual que antes).
    static_bv_node(const uint64_t* words, uint64_t size);
    static_bv_node(std::vector<uint64_t>&& words, uint64_t size);
    ~static_bv_node();

    // La propiedad del buffer es por refcount; las copias de vistas se haran explicitamente en pasos posteriores.
    static_bv_node(const static_bv_node&) = delete;
    static_bv_node& operator=(const static_bv_node&) = delete;

    uint64_t size() const;
    uint64_t ones() const;
    uint64_t space_bytes() const;
    const uint64_t* data() const;
    const buffer_type& buffer() const;

    bool access(uint64_t i) const;
    void read(uint64_t i, uint64_t len, uint64_t* out, uint64_t out_bit) const;

    template <bool Bit> uint64_t rank(uint64_t i) const;
    template <bool Bit> uint64_t select(uint64_t j) const;
    template <bool Bit> int64_t next(uint64_t i) const;

  private:
    buffer_type* buffer_;
};

template <uint32_t BlockFactor>
static_bv_node<BlockFactor>::static_bv_node(const uint64_t* words, uint64_t size)
    : buffer_(new buffer_type(words, size)) {}

template <uint32_t BlockFactor>
static_bv_node<BlockFactor>::static_bv_node(std::vector<uint64_t>&& words, uint64_t size)
    : buffer_(new buffer_type(std::move(words), size)) {}

template <uint32_t BlockFactor> static_bv_node<BlockFactor>::~static_bv_node() {
    buffer_type::release(buffer_);
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::size() const {
    return buffer_->size();
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::ones() const {
    return buffer_->ones();
}

template <uint32_t BlockFactor> uint64_t static_bv_node<BlockFactor>::space_bytes() const {
    // Con una sola vista por buffer, el espacio del buffer se atribuye completo a esta vista.
    // Cuando un buffer tenga varias vistas esto debe cambiar para no contarlo varias veces.
    ADAPTIVE_ASSUME(buffer_->refcount() == 1);
    return sizeof(*this) + buffer_->space_bytes();
}

template <uint32_t BlockFactor> const uint64_t* static_bv_node<BlockFactor>::data() const {
    return buffer_->data();
}

template <uint32_t BlockFactor> const shared_buffer<BlockFactor>& static_bv_node<BlockFactor>::buffer() const {
    return *buffer_;
}

template <uint32_t BlockFactor> bool static_bv_node<BlockFactor>::access(uint64_t i) const {
    return buffer_->access(i);
}

template <uint32_t BlockFactor>
void static_bv_node<BlockFactor>::read(uint64_t i, uint64_t len, uint64_t* out, uint64_t out_bit) const {
    buffer_->read(i, len, out, out_bit);
}

template <uint32_t BlockFactor> template <bool Bit> uint64_t static_bv_node<BlockFactor>::rank(uint64_t i) const {
    return buffer_->template rank<Bit>(i);
}

template <uint32_t BlockFactor> template <bool Bit> uint64_t static_bv_node<BlockFactor>::select(uint64_t j) const {
    return buffer_->template select<Bit>(j);
}

template <uint32_t BlockFactor> template <bool Bit> int64_t static_bv_node<BlockFactor>::next(uint64_t i) const {
    return buffer_->template next<Bit>(i);
}

} // namespace adaptive
