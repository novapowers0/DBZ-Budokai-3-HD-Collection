// Isolated EU research host. DBZ3_NATIVE_GRAPHICS=1 injects our own graphics
// services; the default mode is retained for reference comparisons.
#include <rex/rex_app.h>
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/kernel/init.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem.h>
#include <rex/cvar.h>
#include <rex/ui/window.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/logging.h>
#include <algorithm>
#include <stdexcept>
#include <windows.h>
#include <dbghelp.h>
#include <fstream>
#include <exception>
#include "native_loading.h"
#include "native_indexed.h"
#include "native_stream.h"
#include "native_services.h"
extern const rex::PPCImageInfo PPCImageConfig;
class EuDevice final:public rex::filesystem::HostPathDevice {
public:
    EuDevice(const std::filesystem::path& root):HostPathDevice("\\Device\\Harddisk0\\Partition1",root,true){}
    rex::filesystem::Entry* ResolvePath(std::string_view path) override {
        std::string mapped(path);std::replace(mapped.begin(),mapped.end(),'/','\\');
        if(mapped=="us")mapped="eu";
        else if(mapped.starts_with("us\\"))mapped.replace(0,2,"eu");
        if(mapped=="default.xex")mapped="yae3_xenon.xex";
        if(auto* entry=HostPathDevice::ResolvePath(mapped))return entry;
        // Regional audio/video are flat in this extracted disc copy.
        if(mapped.find('\\')==std::string::npos)return HostPathDevice::ResolvePath("eu\\"+mapped);
        return nullptr;
    }
};
class NativeLab final:public rex::ReXApp {
public:
    explicit NativeLab(rex::ui::WindowedAppContext& ctx):ReXApp(ctx,"dbz3-gpu-lab",PPCImageConfig){
        SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception)->LONG {
            HANDLE file=CreateFileW(L"lab-crash.dmp",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
            if(file!=INVALID_HANDLE_VALUE){MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,nullptr,nullptr);CloseHandle(file);}
            std::ofstream("lab-crash.txt")<<"exception="<<std::hex<<exception->ExceptionRecord->ExceptionCode
                <<" address="<<exception->ExceptionRecord->ExceptionAddress<<" thread="<<std::dec<<GetCurrentThreadId()<<'\n';
            return EXCEPTION_EXECUTE_HANDLER;
        });
        std::set_terminate([](){std::ofstream("lab-terminate.txt")<<"Unhandled C++ exception / terminate\n";std::abort();});
    }
    static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx){return std::make_unique<NativeLab>(ctx);}
protected:
    void OnShutdown() override {dbzlab::shadowShutdown();dbzlab::worldShutdown();dbzlab::streamShutdown();}
    void OnConfigurePaths(rex::PathConfig& paths) override {
        auto root=rex::filesystem::GetExecutableFolder();
        paths.user_data_root=root/"lab-user";paths.cache_root=root/"lab-cache";
        paths.config_path=root/"lab-config.toml";
    }
    void OnPreSetup(rex::RuntimeConfig& config) override {
        config.gpu_plugin="xenos";
        if(dbzlab::nativeServicesEnabled()){config.graphics=dbzlab::createNativeServices();config.gpu_plugin.clear();}
        config.audio_factory=REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
        config.input_factory=REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
        config.kernel_init=rex::kernel::InitializeKernel;
    }
    void OnLoadXexImage(std::string& path) override {path="game:\\yae3_xenon.xex";}
    void OnPostSetup() override {
        if(dbzlab::nativeServicesEnabled()){
            auto* handle=window()->GetNativeWindowHandle();
            if(!handle)throw std::runtime_error("Native GPU window handle unavailable");
            dbzlab::streamSetPresentationWindow(handle);
            window()->SetTitle("Budokai 3 | GPU nativa D3D12 | laboratorio");
        }
        rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants","true");
        rex::cvar::SetFlagByName("frame_cap","60");
    }
    void OnPreLaunchModule() override {
        auto* fs=runtime()->file_system();
        fs->UnregisterDevice("\\Device\\Harddisk0\\Partition1");
        fs->UnregisterDevice("\\Device\\Harddisk0");
        auto device=std::make_unique<EuDevice>(game_data_root());
        if(!device->Initialize()||!fs->RegisterDevice(std::move(device)))throw std::runtime_error("EU research data mount failed");
        auto null=std::make_unique<rex::filesystem::NullDevice>("\\Device\\Harddisk0",std::initializer_list<std::string>{"Partition0","Partition2"});
        null->Initialize();fs->RegisterDevice(std::move(null));
        REXLOG_INFO("Native GPU lab: EU guest boot, native services={}",dbzlab::nativeServicesEnabled());
    }
};
REX_DEFINE_APP(dbz3_gpu_lab,NativeLab::Create)
