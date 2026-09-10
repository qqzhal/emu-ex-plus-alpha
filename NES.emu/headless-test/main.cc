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
#include <algorithm>
#include <string>
#include <vector>

#include "emuframework/EmuApp.hh" // stub header with minimal EmuEx types

#include "fceu/types.h"
#include "fceu/fceu.h"
#include "fceu/git.h"
#include "fceu/ines.h"
#include "fceu/cart.h"
#include "fceu/file.h"
#include "fceu/ppu.h"
#include "fceu/video.h"
#include "fceu/x6502.h"
#include "fceu/boards/mmc3.h"

// declared in fceu.cpp / defined in ines.cpp (static there, no header)
int iNESLoad(const char *name, FCEUFILE *fp, int OverwriteVidMode);

// defined by the MMC3 board (not exposed in a header)
extern uint8 *CHRRAM;

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

	// CPU vectors at the end of the PRG image: NMI=$FA50 RST=$FA60 IRQ=$FA50
	uint32 n = prg.size();
	prg[n - 6] = 0x50; prg[n - 5] = 0xFA;
	prg[n - 4] = 0x60; prg[n - 3] = 0xFA;
	prg[n - 2] = 0x50; prg[n - 1] = 0xFA;

	// Last fixed bank: a minimal boot program at CPU $FA60 (bank N-1 offset
	// $1A60) that exercises every FS303 feature the hacks rely on:
	//   XRAM write, MMC3 IRQ setup, vblank wait, CHR RAM font write through
	//   $2007, display enable, R6=$42 bank switch, readback into WRAM.
	const uint32 base = n - 8192;	// start of the last fixed bank
	const uint32 off = base + 0x1A60;
	std::vector<uint8> code = {		0x78,				// SEI
		0xA2, 0xFF, 0x9A,			// LDX #$FF / TXS
		0xA9, 0x5A, 0x8D, 0x00, 0x50,	// LDA #$5A / STA $5000   (XRAM)
		0xA9, 0x10, 0x8D, 0x00, 0xC0,	// LDA #$10 / STA $C000   (IRQ latch)
		0xA9, 0x00, 0x8D, 0x01, 0xC0,	// LDA #$00 / STA $C001   (reload)
		0x8D, 0x01, 0xE0,			// STA $E001              (IRQ enable)
		0x2C, 0x02, 0x20, 0x10, 0xFB,	// wait vblank 1
		0x2C, 0x02, 0x20, 0x10, 0xFB,	// wait vblank 2
		0xA9, 0x00, 0x8D, 0x06, 0x20,	// LDA #0 / STA $2006 / STA $2006
		0x8D, 0x06, 0x20,
		0xA2, 0x00,				// LDX #0
		0xBD, 0x40, 0xFB,			// loop: LDA $FB40,X
		0x8D, 0x07, 0x20,			// STA $2007              (CHR RAM)
		0xE8,					// INX
		0xE0, 0x40,				// CPX #$40
		0xD0, 0xF5,				// BNE loop
		0xA9, 0x3F, 0x8D, 0x06, 0x20,	// LDA #$3F / STA $2006   (palette)
		0xA9, 0x00, 0x8D, 0x06, 0x20,	// LDA #$00 / STA $2006
		0xA9, 0x0F, 0x8D, 0x07, 0x20,	// LDA #$0F / STA $2007   (black)
		0xA9, 0x30, 0x8D, 0x07, 0x20,	// LDA #$30 / STA $2007   (white)
		0xA9, 0x28, 0x8D, 0x07, 0x20,	// LDA #$28 / STA $2007   (orange)
		0xA9, 0x16, 0x8D, 0x07, 0x20,	// LDA #$16 / STA $2007   (red)
		0xA9, 0x20, 0x8D, 0x06, 0x20,	// LDA #$20 / STA $2006   (nametable)
		0xA9, 0x00, 0x8D, 0x06, 0x20,	// LDA #$00 / STA $2006
		0xA2, 0x00,				// LDX #0
		0x8A, 0x8D, 0x07, 0x20,			// ntloop: TXA / STA $2007
		0xE8,					// INX
		0xE0, 0x40,				// CPX #$40
		0xD0, 0xF7,				// BNE ntloop
		0xA9, 0x90, 0x8D, 0x00, 0x20,	// LDA #$90 / STA $2000   (NMI on)
		0xA9, 0x1E, 0x8D, 0x01, 0x20,	// LDA #$1E / STA $2001   (display on)
		0xA9, 0x06, 0x8D, 0x00, 0x80,	// LDA #$06 / STA $8000   (select R6)
		0xA9, 0x42, 0x8D, 0x01, 0x80,	// LDA #$42 / STA $8001   (bank $42)
		0xAD, 0x00, 0x80,			// LDA $8000              (read bank data)
		0x8D, 0x00, 0x60,			// STA $6000              (WRAM)
		0x4C, 0xAF, 0xFA,			// forever: JMP $FAAF
	};
	// fix the JMP target to the actual address of the forever loop
	{
		uint32 loopBankOff = 0x1A60 + 125;	// offset of the final JMP
		uint32 loopAddr = 0xE000 + loopBankOff;
		code[code.size() - 2] = loopAddr & 0xFF;
		code[code.size() - 1] = loopAddr >> 8;
	}
	memcpy(prg.data() + off, code.data(), code.size());
	prg[base + 0x1A50] = 0x40;		// NMI/IRQ handler: RTI
	for (int i = 0; i < 64; i++)
		prg[base + 0x1B40 + i] = (uint8)(0x40 + i * 3);	// font table
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
	CHECK(rd(0xFFFA) == 0x50 && rd(0xFFFB) == 0xFA, "NMI vector = $FA50");
	CHECK(rd(0xFFFC) == 0x60 && rd(0xFFFD) == 0xFA, "RST vector = $FA60");
	CHECK(rd(0xFFFE) == 0x50 && rd(0xFFFF) == 0xFA, "IRQ vector = $FA50");

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

// ---- live-emulation case: real CPU (x6502) + real PPU timing loop ----
// Boot the ROM and run frames through FCEUPPU_Loop exactly like the app
// does. A black-screen-at-boot regression shows up as a jammed CPU or a
// dead PC (the patch layer copied from the wrong PRG bank and the code
// falling into uninitialized RAM), so assert liveness instead of pixels.

static uint8 emuRAM[0x800];

static DECLFR(EmuARAML) {
	return emuRAM[A & 0x7FF];
}

static DECLFW(EmuBRAML) {
	emuRAM[A & 0x7FF] = V;
}

// capture PPU/MMC3 liveness across frames
static bool g_displaySeen;
static bool g_irqReloadSeen;

static void runEmuCase(const char *path, int prg16k) {
	printf("== emu run: %s (real CPU + PPU timing, 240 frames)\n", path);

	int userCancel = 0;
	FCEUFILE *fp = FCEU_fopen(path, 0, "rb", 0, 0, nullptr, &userCancel);
	if (!fp) {
		printf("  [FAIL] cannot open %s\n", path);
		g_failures++;
		return;
	}
	if (iNESLoad(path, fp, 0) != LOADER_OK) {
		printf("  [FAIL] iNESLoad failed\n");
		g_failures++;
		FCEU_fclose(fp);
		return;
	}

	memset(emuRAM, 0, sizeof(emuRAM));
	SetReadHandler(0x0000, 0x1FFF, EmuARAML);
	SetWriteHandler(0x0000, 0x1FFF, EmuBRAML);

	FSettings.UsrFirstSLine[0] = 0;
	FSettings.UsrLastSLine[0] = 239;
	FSettings.UsrFirstSLine[1] = 0;
	FSettings.UsrLastSLine[1] = 239;
	FCEUPPU_Init();
	FCEUPPU_SetVideoSystem(0);
	currCartInfo->Power();
	FCEUPPU_Power();
	X6502_Init();
	X6502_Power();

	const int frames = 240;
	std::vector<uint16> pcs;
	g_displaySeen = false;
	g_irqReloadSeen = false;
	int ppuOnFrames = 0;
	for (int f = 0; f < frames; f++) {
		EmuEx::NesSystem sys;
		FCEUPPU_Loop(EmuEx::EmuSystemTaskContext{}, sys, nullptr, nullptr, 0);
		pcs.push_back(X.PC);
		if (PPU[1] & 0x18) {
			ppuOnFrames++;
			g_displaySeen = true;
		}
		if (IRQa || IRQCount)
			g_irqReloadSeen = true;
	}

	// 1. the boot program ran: XRAM write landed, bank $42 readback in WRAM
	CHECK(rd(0x5000) == 0x5A, "boot program wrote $5A into $5000 XRAM");
	uint32 expectBanks = (uint32)prg16k * 2;
	uint8 expectBankByte = syntheticPrgByte((0x42 % expectBanks) * 8192);
	CHECK(rd(0x6000) == expectBankByte,
		"real CPU read bank $42 via R6 and stored it in WRAM");

	// 2. CPU liveness: a bank-mangling black screen jams the CPU
	CHECK(!X.jammed, "CPU not jammed");

	// 3. the boot program turned the display on
	CHECK(g_displaySeen, "PPU rendering enabled during boot");
	CHECK(ppuOnFrames > 0, "at least one frame rendered with display on");

	// 4. the MMC3 scanline IRQ clocked (game effects rely on it)
	CHECK(g_irqReloadSeen, "MMC3 IRQ counter active");

	// 5. font tiles written into the 4KB CHR RAM through $2007
	int chrOk = 0;
	if (CHRRAM)
		for (int i = 0; i < 64; i++)
			chrOk += CHRRAM[i] == (uint8)(0x40 + i * 3);
	CHECK(chrOk >= 60, "boot program wrote the font table into CHR RAM");

	// 6. the render pipeline actually produced pixels: count distinct
	// palette indices on screen. A black-screen bug shows up here as an
	// (almost) uniformly blank XBuf even though the game is running.
	int nonzeroPixels = 0, distinctColors = 0, seen[64] = {};
	for (int i = 0; i < 256 * 240; i++) {
		uint8 p = XBuf[i] & 63;
		if (p)
			nonzeroPixels++;
		if (!seen[p]++) {
			distinctColors++;
		}
	}
	char pngName[64];
	snprintf(pngName, sizeof(pngName), "screen_%s.pgm", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
	FILE *pf = fopen(pngName, "wb");
	if (pf) {
		fprintf(pf, "P5\n256 240\n255\n");
		for (int i = 0; i < 256 * 240; i++)
			fputc(XBuf[i] * 4, pf);
		fclose(pf);
	}
	CHECK(nonzeroPixels > 5000, "framebuffer has real pixels (not all black)");
	CHECK(distinctColors >= 3, "framebuffer has multiple colors (tiles rendered)");

	printf("  info: last PC=$%04X display-on frames=%d chr-ok=%d/64 nonzero=%d colors=%d\n",
		X.PC, ppuOnFrames, chrOk, nonzeroPixels, distinctColors);

	FCEU_fclose(fp);
}

// ---- real-ROM case: run an actual game ROM and look for the black screen ----
// Loads a real .nes file, runs 600 frames (10s) through the real CPU+PPU,
// then reports whether the CPU is alive and the framebuffer has content.
// This is the acceptance test for actual hack ROMs (no synthetic data).

static void dumpPGM(const char *path) {
	FILE *pf = fopen(path, "wb");
	if (!pf)
		return;
	fprintf(pf, "P5\n256 240\n255\n");
	for (int i = 0; i < 256 * 240; i++)
		fputc(XBuf[i] * 4, pf);
	fclose(pf);
}

static void realRomCase(const char *path) {
	printf("== real ROM: %s (real CPU + PPU timing, 600 frames)\n", path);

	int userCancel = 0;
	FCEUFILE *fp = FCEU_fopen(path, 0, "rb", 0, 0, nullptr, &userCancel);
	if (!fp) {
		printf("  [FAIL] cannot open %s\n", path);
		g_failures++;
		return;
	}
	if (iNESLoad(path, fp, 0) != LOADER_OK) {
		printf("  [FAIL] iNESLoad failed\n");
		g_failures++;
		FCEU_fclose(fp);
		return;
	}
	CHECK(GameInfo && GameInfo->mappernum == 195, "mapper number is 195");
	printf("  info: totalFileSize=%llu VROM_size=%u\n",
		(unsigned long long)currCartInfo->totalFileSize, (unsigned)VROM_size);

	memset(emuRAM, 0, sizeof(emuRAM));
	SetReadHandler(0x0000, 0x1FFF, EmuARAML);
	SetWriteHandler(0x0000, 0x1FFF, EmuBRAML);

	FSettings.UsrFirstSLine[0] = 0;
	FSettings.UsrLastSLine[0] = 239;
	FSettings.UsrFirstSLine[1] = 0;
	FSettings.UsrLastSLine[1] = 239;
	FCEUPPU_Init();
	FCEUPPU_SetVideoSystem(0);
	currCartInfo->Power();
	FCEUPPU_Power();
	X6502_Init();
	X6502_Power();

	int xramNonZero = 0, chrNonZero = 0, wramNonZero = 0;
	int displayFrames = 0;
	bool jammed = false;
	const int frames = 600;
	for (int f = 1; f <= frames; f++) {
		EmuEx::NesSystem sys;
		FCEUPPU_Loop(EmuEx::EmuSystemTaskContext{}, sys, nullptr, nullptr, 0);
		if (X.jammed)
			jammed = true;
		if (PPU[1] & 0x18)
			displayFrames++;
		if (f == 300)
			dumpPGM("real_mid.pgm");
	}

	int nonzeroPixels = 0, distinctColors = 0, seen[64] = {};
	for (int i = 0; i < 256 * 240; i++) {
		uint8 p = XBuf[i] & 63;
		if (p)
			nonzeroPixels++;
		if (!seen[p]++)
			distinctColors++;
	}

	// real game signature checks
	for (int a = 0x5000; a < 0x6000; a++)
		xramNonZero += rd(a) != 0;
	for (int a = 0x6000; a < 0x7000; a++)
		wramNonZero += rd(a) != 0;
	if (CHRRAM)
		for (int i = 0; i < 4096; i++)
			chrNonZero += CHRRAM[i] != 0;

	printf("  info: jammed=%d display-frames=%d/600 XRAM-nz=%d WRAM-nz=%d CHRRAM-nz=%d nonzero-px=%d colors=%d last-PC=$%04X\n",
		(int)jammed, displayFrames, xramNonZero, wramNonZero, chrNonZero, nonzeroPixels, distinctColors, X.PC);
	CHECK(!jammed, "CPU not jammed");
	CHECK(nonzeroPixels > 3000, "framebuffer has real pixels (black screen would be ~0)");

	char pgmName[512];
	snprintf(pgmName, sizeof(pgmName), "%s.pgm", path);
	dumpPGM(pgmName);
	printf("  (frames: real_mid.pgm, %s)\n", pgmName);
	FCEU_fclose(fp);
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

	// live CPU + PPU run: this is where a black-screen-at-boot regression
	// (bank mangling, dead patch layer, dead IRQ) would show up
	runEmuCase("h195_game.nes", 80);
	runEmuCase("h195_game2.nes", 96);
	runEmuCase("h195_origin.nes", 32);

	// real ROMs passed as extra args (downloaded by CI or given locally):
	// load and run the actual game, dump frames, detect a black screen
	for (int a = 1; a < argc; a++)
		realRomCase(argv[a]);

	if (g_failures) {
		printf("RESULT: FAIL (%d check(s) failed)\n", g_failures);
		return 1;
	}
	printf("RESULT: ALL PASS\n");
	return 0;
}
