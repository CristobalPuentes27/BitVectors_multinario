/*

adaptive -- an implementation of adaptive dynamic bitvectors and packed arrays.
Copyright (C) 2024 Gonzalo Navarro
Copyright (C) 2026 Diego Arias

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#pragma once

#include <cstdint>

namespace adaptive {

// Desglose de memoria de un bit_vector (ver bit_vector::memory_report()). Pensado para medir la retencion de
// memoria de los buffers compartidos (memoria "huerfana") y para decidir la politica de compactacion.
//
// Los bits del vector se reparten entre hojas dinamicas (copias privadas, `leaf_bits`) y vistas estaticas
// (`view_bits`); las vistas apuntan a buffers compartidos, que pueden guardar mas bits que los que sus vistas
// cubren (`dead_bits`).
struct memory_report {
    // hojas dinamicas
    uint64_t leaves = 0;
    uint64_t leaf_bits = 0;  // bits almacenados (suma de largos)
    uint64_t leaf_bytes = 0; // incluye el espacio fijo no usado de cada hoja

    // nodos internos
    uint64_t dynamic_nodes = 0;
    uint64_t dynamic_bytes = 0;

    // hojas estaticas: vistas sobre buffers compartidos
    uint64_t static_views = 0;
    uint64_t view_bits = 0;  // suma de largos de las vistas (por recorrido del arbol)
    uint64_t view_bytes = 0; // solo los objetos vista

    // buffers (cada buffer se cuenta una vez)
    uint64_t buffers = 0;
    uint64_t buffer_refs = 0;  // suma de refcounts; debe igualar static_views
    uint64_t buffer_bits = 0;  // bits almacenados en los buffers
    uint64_t live_bits = 0;    // bits cubiertos por alguna vista, segun el contador de cada buffer
    uint64_t buffer_bytes = 0; // palabras + directorios de rank/select

    // Memoria huerfana: bits retenidos en buffers que ninguna vista referencia.
    uint64_t dead_bits() const { return buffer_bits - live_bits; }
    double dead_fraction() const {
        return buffer_bits == 0 ? 0.0 : static_cast<double>(buffer_bits - live_bits) / static_cast<double>(buffer_bits);
    }
    // Bytes del arbol (sin el objeto bit_vector); coincide con space_bytes() salvo el redondeo de repartir
    // cada buffer entre sus vistas (menos de un byte por vista).
    uint64_t total_bytes() const { return leaf_bytes + dynamic_bytes + view_bytes + buffer_bytes; }
};

} // namespace adaptive
