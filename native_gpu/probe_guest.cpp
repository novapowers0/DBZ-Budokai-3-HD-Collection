// Read-only guest-state probe using hardware execute breakpoints.
// Scope: one explicitly named local dbz3.exe process; no injected code or patched
// instructions. Debug registers are restored before the debugger detaches.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool success,const char* operation) {
    if(!success)throw std::runtime_error(std::string(operation)+": Win32 "+std::to_string(GetLastError()));
}
static bool readMemory(HANDLE process,uint64_t address,void* bytes,size_t size) {
    SIZE_T done=0;
    return ReadProcessMemory(process,reinterpret_cast<void*>(address),bytes,size,&done)&&done==size;
}
struct Registers { DWORD64 dr0,dr1,dr2,dr3,dr6,dr7; };
struct DebugSession {
    DWORD pid;
    bool attached=false;
    DEBUG_EVENT pending{};
    bool hasPending=false;
    std::map<DWORD,Registers> originals;
    std::array<uint64_t,3> targets{};
    void arm(DWORD tid) {
        if(originals.count(tid))return;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,tid);
        if(!thread)return;
        CONTEXT context{};context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(thread,&context)) {
            if(!originals.count(tid)) originals[tid]={context.Dr0,context.Dr1,context.Dr2,context.Dr3,context.Dr6,context.Dr7};
            // Refuse to overwrite a debugger's existing hardware breakpoints.
            if(context.Dr7&0xff) {CloseHandle(thread);throw std::runtime_error("Existing hardware breakpoints; another debugger may be active");}
            context.Dr0=targets[0];context.Dr1=targets[1];context.Dr2=targets[2];
            context.Dr3=0;context.Dr6=0;context.Dr7=0x15;
            if(!SetThreadContext(thread,&context)){CloseHandle(thread);throw std::runtime_error("Set debug registers failed");}
        }
        CloseHandle(thread);
    }
    void restore() noexcept {
        if(!attached)return;
        for(auto& pair:originals) {
            HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,pair.first);
            if(!thread)continue;
            DWORD suspended=SuspendThread(thread);
            if(suspended!=DWORD(-1)) {
                CONTEXT context{};context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
                const auto& r=pair.second;
                context.Dr0=r.dr0;context.Dr1=r.dr1;context.Dr2=r.dr2;
                context.Dr3=r.dr3;context.Dr6=r.dr6;context.Dr7=r.dr7;
                SetThreadContext(thread,&context);ResumeThread(thread);
            }
            CloseHandle(thread);
        }
        if(hasPending){
            ContinueDebugEvent(pending.dwProcessId,pending.dwThreadId,DBG_CONTINUE);
            hasPending=false;
        }
        DebugActiveProcessStop(pid);attached=false;
    }
    ~DebugSession(){restore();}
};
int main(int argc,char** argv) {
    HANDLE process=nullptr;
    try {
        if(argc!=5)throw std::runtime_error("Usage: probe-guest <PID> <exact-game-executable-path> <output-directory> <sample-limit>");
        DWORD pid=std::stoul(argv[1]);int limit=std::stoi(argv[4]);
        if(limit<1||limit>1024)throw std::runtime_error("Sample limit must be 1..1024");
        process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,pid);
        require(process!=nullptr,"Open game process");
        wchar_t image[32768];DWORD length=32768;
        require(QueryFullProcessImageNameW(process,0,image,&length),"Query game path");
        auto actual=std::filesystem::weakly_canonical(std::filesystem::path(image));
        auto expected=std::filesystem::weakly_canonical(std::filesystem::path(argv[2]));
        if(actual!=expected||(actual.filename()!=L"dbz3.exe"&&actual.filename()!=L"dbz3-gpu-lab.exe"))
            throw std::runtime_error("PID does not belong to the explicitly selected Budokai executable");
        const uint64_t guestBase=0x200000000ull;
        const uint32_t imageEnd=0x826D0000,codeBase=0x82080000;
        std::array<uint32_t,3> guests{0x82237B30,0x8223A670,0x8223E0A8};
        DebugSession session{pid};
        for(size_t i=0;i<guests.size();++i) {
            uint64_t slot=guestBase+imageEnd+uint64_t(guests[i]-codeBase)*2;
            require(readMemory(process,slot,&session.targets[i],8),"Read EU function table");
            MEMORY_BASIC_INFORMATION memory{};
            require(VirtualQueryEx(process,reinterpret_cast<void*>(session.targets[i]),&memory,sizeof(memory))!=0,"Validate native function pointer");
            if(memory.Type!=MEM_IMAGE || !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)))
                throw std::runtime_error("Function-table pointer is not executable image memory");
            std::cout<<"EU function "<<std::hex<<guests[i]<<" -> "<<session.targets[i]<<std::dec<<"\n";
        }
        std::filesystem::path output=argv[3];std::filesystem::create_directories(output);
        require(DebugActiveProcess(pid),"Attach debugger");session.attached=true;
        require(DebugSetProcessKillOnExit(FALSE),"Set non-destructive debugger exit");
        bool initial=true;int samples=0,frames=0;ULONGLONG started=GetTickCount64();
        std::ofstream events(output/"events.jsonl",std::ios::app);
        while(samples<limit && GetTickCount64()-started<15000) {
            DEBUG_EVENT event{};
            if(!WaitForDebugEvent(&event,100))continue;
            session.pending=event;session.hasPending=true;
            DWORD status=DBG_CONTINUE;bool finish=false;
            if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
                session.arm(event.dwThreadId);
                if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
                CloseHandle(event.u.CreateProcessInfo.hProcess);CloseHandle(event.u.CreateProcessInfo.hThread);
            } else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT) {
                session.arm(event.dwThreadId);CloseHandle(event.u.CreateThread.hThread);
            } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
                if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
            } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
                finish=true;
            } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
                auto& exception=event.u.Exception.ExceptionRecord;
                if(exception.ExceptionCode==EXCEPTION_BREAKPOINT && initial) {
                    initial=false;
                } else if(exception.ExceptionCode==EXCEPTION_SINGLE_STEP) {
                    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,FALSE,event.dwThreadId);
                    CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER|CONTEXT_DEBUG_REGISTERS;
                    require(thread&&GetThreadContext(thread,&context),"Read stopped CPU context");
                    int point=-1;
                    for(size_t i=0;i<session.targets.size();++i) if(context.Rip==session.targets[i])point=int(i);
                    if(point>=0) {
                        std::array<uint64_t,33> regs{};
                        if(context.Rdx!=guestBase || !readMemory(process,context.Rcx,regs.data(),sizeof(regs))) {
                            CloseHandle(thread);throw std::runtime_error("Unexpected PPC callback ABI; refusing to interpret context");
                        }
                        uint32_t device=uint32_t(regs[0]); // PPCContext starts with r3, r0, r1, r2, r4...
                        if(point==0)++frames;
                        events<<"{\"sample\":"<<samples<<",\"frame\":"<<frames<<",\"guest\":\""<<std::hex<<guests[point]
                              <<"\",\"thread\":"<<std::dec<<event.dwThreadId<<",\"r3_to_r10\":[\""<<std::hex<<uint32_t(regs[0]);
                        for(int r=4;r<=10;++r)events<<"\",\""<<uint32_t(regs[r]);
                        events<<"\"],\"lr\":\""<<regs[32]<<"\"}"<<std::dec<<"\n";events.flush();
                        std::vector<uint8_t> state(0x6400);
                        if(device>=0x10000 && readMemory(process,guestBase+device,state.data(),state.size())) {
                            std::ofstream snapshot(output/("device-"+std::to_string(samples)+".bin"),std::ios::binary);
                            snapshot.write(reinterpret_cast<char*>(state.data()),state.size());
                        }
                        if(point==1){
                            uint32_t count=uint32_t(regs[5]),address=uint32_t(regs[6]),stride=uint32_t(regs[7]);
                            uint64_t size=uint64_t(count)*stride;
                            if(count&&stride>=12&&stride<=128&&size<=4*1024*1024&&address>=0x10000){
                                std::vector<uint8_t> vertices(static_cast<size_t>(size));
                                if(readMemory(process,guestBase+address,vertices.data(),vertices.size())){
                                    std::ofstream file(output/("vertices-"+std::to_string(samples)+".bin"),std::ios::binary);
                                    file.write(reinterpret_cast<char*>(vertices.data()),vertices.size());
                                }
                            }
                            for(auto field:{13068u,13072u}){
                                uint32_t shader=0;
                                if(!readMemory(process,guestBase+device+field,&shader,4))continue;
                                shader=_byteswap_ulong(shader);
                                std::vector<uint8_t> header(2048);
                                if(shader<0x10000||!readMemory(process,guestBase+shader,header.data(),header.size()))continue;
                                std::string prefix="shader-"+std::to_string(samples)+"-"+std::to_string(field);
                                std::ofstream file(output/(prefix+".bin"),std::ios::binary);
                                file.write(reinterpret_cast<char*>(header.data()),header.size());
                                uint32_t constants=uint32_t(header[24])<<24|uint32_t(header[25])<<16|uint32_t(header[26])<<8|header[27];
                                std::vector<uint8_t> data(16384);
                                if(constants>=0x10000&&readMemory(process,guestBase+constants,data.data(),data.size())){
                                    std::ofstream values(output/(prefix+"-constants.bin"),std::ios::binary);
                                    values.write(reinterpret_cast<char*>(data.data()),data.size());
                                }
                            }
                        }
                        ++samples;
                    } else status=DBG_EXCEPTION_NOT_HANDLED;
                    context.Dr6=0;context.EFlags|=0x10000;
                    require(SetThreadContext(thread,&context),"Resume hardware breakpoint");CloseHandle(thread);
                } else status=DBG_EXCEPTION_NOT_HANDLED;
            }
            // Leave the final event stopped. Restoring after resuming allows
            // another execute breakpoint to race with SuspendThread/detach,
            // potentially leaving debug registers installed in the target.
            if(finish||samples>=limit||GetTickCount64()-started>=15000)break;
            require(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status),"Continue debug event");
            session.hasPending=false;
        }
        session.restore();CloseHandle(process);process=nullptr;
        std::cout<<"Captured "<<samples<<" guest calls across "<<frames<<" presentation boundaries; detached.\n";
        return samples?0:2;
    } catch(const std::exception& error) {
        if(process)CloseHandle(process);
        std::cerr<<"Guest probe error: "<<error.what()<<"\n";return 1;
    }
}
