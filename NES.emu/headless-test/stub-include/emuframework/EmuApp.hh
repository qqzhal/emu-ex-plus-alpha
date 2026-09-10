// Minimal stand-in for the real emuframework/EmuApp.hh, used only by the
// headless test so that fceu/ppu.cpp compiles without the imagine/EmuFramework
// front-end. Provides just the four types referenced by FCEUPPU_Loop's
// signature; the real front-end implementations of FCEUPPU_FrameReady and
// EmuEx::emulateSound are stubbed out in stubs.cc.
#pragma once

namespace EmuEx
{

struct EmuSystemTaskContext
{
};

struct NesSystem
{
};

class EmuVideo
{
};

class EmuAudio
{
};

}
