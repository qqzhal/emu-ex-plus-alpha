// Headless acceptance test for the Mapper 195 (FS303) enhancements.
// Mirrors the PC-side acceptance criteria from fceux增强说明.md §9.2:
//   - iNES loading must keep 195 in the not_power2 list so the CHR section
//     lands in VROM (a broken load yields a blank 0xFF-filled VROM).
//   - Mapper195_Init must derive the real PRG bank count (160 for the
//     game.nes layout) from totalFileSize - CHR.
//   - PRG writes must translate modulo the real bank count, with the
//     FixMMC3PRG sentinel values 0xFE/0xFF mapping to N-2/N-1.
//   - CHR values <= 3 must route to the 4KB CHR RAM, others to CHR ROM.
//   - $5000-$5FFF must be an independent 4KB PRG-RAM.
// Runs against a synthetic ROM with the exact header layout of game.nes
// (160 x 8KB PRG + 256KB CHR, no battery, horizontal mirroring) plus a
// standard 64-bank FS303 ROM as control, so no copyrighted data is needed.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "fceu/types.h"
#include "fceu/fceu.h"
#include "fceu/git.h"
#include "fceu/ines.h"
#include "fceu/cart.h"
#include "fceu/file.h"
#include "fceu/ppu.h"

// declared in fceu.cpp / defined in ines.cpp (static there, no header)
int iNESLoad(const char *name, FCEUFILE *fp, int OverwriteVidMode);

bool HeadlessHasExState(const char *tag);

static int g_failures = 0;

#define CHECK(cond, msg) do { \
	if (cond) { printf("  [PASS] %s\n", msg); } \
	else { printf("  [FAIL] %s\n", msg); g_failures++; } \
} while (0)

static uint8 rd(uint32 A) {
	return GetReadHandler(A & 0xFFFF)(A & 0xFFFF);
}

static void wr(uint32 A, uint8 V) {
	GetWriteHandler(A & 0xFFFF)(A & 0xFFFF, V);
}

static uint32 prgBytesFor(int banks16k) {
	return (uint32)banks16k * 16384;
}

static uint32 chrBytes() {
	return 256 * 1024;
}

// PRG byte at absolute ROM-buffer offset, matching what the test fills in.
static uint8 syntheticPrgByte(uint32 offset) {
	// Fill each 8KB bank with a recognizable pattern: byte k of bank N is
	// (N + k) except the reset vectors at the very end of the image.
	return (uint8)((offset * 7 + 13) & 0xFF);
}

static uint8 syntheticChrByte(uint32 offset) {
	return (uint8)((offset * 5 + 0x5A) & 0xFF);
}

static void fillSyntheticRom(std::vector<uint8> &prg, std::vector<uint8> &chr, int prg16k) {
	prg.resize(prgBytesFor(prg16k));
	for (uint32 i = 0; i < prg.size(); i++)
		prg[i] = syntheticPrgByte(i);
	chr.resize(chrBytes());
	for (uint32 i = 0; i < chr.size(); i++)
		chr[i] = syntheticChrByte(i);

	// CPU vectors at the end of the PRG image: NMI=$C550 RST=$FA60 IRQ=$C506
	uint32 n = prg.size();
	prg[n - 6] = 0x50; prg[n - 5] = 0xC5;
	prg[n - 4] = 0x60; prg[n - 3] = 0xFA;
	prg[n - 2] = 0x06; prg[n - 1] = 0xC5;
}

static bool writeSyntheticRom(const char *path, int prg16k,
	const std::vector<uint8> &prg, const std::vector<uint8> &chr) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	uint8 head[16] = {};
	memcpy(head, "NES\x1A", 4);
	head[4] = (uint8)prg16k;          // 16KB PRG units (80 -> 1280KB, non-power-of-2)
	head[5] = 32;                     // 8KB CHR units (256KB)
	head[6] = 0x30;                   // mapper low nibble 3, horizontal mirroring, no battery
	head[7] = 0xC0;                   // mapper high nibble 12 -> mapper 195
	fwrite(head, 1, 16, f);
	fwrite(prg.data(), 1, prg.size(), f);
	fwrite(chr.data(), 1, chr.size(), f);
	fclose(f);
	return true;
}

static uint32 fileCRC(const std::vector<uint8> &data) {
	// simple additive checksum, enough to tell buffers apart
	uint32 c = 0;
	for (size_t i = 0; i < data.size(); i++)
		c = c * 31 + data[i];
	return c;
}

static void runCase(const char *path, int prg16k, const std::vector<uint8> &prg, const std::vector<uint8> &chr) {
	char title[128];
	snprintf(title, sizeof(title), "case: %s (PRG %d x 16KB = %d banks of 8KB)", path, prg16k, prg16k * 2);
	printf("== %s\n", title);

	uint32 expectBanks = (uint32)prg16k * 2;

	int userCancel = 0;
	FCEUFILE *fp = FCEU_fopen(path, 0, "rb", 0, 0, nullptr, &userCancel);
	if (!fp) {
		printf("  [FAIL] cannot open %s\n", path);
		g_failures++;
		return;
	}

	int loadResult = iNESLoad(path, fp, 0);
	CHECK(loadResult == LOADER_OK, "iNESLoad returns LOADER_OK");
	CHECK(GameInfo && GameInfo->mappernum == 195, "mapper number is 195");

	uint64 expectTotal = (uint64)prg.size() + (uint64)chr.size();
	CHECK(currCartInfo && currCartInfo->totalFileSize == expectTotal,
		("CartInfo.totalFileSize == " + std::to_string(expectTotal) +
		 " (got " + std::to_string(currCartInfo ? (unsigned long long)currCartInfo->totalFileSize : 0) + ")").c_str());

	printf("  info: ROM_size=%u (16KB units, padded) VROM_size=%u\n",
		(unsigned)ROM_size, (unsigned)VROM_size);

	// --- modification A: the CHR section must have been read into VROM ---
	CHECK(VROM != nullptr, "VROM buffer allocated");
	bool chrIntact = false;
	if (VROM) {
		chrIntact = (memcmp(VROM, chr.data(), 4096) == 0) &&
			(memcmp(VROM + chr.size() - 4096, chr.data() + chr.size() - 4096, 4096) == 0);
	}
	CHECK(chrIntact, "VROM holds real CHR data (not_power2 list kept 195)");

	// --- power up the board (NES.emu's CartInfo::Power takes no args) ---
	if (currCartInfo->Power)
		currCartInfo->Power();
	else {
		printf("  [FAIL] no Power handler registered\n");
		g_failures++;
	}

	// --- reset vector must come from the last real bank (N-1), not a
	//     bit-mangled bank: vectors live at the end of the PRG image ---
	CHECK(rd(0xFFFA) == 0x50 && rd(0xFFFB) == 0xC5, "NMI vector = $C550");
	CHECK(rd(0xFFFC) == 0x60 && rd(0xFFFD) == 0xFA, "RST vector = $FA60");
	CHECK(rd(0xFFFE) == 0x06 && rd(0xFFFF) == 0xC5, "IRQ vector = $C506");

	uint32 last = expectBanks - 1;
	CHECK(rd(0xE000) == syntheticPrgByte(last * 8192),
		"$E000 window maps to bank N-1 (FixMMC3PRG sentinel $FF)");
	CHECK(rd(0xC000) == syntheticPrgByte((expectBanks - 2) * 8192),
		"$C000 window maps to bank N-2 (FixMMC3PRG sentinel $FE)");

	// --- game writes R6=0x42 / R7=0x43 (the hack's patch loader) ---
	uint32 bank42 = 0x42 % expectBanks;
	uint32 bank43 = 0x43 % expectBanks;
	wr(0x8000, 0x06);
	wr(0x8001, 0x42);
	CHECK(rd(0x8000) == syntheticPrgByte(bank42 * 8192),
		"R6=0x42 maps $8000 to bank 0x42 % N (modulo, not bitmask)");

	wr(0x8000, 0x07);
	wr(0x8001, 0x43);
	CHECK(rd(0xA000) == syntheticPrgByte(bank43 * 8192),
		"R7=0x43 maps $A000 to bank 0x43 % N");

	// R2/R3/R4/R5 -> CHR, sanity check a ROM window after switching mode
	wr(0x8000, 0x46);	// MMC3_cmd bit6 set: $8000 slot = 0xFE sentinel, $C000 = DRegBuf[6]
	CHECK(rd(0xC000) == syntheticPrgByte(bank42 * 8192),
		"mode switch: $C000 now maps DRegBuf[6]=0x42");
	CHECK(rd(0x8000) == syntheticPrgByte((expectBanks - 2) * 8192),
		"mode switch: $8000 fixed bank = N-2");

	// --- $5000-$5FFF independent 4KB XRAM ---
	CHECK(rd(0x5000) == 0x00, "XRAM reads 0 at power-up");
	wr(0x5000, 0xAB);
	CHECK(rd(0x5000) == 0xAB, "XRAM write/readback at $5000");
	wr(0x5FFF, 0xCD);
	CHECK(rd(0x5FFF) == 0xCD, "XRAM write/readback at $5FFF");
	CHECK(rd(0x6000) != 0xAB, "XRAM does not alias $6000 WRAM");

	// --- $6000-$7FFF 8KB WRAM ---
	wr(0x6000, 0x5A);
	CHECK(rd(0x6000) == 0x5A, "WRAM write/readback at $6000");

	// --- CHR: values <= 3 -> CHR RAM, else CHR ROM ---
	wr(0x8000, 0x00);
	wr(0x8001, 0x00);	// slot 0 <- CHR RAM page 0
	CHECK((PPUCHRRAM & 0x01) != 0, "CHR value 0 routes slot 0 to CHR RAM");
	CHECK(VPageR[0][0x200] == 0, "CHR RAM slot 0 reads zeroed RAM");
	wr(0x8001, 0x04);	// slot 0 <- CHR ROM page 4
	CHECK((PPUCHRRAM & 0x01) == 0, "CHR value 4 routes slot 0 to CHR ROM");
	CHECK(VPageR[0][0x200] == syntheticChrByte(4 * 1024 + 0x200),
		"CHR ROM slot 0 reads CHR page 4");

	// --- save-state registration for the new RAM blocks ---
	CHECK(HeadlessHasExState("CHRR"), "CHRRAM registered in save state");
	CHECK(HeadlessHasExState("M5KX"), "XRAM registered in save state");

	FCEU_fclose(fp);
	printf("\n");
}

int main(int argc, char **argv) {
	static FCEUGI gi;
	GameInfo = &gi;

	std::vector<uint8> prg, chr;

	// game.nes layout: 80 x 16KB PRG (non-power-of-2 160 banks) + 256KB CHR
	fillSyntheticRom(prg, chr, 80);
	if (!writeSyntheticRom("h195_game.nes", 80, prg, chr)) {
		fprintf(stderr, "cannot write h195_game.nes\n");
		return 2;
	}

	// game2.nes layout: 96 x 16KB PRG (192 banks) + 256KB CHR
	std::vector<uint8> prg2, chr2;
	fillSyntheticRom(prg2, chr2, 96);
	if (!writeSyntheticRom("h195_game2.nes", 96, prg2, chr2)) {
		fprintf(stderr, "cannot write h195_game2.nes\n");
		return 2;
	}

	// origin.nes layout: 32 x 16KB PRG (64 banks, power of 2) + 256KB CHR
	std::vector<uint8> prg0, chr0;
	fillSyntheticRom(prg0, chr0, 32);
	if (!writeSyntheticRom("h195_origin.nes", 32, prg0, chr0)) {
		fprintf(stderr, "cannot write h195_origin.nes\n");
		return 2;
	}

	runCase("h195_game.nes", 80, prg, chr);
	runCase("h195_game2.nes", 96, prg2, chr2);
	runCase("h195_origin.nes", 32, prg0, chr0);

	if (g_failures) {
		printf("RESULT: FAIL (%d check(s) failed)\n", g_failures);
		return 1;
	}
	printf("RESULT: ALL PASS\n");
	return 0;
}
