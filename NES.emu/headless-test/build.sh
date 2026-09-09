#!/usr/bin/env bash
# Builds the NES.emu fceu core (ines/cart/mmc3/file layers) with a small
# headless driver and runs the Mapper 195 (FS303) acceptance test.
# Used locally and by .github/workflows/headless-test.yml.
set -e
cd "$(dirname "$0")/../.."

CXX="${CXX:-g++}"
OUT="${OUT:-build-headless}"
CXXFLAGS="-std=c++20 -O1 -g -w -DHAVE_ASPRINTF -DPSS_STYLE=1 -DLSB_FIRST -DFRAMESKIP"
INC="-INES.emu/src -INES.emu/src/fceu -INES.emu/src/fceu/boards"

mkdir -p "$OUT"

OBJS=()
for src in NES.emu/src/fceu/boards/*.cpp NES.emu/src/fceu/boards/*.c; do
	obj="$OUT/$(echo "$src" | tr '/' '_').o"
	echo "CXX $src"
	$CXX $CXXFLAGS $INC -c "$src" -o "$obj"
	OBJs+=("$obj")
done

SRCS="
NES.emu/src/fceu/ines.cpp
NES.emu/src/fceu/cart.cpp
NES.emu/src/fceu/file.cpp
NES.emu/src/fceu/emufile.cpp
NES.emu/src/fceu/utils/crc32.cpp
NES.emu/src/fceu/utils/md5.cpp
NES.emu/src/fceu/utils/memory.cpp
NES.emu/src/fceu/utils/xstring.cpp
NES.emu/src/fceu/utils/endian.cpp
NES.emu/src/fceu/utils/general.cpp
NES.emu/headless-test/stubs.cc
NES.emu/headless-test/main.cc
"

for src in $SRCS; do
	obj="$OUT/$(echo "$src" | tr '/' '_').o"
	echo "CXX $src"
	$CXX $CXXFLAGS $INC -c "$src" -o "$obj"
	OBJs+=("$obj")
done

echo "LINK $OUT/headless-test"
$CXX "${OBJs[@]}" -lz -o "$OUT/headless-test"

cd "$OUT"
./headless-test
