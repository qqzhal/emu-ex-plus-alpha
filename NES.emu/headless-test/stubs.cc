// Headless stand-in for the NES.emu front-end (main/FceuApi.cc + imagine).
// Provides the globals and FCEUD_* entry points the fceu core references so
// ines.cpp / cart.cpp / boards/*.cpp can be linked and exercised on a desktop
// CI runner. Only used by headless-test/main.cc, never shipped in the app.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include "fceu/types.h"
#include "fceu/fceu.h"
#include "fceu/cart.h"
#include "fceu/state.h"
#include "fceu/x6502.h"
#include "fceu/ppu.h"
#include "fceu/sound.h"
#include "fceu/unif.h"
#include "fceu/cheat.h"
#include "fceu/input.h"
#include "fceu/file.h"
#include "fceu/emufile.h"
#include "fceu/driver.h"

// ---- globals owned by fceu.cpp in the real app ----
readfunc ARead[0x10000];
writefunc BWrite[0x10000];
FCEUGI *GameInfo = nullptr;
void (*GameInterface)(GI h) = nullptr;
void (*GameStateRestore)(int version) = nullptr;
uint64 timestampbase = 0;
int normalscanlines = 240;
int totalscanlines = 240;
int postrenderscanlines = 0;
int vblankscanlines = 0;

static DECLFR(ANullStub) {
	return 0xFF;
}

static DECLFW(BNullStub) {
}

readfunc GetReadHandler(int32 a) {
	return ARead[a];
}

writefunc GetWriteHandler(int32 a) {
	return BWrite[a];
}

void SetReadHandler(int32 start, int32 end, readfunc func) {
	if (!func)
		func = ANullStub;
	for (int32 x = end; x >= start; x--)
		ARead[x] = func;
}

void SetWriteHandler(int32 start, int32 end, writefunc func) {
	if (!func)
		func = BNullStub;
	for (int32 x = end; x >= start; x--)
		BWrite[x] = func;
}

// ---- globals owned by ppu.cpp / x6502.cpp ----
int scanline = 0;
uint32 timestamp = 0;
uint32 soundtimestamp = 0;
uint8 PPU[4];
uint8 PPUCHRRAM = 0;
uint8 *UNIFchrrama = nullptr;
void (*GameHBIRQHook)(void) = nullptr;
void (*GameHBIRQHook2)(void) = nullptr;

void X6502_IRQBegin(int w) {}
void X6502_IRQEnd(int w) {}

// ---- x6502.cpp debug hooks ----
uint8 X6502_DMR(uint32 A) { return 0xFF; }
void X6502_DMW(uint32 A, uint8 V) {}

// ---- fceu.cpp ----
int eoptions = 0;

void FCEU_MemoryRand(uint8 *ptr, uint32 size, bool default_zero) {
	memset(ptr, default_zero ? 0x00 : 0xFF, size);
}

// ---- vsuni.cpp ----
SFORMAT FCEUVSUNI_STATEINFO[] = {
	{ 0 }
};

// ---- ppu.cpp globals (ppu.cpp is not built here: it depends on the
// imagine/EmuFramework render front-end) ----
uint8 PALRAM[0x20];
uint8 UPALRAM[3];
uint8 NTARAM[0x800];
uint8 *vnapage[4];
uint8 PPUNTARAM = 0;
uint32 NTRefreshAddr = 0;
void (*PPU_hook)(uint32 A) = nullptr;
uint8 (FASTCALL *FFCEUX_PPURead)(uint32 A) = nullptr;
void (*FFCEUX_PPUWrite)(uint32 A, uint8 V) = nullptr;
void PPU_ResetHooks() {}
int newppu = 0;
int newppu_get_scanline() { return 0; }
int newppu_get_dot() { return 0; }
PPUPHASE ppuphase = PPUPHASE_VBL;
int fceuindbg = 0;
int PEC586Hack = 0;
int QTAIHack = 0;
uint8 qtaintramreg = 0;
int MMC5Hack = 0;
uint8 MMC5HackCHRMode = 0;
uint32 MMC5HackVROMMask = 0;
uint8 *MMC5HackExNTARAMPtr = nullptr;
uint8 *MMC5HackVROMPTR = nullptr;
uint8 MMC5HackSPMode = 0;
uint8 MMC5HackSPScroll = 0;
uint8 MMC5HackSPPage = 0;
uint8 MMC50x5130 = 0;
uint8 *FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr) { return nullptr; }
void FCEUPPU_LineUpdate() {}

// ---- sound.cpp globals (not built here) ----
EXPSOUND GameExpSound = { nullptr, nullptr, nullptr, nullptr, nullptr };
int32 nesincsize = 0;
int32 Wave[2048 + 512];
int32 WaveHi[40000];
uint32 soundtsinc = 0;
uint32 soundtsoffs = 0;

// ---- fceu.cpp / movie.cpp / RNG ----
int AllocGenieRW(void) { return 0; }
void FlushGenieRW(void) {}
std::string FCEU_MakeFName(int type, int id1, const char *cd1) { return std::string(); }
std::string curMovieFilename;
uint64 xoroshiro128plus_next() { return 0; }
FCEUS FSettings;
X6502 X;
static void MapIRQHookDummy(int a) {}
void (*MapIRQHook)(int a) = MapIRQHookDummy;

// ---- front-end input hooks (main/input.cc in the real app) ----
void GetMouseData(uint32 (&d)[3]) { d[0] = d[1] = d[2] = 0; }
int GetKeyboard() { return 0; }

// ---- cheat.cpp ----
void FCEU_CheatAddRAM(int s, uint32 A, uint8 *p) {}

// ---- vsuni.cpp ----
void FCEU_VSUniCheck(uint64 md5partial, int *MapperNo, uint8 *Mirroring) {}

// ---- state.cpp (AddExState records tags so the test can verify them) ----
struct ExStateRec {
	char desc[8];
	void *ptr;
	uint32 size;
};
static std::vector<ExStateRec> exStates;

void ResetExState(void (*PreSave)(void), void (*PostSave)(void)) {
	exStates.clear();
}

void AddExState(void *v, uint32 s, int type, const char *desc) {
	ExStateRec r = {};
	if (desc) {
		strncpy(r.desc, desc, 7);
		r.desc[7] = 0;
	}
	r.ptr = v;
	r.size = s;
	exStates.push_back(r);
}

bool HeadlessHasExState(const char *tag) {
	for (auto &r : exStates)
		if (!strcmp(r.desc, tag))
			return true;
	return false;
}

// ---- cart.cpp's save helpers live elsewhere in the real app ----
void FCEU_SaveGameSave(CartInfo *LocalHWInfo) {}
void FCEU_ClearGameSave(CartInfo *LocalHWInfo) {}
void FCEU_LoadGameSave(CartInfo *LocalHWInfo) {}

// ---- palette.cpp / video ----
void FCEUI_SetVidSystem(int system) {}

// ---- logging (owned by main/FceuApi.cc) ----
void FCEU_printf(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
}

void FCEU_PrintError(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void FCEUD_PrintError(const char *errormsg) {
	fprintf(stderr, "%s\n", errormsg);
}

void FCEUD_Message(const char *s) {
	fputs(s, stdout);
}

// ---- driver.h surface used by file.cpp / ines.cpp ----
FILE *FCEUD_UTF8fopen(const char *fn, const char *mode) {
	return fopen(fn, mode);
}

EMUFILE_FILE *FCEUD_UTF8_fstream(const char *n, const char *m) {
	return new EMUFILE_FILE(n, m);
}

ArchiveScanRecord FCEUD_ScanArchive(std::string fname) {
	return ArchiveScanRecord();
}

FCEUFILE *FCEUD_OpenArchiveIndex(ArchiveScanRecord &asr, std::string &fname, int innerIndex) { return 0; }
FCEUFILE *FCEUD_OpenArchiveIndex(ArchiveScanRecord &asr, std::string &fname, int innerIndex, int *userCancel) { return 0; }
FCEUFILE *FCEUD_OpenArchive(ArchiveScanRecord &asr, std::string &fname, std::string *innerFilename) { return 0; }
FCEUFILE *FCEUD_OpenArchive(ArchiveScanRecord &asr, std::string &fname, std::string *innerFilename, int *userCancel) { return 0; }

bool FCEUD_PauseAfterPlayback() { return false; }
bool FCEUD_ShouldDrawInputAids() { return false; }
int FCEUD_ShowStatusIcon(void) { return 0; }
void FCEUD_ToggleStatusIcon() {}
void FCEUD_HideMenuToggle(void) {}
void FCEUD_NetworkClose(void) {}
void FCEUD_NetplayText(uint8 *text) {}
int FCEUD_SendData(void *data, uint32 len) { return 1; }
int FCEUD_RecvData(void *data, uint32 len) { return 1; }
void FCEUD_VideoChanged() {}
void FCEUD_SetEmulationSpeed(int cmd) {}
int FCEUD_GetEmulationSpeed(void) { return 0; }
void FCEUD_SoundToggle(void) {}
void FCEUD_SoundVolumeAdjust(int n) {}
void FCEUD_TurboOn(void) {}
void FCEUD_TurboOff(void) {}
void FCEUD_TurboToggle(void) {}
void FCEUD_AviRecordTo() {}
void FCEUD_AviStop() {}
void FCEUD_MovieRecordTo() {}
void FCEUD_MovieReplayFrom() {}
void FCEUD_SaveStateAs() {}
void FCEUD_LoadStateFrom() {}
void FCEUD_FlushTrace() {}
void FCEUD_DebugBreakpoint() {}
const char *FCEUD_GetCompilerString() { return ""; }
uint64 FCEUD_GetTime() { return 0; }
uint64 FCEUD_GetTimeFreq() { return 1000; }
void FCEUD_SetInput(bool fourscore, bool microphone, ESI port0, ESI port1, ESIFC fcexp) {}
void FCEUD_UpdateNTView(int scanline, bool drawall) {}
void FCEUD_UpdatePPUView(int scanline, bool drawall) {}
void FCEUD_TraceInstruction(uint8 *opcode, int size) {}
int FCEUD_FDSReadBIOS(void *buff, uint32 size) { return 0; }
int FCEU_InitVirtualVideo(void) { return 1; }
void FCEU_KillVirtualVideo(void) {}
void FCEU_ResetMessages(void) {}
void FCEU_PutImageDummy(void) {}
void FCEU_PutImage(void) {}
void GetFileBase(const char *horta) {}
