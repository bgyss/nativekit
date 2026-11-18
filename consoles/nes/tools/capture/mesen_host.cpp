/* Native host for the pinned test reference, never linked into nnes. */
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/MessageManager.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/Video/VideoDecoder.h"
#include "Shared/Video/BaseVideoFilter.h"
#include "Shared/Interfaces/IConsole.h"
#include "Shared/SaveStateManager.h"
#include "Debugger/Debugger.h"
#include "Debugger/ScriptManager.h"
#include "Debugger/MemoryDumper.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"
#include <filesystem>

int main(int argc, char **argv)
{
    // Private interface: ROM, isolated home, combined Lua, result path, WAV.
    if(argc != 6) {
        std::cerr << "usage: nnes-mesen ROM HOME LUA RESULT WAV\n";
        return 64;
    }
    try {
        FolderUtilities::SetHomeFolder(argv[2]);
        Emulator emu;
        auto settings = emu.GetSettings();
        auto &nes = settings->GetNesConfig();
        nes.Region = ConsoleRegion::Ntsc;
        nes.RamPowerOnState = RamState::AllZeros;
        nes.RandomizeCpuPpuAlignment = false;
        nes.RandomizeMapperPowerOnState = false;
        nes.EnableHdPacks = false;
        nes.DisableGameDatabase = true;
        nes.AutoConfigureInput = false;
        nes.Port1.Type = ControllerType::NesController;
        nes.Port2.Type = ControllerType::NesController;
        for(auto &volume : nes.ChannelVolumes) volume = 100;
        // Mesen's default 2C02 palette; configuration never comes from the UI.
        const uint32_t palette[64] = {
            0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
            0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0,0,0,
            0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
            0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0,0,0,
            0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
            0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0,0,
            0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
            0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0,0
        };
        for(int i = 0; i < 64; i++) nes.UserPalette[i] = 0xFF000000 | palette[i];
        auto &preferences = settings->GetPreferences();
        preferences.DisableGameSelectionScreen = true;
        preferences.DisableOsd = true;
        preferences.AutoSaveStateDelay = 0;
        preferences.RewindBufferSize = 0;
        auto &debug = settings->GetDebugConfig();
        debug.ScriptAllowIoOsAccess = true;
        debug.ScriptAllowNetworkAccess = false;
        debug.ScriptTimeout = 5;
        settings->GetAudioConfig().DisableDynamicSampleRate = true;
        settings->SetFlag(EmulationFlags::ConsoleMode);
        settings->SetFlag(EmulationFlags::MaximumSpeed);
        MessageManager::SetOptions(false, false);
        emu.Initialize(false);
        // No emulation thread: scripts are armed before the first opcode.
        if(!emu.LoadRom(VirtualFile(argv[1]), VirtualFile(), false)) {
            std::cerr << "could not load NES image\n" << MessageManager::GetLog();
            emu.Release();
            return 65;
        }
        if(emu.GetConsoleType() != ConsoleType::Nes) {
            emu.Release();
            return 65;
        }
        emu.InitDebugger();
        std::ifstream input(argv[3], std::ios::binary);
        if(!input) throw std::runtime_error("cannot read Lua script");
        string code((std::istreambuf_iterator<char>(input)), {});
        auto scripts = emu.InternalGetDebugger()->GetScriptManager();
        int id = scripts->LoadScript("nnes-capture.lua",
            std::filesystem::path(argv[3]).parent_path().string() + "/", code, -1);
        if(!emu.CaptureStopRequested()) {
            emu.GetSoundMixer()->StartRecording(argv[5]);
            emu.Run();
            emu.GetSoundMixer()->StopRecording();
        }
        int result = emu.GetStopCode();
        auto artifacts = std::filesystem::path(argv[5]).parent_path();
        if(result == 0) {
            emu.GetVideoDecoder()->WaitForAsyncFrameDecode();
            std::stringstream png;
            // A script may stop before any completed frame. Decode the PPU
            // buffer explicitly instead of depending on a desktop renderer.
            auto ppu = emu.GetPpuFrame();
            unique_ptr<BaseVideoFilter> filter(emu.GetVideoFilter(true));
            filter->SetBaseFrameInfo({ppu.Width, ppu.Height});
            filter->SendFrame((uint16_t*)ppu.FrameBuffer, ppu.FrameCount, 0, nullptr);
            filter->TakeScreenshot(VideoFilterType::None, "", &png);
            std::ofstream shot(artifacts / "final.png", std::ios::binary);
            shot << png.str();
            auto dumper = emu.InternalGetDebugger()->GetMemoryDumper();
            for(auto item : {std::make_pair(MemoryType::NesInternalRam, "ram.bin"),
                             std::make_pair(MemoryType::NesSpriteRam, "oam.bin"),
                             std::make_pair(MemoryType::NesNametableRam, "vram.bin")}) {
                std::vector<uint8_t> bytes(dumper->GetMemorySize(item.first));
                dumper->GetMemoryState(item.first, bytes.data());
                std::ofstream dump(artifacts / item.second, std::ios::binary);
                dump.write((const char*)bytes.data(), bytes.size());
            }
            // Run() has returned; the ostream overload avoids waiting for a
            // debugger break on an emulation thread that is no longer running.
            std::ofstream state(artifacts / "final.mss", std::ios::binary);
            emu.GetSaveStateManager()->SaveState(state);
        }
        std::cerr << scripts->GetScriptLog(id) << "\n" << MessageManager::GetLog();
        std::ofstream summary(argv[4]);
        summary << "{\"version\":1,\"stop_code\":" << result
                << ",\"frames\":" << emu.GetFrameCount()
                << ",\"master_clock\":" << emu.GetMasterClock() << "}\n";
        summary.close();
        emu.Stop(false, true, false);
        emu.Release();
        return result == 0 ? 0 : 65;
    } catch(const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 65;
    }
}
