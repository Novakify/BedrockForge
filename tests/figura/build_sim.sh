#!/bin/bash
# Build the headless FiguraBridge simulator. Usage: tests/figura/build_sim.sh [OBJDIR] [OUT]
# Each agent/session should use its own OBJDIR so parallel builds don't collide.
set -e
OBJ=${1:-/tmp/fbobj}; OUT=${2:-/tmp/fbsim}
cd "$(dirname "$0")/../.."
mkdir -p "$OBJ"
for f in src/figura/core/*.cpp src/lyrics/core/Json.cpp tests/figura/Sim.cpp; do
  o=$OBJ/$(basename $f .cpp).o
  newer=0; for h in src/figura/core/*.h; do [ "$h" -nt "$o" ] && newer=1; done
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ $newer = 1 ]; then g++ -std=c++20 -O1 -g -Wall -Isrc -c "$f" -o "$o"; fi
done
g++ -o "$OUT" "$OBJ"/*.o
