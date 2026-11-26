#!/usr/bin/env bash
# Compila y ejecuta los tests con g++/clang++ sin necesitar CMake.
#
# Uso:
#   ./run_tests.sh                 # modo normal, con ASan+UBSan
#   QUICK=1 ./run_tests.sh         # modo rapido (~1 min)
#   THOROUGH=3 ./run_tests.sh      # mas semillas (mas lento)
#   SEED=42 ./run_tests.sh         # semilla base distinta (reproducir fallos)
#   VERBOSE=1 ./run_tests.sh       # imprime cada escenario antes de ejecutarlo
#   NATIVE=1 ./run_tests.sh        # compila con -march=native (ruta BMI2 de select_in_word)
#   SANITIZE=0 ./run_tests.sh      # sin sanitizers (mas rapido, menos chequeos)
#   APPLY_KNOWN_FIXES=1 ./run_tests.sh   # prueba sobre una copia con tests/known_issues/*.patch aplicados
#   CXX=clang++ ./run_tests.sh
#   ./run_tests.sh bit_vector      # solo un test (bit_util | static_bv | static_views | leaf_bv | bit_vector)
#
# IMPORTANTE: los tests se compilan SIN -DNDEBUG para que ADAPTIVE_ASSUME siga siendo un assert() real.
set -u
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
OUT="${OUT:-build-tests}"
INC="include"
mkdir -p "$OUT"

FLAGS=(-std=c++20 -O1 -g -Wall -Wextra -Itests)
[ "${SANITIZE:-1}" = "1" ] && FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
[ "${NATIVE:-0}" = "1" ] && FLAGS+=(-march=native)

if [ "${APPLY_KNOWN_FIXES:-0}" = "1" ]; then
    rm -rf "$OUT/patched" && mkdir -p "$OUT/patched" && cp -r include "$OUT/patched/include"
    for p in tests/known_issues/*.patch; do
        echo ">> aplicando $p a la copia temporal ($OUT/patched/include)"
        patch -s -p1 -d "$OUT/patched" < "$p" || { echo "no se pudo aplicar $p"; exit 2; }
    done
    INC="$OUT/patched/include"
fi
FLAGS+=(-I"$INC")

ALL=(bit_util static_bv static_views leaf_bv bit_vector)
SEL=("${@:-${ALL[@]}}")
status=0
for name in "${SEL[@]}"; do
    echo ">> compilando test_$name"
    if ! $CXX "${FLAGS[@]}" "tests/test_$name.cpp" -o "$OUT/test_$name"; then
        echo "[ERROR] no compila test_$name"; status=1; continue
    fi
    start=$(date +%s)
    "$OUT/test_$name" || status=1
    echo "   ($(( $(date +%s) - start )) s)"
done

[ $status -eq 0 ] && echo "TODOS LOS TESTS PASARON" || echo "HAY TESTS FALLIDOS"
exit $status
