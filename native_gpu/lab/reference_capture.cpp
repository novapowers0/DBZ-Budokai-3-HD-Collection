// Optional research oracle. No RenderDoc DLL is loaded by this code. The
// user must launch the lab through RenderDoc and explicitly enable this flag.
#include "reference_capture.h"
#if defined(DBZ3_REFERENCE_CAPTURE)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <renderdoc_app.h>
#include <immintrin.h>
#include <array>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
namespace dbzlab {namespace {
struct FpuScope {unsigned saved=_mm_getcsr();FpuScope(){_mm_setcsr(0x1f80);}~FpuScope(){_mm_setcsr(saved);}};
std::mutex guard;RENDERDOC_API_1_0_0* api=nullptr;bool attempted=false;int frames=0;
}
void referenceBeforeIndexed() noexcept {
 std::lock_guard lock(guard);if(attempted)return;attempted=true;
 const char* option=std::getenv("DBZ3_CAPTURE_FIRST_3D");if(!option||std::string(option)!="1")return;
 HMODULE module=GetModuleHandleW(L"renderdoc.dll");if(!module)return;
 auto get=reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module,"RENDERDOC_GetAPI"));
 if(!get||!get(eRENDERDOC_API_Version_1_0_0,reinterpret_cast<void**>(&api))||!api)return;
 FpuScope fpu;api->TriggerCapture();frames=1;
 std::ofstream("reference-manual-start.json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"capturing\":"<<api->IsFrameCapturing()<<"}\n";
}
void referenceAfterSwap() noexcept {
 std::lock_guard lock(guard);if(!api||frames<=0)return;
 FpuScope fpu;uint32_t count=api->GetNumCaptures();if(!count)return;frames=0;uint32_t result=1;
 std::array<char,4096> path{};uint32_t length=uint32_t(path.size());uint64_t timestamp=0;
 bool have=count&&api->GetCapture(count-1,path.data(),&length,&timestamp);
 std::string name=have?path.data():"";for(char& c:name)if(c=='\\')c='/';
 std::ofstream("reference-manual-capture.json")<<"{\"process_id\":"<<GetCurrentProcessId()<<",\"result\":"<<result<<",\"path\":\""<<name<<"\",\"timestamp\":"<<timestamp<<"}\n";
}
}
#else
namespace dbzlab {void referenceBeforeIndexed() noexcept {} void referenceAfterSwap() noexcept {}}
#endif
