# Tests del bitvector adaptativo multiario

Ejecutar desde la raiz del proyecto: `./run_tests.sh` (o `QUICK=1 ./run_tests.sh` para ~1 min).
Con CMake: `cmake -B build -DADAPTIVE_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build`.

| Archivo | Que verifica |
|---|---|
| `test_bit_util.cpp` | `popcount`, `select_in_word`, `next_in_word`, `copy_bits` (buffers de tamano exacto: ASan detecta lecturas/escrituras fuera de rango) |
| `test_static_bv.cpp` | `static_bv_node` (vista) y `shared_buffer` (almacenamiento, incl. refcount) vs. referencia: tamanos de borde, 9 patrones de bits, 4 `BlockFactor`, ambos constructores. **Debe seguir pasando cuando pase a ser una vista.** |
| `test_leaf_bv.cpp` | `leaf_bv_node`: constructor con offset, insert/erase/write aleatorios, llenar/vaciar |
| `test_bit_vector.cpp` | Test diferencial del arbol completo con 4 configuraciones, 7 mezclas de carga, varios `theta`; ademas crecer desde vacio, vaciar por completo, inserciones secuenciales, ciclos flatten/split, copia y serializacion |
| `diag_split_cost.cpp` | **No es un test.** Mide allocs, bytes pedidos y pico de heap durante los primeros splits (el "antes" de la memoria) |

Variables: `QUICK`, `THOROUGH=n`, `SEED=n`, `VERBOSE=1`, `NATIVE=1`, `SANITIZE=0`, `APPLY_KNOWN_FIXES=1`, `CXX`.

`tests/known_issues/`: parches para bugs del codigo base que estos tests detectan.
Los tests se compilan sin `-DNDEBUG` a proposito (`ADAPTIVE_ASSUME` debe ser un `assert` real).
