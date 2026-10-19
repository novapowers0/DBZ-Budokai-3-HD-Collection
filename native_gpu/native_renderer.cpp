// Budokai 3 host renderer: direct D3D12 buffers, materials and presentation.
// This executable validates the native material path on player-owned models.
// It does not claim to replace the game's live renderer yet.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#define DBZ_NATIVE_GPU_BUILD
#include "native_gpu.h"
#if defined(DBZ_NATIVE_DLSS)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#endif

static void check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char text[200];
        std::snprintf(text, sizeof(text), "%s failed (HRESULT 0x%08lX)", operation, (unsigned long)result);
        throw std::runtime_error(text);
    }
}
struct LegacyVertex { float p[3], n[3], uv[2]; };
struct Vertex { float p[3], n[3], uv[2], color[4]{1,1,1,1}, fog=1; };
struct Draw { uint32_t first, count, base, ramp, blend; float diffuse[3]; };
struct LiveDraw { float matrix[16], material[20]; uint32_t flags; };
struct Texture { uint32_t w, h; std::vector<uint8_t> rgba; uint32_t format=DBZ_NATIVE_RGBA8; };
struct RenderSurface {
    ID3D12Resource *color=nullptr,*depth=nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{},dsv{};
    DbzNativeClear clear{};uint32_t texture=UINT32_MAX;
};
struct Scene {
    std::array<float, 6> bounds;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Draw> draws;
    std::vector<Texture> textures;
    std::vector<LiveDraw> liveDraws;
    std::vector<std::vector<float>> palettes;
    std::vector<DbzNativeRaster> raster;
    DbzNativeClear clear{{.025f,.038f,.07f,1},1,0,3};
    uint32_t width=900,height=900;
};
static_assert(sizeof(LegacyVertex)==32 && sizeof(Vertex)==52 && sizeof(Draw)==32 && sizeof(LiveDraw)==148);
static_assert(sizeof(DbzNativeStencil)==12 && sizeof(DbzNativeAlphaTest)==8 && sizeof(DbzNativeRaster)==60 && sizeof(DbzNativeClear)==28);
static_assert(sizeof(DbzNativeFrameDraw)==256);
static_assert(sizeof(DbzNativeFrameOperation)==44);
static DbzNativeRaster defaultRaster(uint32_t width,uint32_t height){
    return {{0,0,255,255,{7,0,0,0},{7,0,0,0}},{0,7,0,0},{0,0,float(width),float(height),0,1},{0,0,int32_t(width),int32_t(height)}};
}
static void validateRaster(const DbzNativeRaster& r){
    for(float v:r.viewport)if(!std::isfinite(v))throw std::runtime_error("Nonfinite native viewport");
    if(r.viewport[2]<0||r.viewport[3]<0||r.viewport[2]>32767||r.viewport[3]>32767||
        std::abs(r.viewport[0])>32768||std::abs(r.viewport[1])>32768||r.viewport[4]<0||r.viewport[5]>1||r.viewport[4]>r.viewport[5])
        throw std::runtime_error("Invalid native viewport");
    if(r.scissor[0]>r.scissor[2]||r.scissor[1]>r.scissor[3]||r.stencil.enabled>1)throw std::runtime_error("Invalid raster state");
    for(auto face:{r.stencil.front,r.stencil.back})for(int i=0;i<4;++i)if(face[i]>7)throw std::runtime_error("Invalid native stencil operation");
    if(r.alpha.enabled>1||r.alpha.function>7||r.alpha.reserved||!std::isfinite(r.alpha.reference))throw std::runtime_error("Invalid native alpha test");
}
static void validateClear(const DbzNativeClear& c){
    for(float v:c.color)if(!std::isfinite(v))throw std::runtime_error("Nonfinite clear color");
    if(!std::isfinite(c.depth)||c.depth<0||c.depth>1||c.stencil>255||(c.flags&~7u))throw std::runtime_error("Invalid native clear");
}
static void validatePipelineFlags(uint32_t flags,bool extended){
    uint32_t allowed=0xf0f7fu|DBZ_NATIVE_ALPHA_DEST_ZERO|DBZ_NATIVE_ALPHA_DEST_INVERSE|(extended?DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_INPUT_CLIP|DBZ_NATIVE_TOON_CAMERA_W|DBZ_NATIVE_SAMPLE_SCENE|DBZ_NATIVE_LINE_LIST:0u);
    uint32_t alphaDestination=flags&0x3800;
    if((flags&~allowed)||((flags>>2)&3)>2||((flags>>4)&7)>6||
        (alphaDestination&&(alphaDestination&(alphaDestination-1)))||
        (flags&(DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_MASK))==(DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_MASK)||
        ((flags&DBZ_NATIVE_SAMPLE_SCENE)&&(flags&(DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_MASK))))
        throw std::runtime_error("Unsupported live pipeline flags");
}
static uint32_t paletteSize(float mode){
    if(mode==DBZ_NATIVE_GAUSSIAN_13)return 13;
    if(mode==DBZ_NATIVE_DEPTH_OF_FIELD_9)return 4;
    if(mode==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL)return 0;
    if(mode==DBZ_NATIVE_SOLID_COLOR_FOG)return 0;
    if(mode==DBZ_NATIVE_SKIN_TOON||mode==DBZ_NATIVE_SKIN_OUTLINE||mode==DBZ_NATIVE_SKIN_COLOR||mode==DBZ_NATIVE_SKIN_MASK)return 388;
    if(mode==DBZ_NATIVE_RIGID_TOON||mode==DBZ_NATIVE_RIGID_OUTLINE||mode==DBZ_NATIVE_RIGID_MASK)return 4;
    if(mode==DBZ_NATIVE_TEXTURED_COLOR||mode==DBZ_NATIVE_SHADOW||mode==DBZ_NATIVE_FLAT_COLOR||mode==DBZ_NATIVE_WORLD_TOON||mode==DBZ_NATIVE_VIDEO_601||mode==DBZ_NATIVE_FXAA||mode==DBZ_NATIVE_IMAGE_COPY)return 0;
    throw std::runtime_error("Unsupported native material mode");
}
static void validateSkin(float bone,float weight){
    if(bone<0||bone>127||std::floor(bone)!=bone||weight<0||weight>1)
        throw std::runtime_error("Invalid character bone or weight");
}

template<class T> static void read(std::ifstream& file, T* target, size_t count = 1) {
    if (count > std::numeric_limits<size_t>::max() / sizeof(T) ||
        !file.read(reinterpret_cast<char*>(target), count * sizeof(T)))
        throw std::runtime_error("Truncated scene");
}
static Scene load(const char* path) {
    std::ifstream file(path, std::ios::binary);
    char magic[8]; read(file, magic, 8);
    bool extended=std::memcmp(magic,"DBZNGPU4",8)==0;
    bool skinned=extended||std::memcmp(magic,"DBZNGPU3",8)==0;
    bool live=skinned||std::memcmp(magic,"DBZNGPU2",8)==0;
    if (!live && std::memcmp(magic, "DBZNGPU1", 8)) throw std::runtime_error("Invalid scene magic");
    uint32_t n[4]; read(file, n, 4);
    if (!n[0] || n[0] > 1000000 || !n[1] || n[1] > 6000000 ||
        !n[2] || n[2] > 10000 || n[3] > 4096) throw std::runtime_error("Invalid scene counts");
    Scene scene; read(file, scene.bounds.data(), 6);
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(scene.bounds[i]) || !std::isfinite(scene.bounds[i+3]) ||
            scene.bounds[i] > scene.bounds[i+3]) throw std::runtime_error("Invalid scene bounds");
    if(live){
        read(file,&scene.width);read(file,&scene.height);
        if(!scene.width||!scene.height||scene.width>8192||scene.height>8192)throw std::runtime_error("Invalid native frame dimensions");
    }
    if(extended){read(file,&scene.clear);validateClear(scene.clear);}
    scene.vertices.resize(n[0]);
    if(live)read(file,scene.vertices.data(),n[0]);
    else for(auto& v:scene.vertices){LegacyVertex old{};read(file,&old);std::memcpy(&v,&old,sizeof(old));}
    for (const auto& vertex : scene.vertices)
        for (float component : std::array<float,8>{vertex.p[0],vertex.p[1],vertex.p[2],vertex.n[0],
                vertex.n[1],vertex.n[2],vertex.uv[0],vertex.uv[1]})
            if (!std::isfinite(component)) throw std::runtime_error("Nonfinite vertex");
    for(const auto& vertex:scene.vertices){
        for(float c:vertex.color)if(!std::isfinite(c))throw std::runtime_error("Nonfinite vertex color");
        if(!std::isfinite(vertex.fog))throw std::runtime_error("Nonfinite vertex fog");
    }
    scene.indices.resize(n[1]); read(file, scene.indices.data(), n[1]);
    for (uint32_t index : scene.indices)
        if (index >= n[0]) throw std::runtime_error("Invalid vertex index");
    scene.draws.resize(n[2]); read(file, scene.draws.data(), n[2]);
    for (const auto& draw : scene.draws) {
        if (draw.first > n[1] || draw.count > n[1] - draw.first || draw.count % 3 ||
            (draw.base != UINT32_MAX && draw.base >= n[3]) ||
            (draw.ramp != UINT32_MAX && draw.ramp >= n[3])) throw std::runtime_error("Invalid draw");
        if (draw.blend) throw std::runtime_error("This native material path does not implement translucent materials yet");
        for (float c : draw.diffuse) if (!std::isfinite(c)) throw std::runtime_error("Invalid material");
    }
    if(live){
        scene.liveDraws.resize(n[2]);scene.raster.assign(n[2],defaultRaster(scene.width,scene.height));
        if(extended){for(uint32_t i=0;i<n[2];++i){read(file,&scene.liveDraws[i]);read(file,&scene.raster[i]);validateRaster(scene.raster[i]);}}
        else read(file,scene.liveDraws.data(),n[2]);
        for(const auto& state:scene.liveDraws){
            for(float v:state.matrix)if(!std::isfinite(v))throw std::runtime_error("Nonfinite live projection");
            for(float v:state.material)if(!std::isfinite(v))throw std::runtime_error("Nonfinite live material");
            validatePipelineFlags(state.flags,extended);
            paletteSize(state.material[3]);
            if(state.material[3]==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL)throw std::runtime_error("Depth resampling requires streaming native snapshots");
            if(!skinned&&state.material[3]!=1&&state.material[3]!=2)throw std::runtime_error("Version-3 material in older scene");
        }
    }
    if(skinned){
        scene.palettes.resize(n[2]);
        for(size_t d=0;d<n[2];++d){
            uint32_t rows=0;read(file,&rows);
            float mode=scene.liveDraws[d].material[3];bool skin=paletteSize(mode)==388;
            if(rows!=paletteSize(mode))throw std::runtime_error("Invalid character palette size");
            auto& palette=scene.palettes[d];palette.resize(size_t(rows)*4);read(file,palette.data(),palette.size());
            for(float v:palette)if(!std::isfinite(v))throw std::runtime_error("Nonfinite character palette");
            if(skin){
                const auto& draw=scene.draws[d];
                for(uint32_t i=draw.first;i<draw.first+draw.count;++i){
                    const auto& v=scene.vertices[scene.indices[i]];
                    validateSkin(mode==DBZ_NATIVE_SKIN_COLOR?v.n[0]:v.color[0],mode==DBZ_NATIVE_SKIN_COLOR?v.n[1]:v.color[1]);
                }
            }
        }
    }
    size_t total = 0;
    for (uint32_t t = 0; t < n[3]; ++t) {
        uint32_t info[3]; read(file, info, 3);
        uint64_t size = uint64_t(info[0]) * info[1] * 4;
        if (!info[0] || !info[1] || info[0] > 8192 || info[1] > 8192 ||
            size != info[2] || total + size > 512ull*1024*1024) throw std::runtime_error("Invalid texture");
        total += size;
        Texture texture{info[0], info[1], std::vector<uint8_t>(info[2])};
        read(file, texture.rgba.data(), texture.rgba.size());
        scene.textures.push_back(std::move(texture));
    }
    if (file.peek() != EOF) throw std::runtime_error("Unexpected trailing scene data");
    return scene;
}

class Renderer {
    std::vector<IUnknown*> owned;
    template<class T> void keep(T* object) { owned.push_back(object); }
    template<class T> void release(T* object) {
        auto found=std::find(owned.begin(),owned.end(),static_cast<IUnknown*>(object));
        if(found!=owned.end()){owned.erase(found);object->Release();}
    }
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* commands = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE event = nullptr;
    uint64_t serial = 0;
    ID3D12Resource *target = nullptr, *depth = nullptr;
    ID3D12Resource *maskTarget = nullptr, *maskDepth = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE maskRtv{},maskDsv{};
    ID3D12DescriptorHeap *rtv = nullptr, *dsv = nullptr, *srv = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState *materialPso = nullptr, *outlinePso = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    ID3D12Resource* vertexBuffer = nullptr;
    ID3D12Resource* indexBuffer = nullptr;
    uint32_t vertexCount = 0;
    uint32_t vertexCapacity=0,indexCapacity=0,descriptorCapacity=0;bool streamingMode=false,unalignedBlocks=false;
    D3D12_INDEX_BUFFER_VIEW indexView{};
    IDXGIFactory6* factory = nullptr;
    IDXGISwapChain3* swapchain = nullptr;
    uint32_t descriptorSize = 0;
    std::vector<Draw> draws;
    std::array<float, 6> bounds;
    std::array<float, 16> viewProjection{};
    std::array<float, 3> eye{};
    bool liveCamera = false;
    uint32_t width=900,height=900;
    std::vector<LiveDraw> liveDraws;
    std::vector<DbzNativeRaster> liveRaster;
    DbzNativeClear frameClear{};
    std::vector<ID3D12PipelineState*> livePipelines;
    std::array<ID3D12PipelineState*,8> stencilCopyPipelines{};
    D3D12_SHADER_BYTECODE depthCopyShader{},stencilCopyShader{};
    std::vector<bool> visible;
    std::vector<ID3D12Resource*> paletteBuffers;
    std::vector<uint32_t> paletteRows;
    std::vector<uint32_t> skinnedVertices;
    std::vector<uint32_t> colorSkinnedVertices;
    std::vector<ID3D12Resource*> textureObjects;
    std::vector<uint32_t> textureMappings;
    std::vector<RenderSurface> surfaces;
    std::map<uint32_t,uint32_t> surfaceTextures;
    std::map<uint32_t,uint32_t> snapshotTextures;
    std::map<uint32_t,bool> initializedSnapshots;
    std::vector<DbzNativeFrameOperation> operations;
    std::vector<uint32_t> drawTargets;
    uint64_t surfaceBytes=0;
    uint32_t presentInterval=1;
    std::vector<ID3D12Resource*> temporaryUploads;
    ID3D12Resource *whiteTexture=nullptr,*blackTexture=nullptr,*sceneCopy=nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePipeline{};
    std::map<std::string,ID3D12PipelineState*> pipelineCache;
    uint64_t textureBytesUploaded=0,submittedFrames=0;uint32_t textureUploads=0;
    // Optional NVIDIA DLSS (DBZ3_NATIVE_DLSS=1): the frame renders at the guest size and DLSS writes
    // the window-sized output. The guest supplies no motion vectors, so every frame is evaluated on
    // its own (reset, zero motion). Unsupported or failing DLSS falls back to the plain copy.
    bool dlssWanted=false;int dlssState=0;void *ngxParams=nullptr,*dlssFeature=nullptr;
    ID3D12Resource *dlssOutput=nullptr,*dlssMotion=nullptr;HWND presentWindow=nullptr;
    uint32_t outputWidth=0,outputHeight=0;std::chrono::steady_clock::time_point lastPresent{};std::wstring adapterName;

    static void dlssStatus(const std::string& text) noexcept {
        try{std::ofstream("native-dlss-status.txt")<<text<<'\n';}catch(...){}
    }
#if defined(DBZ_NATIVE_DLSS)
    void initDlss() {
        dlssState=-1;
        std::error_code ec;std::wstring data=std::filesystem::temp_directory_path(ec).wstring();
        // nvngx_dlss.dll ships next to the game exe; this DLL lives in native_gpu/
        wchar_t exePath[MAX_PATH]{},selfPath[MAX_PATH]{};HMODULE self=nullptr;
        GetModuleFileNameW(nullptr,exePath,MAX_PATH);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Renderer::dlssStatus),&self);
        GetModuleFileNameW(self,selfPath,MAX_PATH);
        std::wstring exeDir=std::filesystem::path(exePath).parent_path().wstring(),selfDir=std::filesystem::path(selfPath).parent_path().wstring();
        const wchar_t* paths[]={exeDir.c_str(),selfDir.c_str()};
        NVSDK_NGX_FeatureCommonInfo featureInfo{};featureInfo.PathListInfo.Path=paths;featureInfo.PathListInfo.Length=2;
        NVSDK_NGX_Result result=NVSDK_NGX_D3D12_Init_with_ProjectID("6f3c2a1e-8d4b-4c7a-9e21-b3d5f0a7c942",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.5.1",data.empty()?L".":data.c_str(),device,&featureInfo,NVSDK_NGX_Version_API);
        if(NVSDK_NGX_FAILED(result)){dlssStatus("unavailable: NGX init "+std::to_string(uint32_t(result)));return;}
        NVSDK_NGX_Parameter* params=nullptr;int available=0;
        if(NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_GetCapabilityParameters(&params))||!params||
           NVSDK_NGX_FAILED(params->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available))||!available){
            int initResult=0,needsDriver=0,minMajor=0,minMinor=0;
            if(params){params->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,&initResult);
                params->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&needsDriver);
                params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,&minMajor);
                params->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,&minMinor);
                NVSDK_NGX_D3D12_DestroyParameters(params);}
            NVSDK_NGX_D3D12_Shutdown1(device);
            char text[200];std::snprintf(text,sizeof(text),"unavailable: DLSS not supported (init 0x%08X, needs driver %d, min %d.%d, adapter %ls)",
                uint32_t(initResult),needsDriver,minMajor,minMinor,adapterName.c_str());
            dlssStatus(text);return;
        }
        ngxParams=params;dlssState=1;
        // committed default-heap textures start zeroed: zero motion at render resolution
        dlssMotion=resource(textureDescription(width,height,DXGI_FORMAT_R16G16_FLOAT),D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    void releaseDlssFeature() noexcept {
        if(dlssFeature){NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(dlssFeature));dlssFeature=nullptr;}
    }
    void shutdownDlss() noexcept {
        releaseDlssFeature();
        if(ngxParams&&device){NVSDK_NGX_D3D12_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(ngxParams));NVSDK_NGX_D3D12_Shutdown1(device);}
        ngxParams=nullptr;
    }
    // Records DLSS into the frame's command list; false leaves the states as they were.
    bool evaluateDlss() {
        if(!dlssOutput){
            auto od=textureDescription(outputWidth,outputHeight,DXGI_FORMAT_R8G8B8A8_UNORM);od.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            dlssOutput=resource(od,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        auto* params=static_cast<NVSDK_NGX_Parameter*>(ngxParams);
        if(!dlssFeature){
            NVSDK_NGX_DLSS_Create_Params create{};
            create.Feature.InWidth=width;create.Feature.InHeight=height;
            create.Feature.InTargetWidth=outputWidth;create.Feature.InTargetHeight=outputHeight;
            create.Feature.InPerfQualityValue=(width>=outputWidth&&height>=outputHeight)?NVSDK_NGX_PerfQuality_Value_DLAA:NVSDK_NGX_PerfQuality_Value_MaxQuality;
            create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
            NVSDK_NGX_Handle* handle=nullptr;
            NVSDK_NGX_Result result=NGX_D3D12_CREATE_DLSS_EXT(commands,1,1,&handle,params,&create);
            if(NVSDK_NGX_FAILED(result)||!handle){dlssState=-1;dlssStatus("off: feature creation "+std::to_string(uint32_t(result)));return false;}
            dlssFeature=handle;
            dlssStatus("active "+std::to_string(width)+"x"+std::to_string(height)+" -> "+std::to_string(outputWidth)+"x"+std::to_string(outputHeight));
        }
        const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,read);barrier(depth,D3D12_RESOURCE_STATE_DEPTH_WRITE,read);
        auto now=std::chrono::steady_clock::now();
        float deltaMs=lastPresent.time_since_epoch().count()?std::chrono::duration<float,std::milli>(now-lastPresent).count():16.7f;
        NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
        eval.Feature.pInColor=target;eval.Feature.pInOutput=dlssOutput;eval.pInDepth=depth;eval.pInMotionVectors=dlssMotion;
        eval.InRenderSubrectDimensions.Width=width;eval.InRenderSubrectDimensions.Height=height;
        eval.InReset=1;eval.InMVScaleX=eval.InMVScaleY=0;eval.InPreExposure=eval.InExposureScale=1;eval.InFrameTimeDeltaInMsec=deltaMs;
        NVSDK_NGX_Result result=NGX_D3D12_EVALUATE_DLSS_EXT(commands,static_cast<NVSDK_NGX_Handle*>(dlssFeature),params,&eval);
        barrier(depth,read,D3D12_RESOURCE_STATE_DEPTH_WRITE);barrier(target,read,D3D12_RESOURCE_STATE_RENDER_TARGET);
        if(NVSDK_NGX_FAILED(result)){dlssState=-1;dlssStatus("off: evaluation "+std::to_string(uint32_t(result)));return false;}
        return true;
    }
#else
    void initDlss(){dlssState=-1;dlssStatus("unavailable: renderer built without the DLSS SDK");}
    void releaseDlssFeature() noexcept {}
    void shutdownDlss() noexcept {}
    bool evaluateDlss(){return false;}
#endif
    // Swapchain size: the window client with DLSS, the render size otherwise. Frames complete
    // before the next one starts (submit waits), so the buffers can be resized here.
    void fitSwapchain() {
        uint32_t w=width,h=height;RECT client{};
        if(dlssState==1&&presentWindow&&GetClientRect(presentWindow,&client)&&client.right>0&&client.bottom>0){w=uint32_t(client.right);h=uint32_t(client.bottom);}
        if(w==outputWidth&&h==outputHeight)return;
        releaseDlssFeature();if(dlssOutput){release(dlssOutput);dlssOutput=nullptr;}
        check(swapchain->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"Resize native swapchain");
        outputWidth=w;outputHeight=h;
    }

    void dispose() noexcept {
        shutdownDlss();
        if (event) {CloseHandle(event);event=nullptr;}
        for (auto i=owned.rbegin();i!=owned.rend();++i) (*i)->Release();
        owned.clear();
    }

    ID3D12Resource* resource(D3D12_RESOURCE_DESC description, D3D12_HEAP_TYPE heap,
                            D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr) {
        D3D12_HEAP_PROPERTIES properties{}; properties.Type = heap;
        properties.CreationNodeMask = properties.VisibleNodeMask = 1;
        ID3D12Resource* result;
        check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &description,
              state, clear, IID_PPV_ARGS(&result)), "Create resource");
        keep(result); return result;
    }
    static D3D12_RESOURCE_DESC bufferDescription(size_t size) {
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size; d.Height = d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return d;
    }
    static D3D12_RESOURCE_DESC textureDescription(uint32_t w, uint32_t h, DXGI_FORMAT format) {
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w; d.Height = h; d.DepthOrArraySize = d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Format = format; return d;
    }
    void barrier(ID3D12Resource* object, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {object, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
        commands->ResourceBarrier(1, &b);
    }
    ID3D12Resource* upload(const void* bytes, size_t size) {
        auto object = resource(bufferDescription(size), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped; D3D12_RANGE range{0,0}; check(object->Map(0, &range, &mapped), "Map upload");
        std::memcpy(mapped, bytes, size); object->Unmap(0, nullptr); return object;
    }
    ID3D12Resource* uploadTexture(const Texture& texture) {
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC2_UNORM,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8G8_UNORM,DXGI_FORMAT_R32_FLOAT};
        auto desc = textureDescription(texture.w, texture.h, formats[texture.format]);
        auto object = resource(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint; UINT rows; UINT64 rowSize, total;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&total);
        if(uint64_t(rows)*rowSize!=texture.rgba.size())throw std::runtime_error("Native texture footprint does not match input rows");
        auto staging = resource(bufferDescription(total), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        temporaryUploads.push_back(staging);
        void* mapped; D3D12_RANGE range{0,0}; check(staging->Map(0,&range,&mapped), "Map texture upload");
        for (uint32_t y=0; y<rows; ++y)
            std::memcpy(static_cast<uint8_t*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,
                        texture.rgba.data()+y*size_t(rowSize),size_t(rowSize));
        staging->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=object;
        destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=staging;
        source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint=footprint;
        commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        barrier(object,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ++textureUploads;textureBytesUploaded+=texture.rgba.size();
        return object;
    }
    void bindTexture(ID3D12Resource* object,uint32_t index,uint32_t mapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING){
        D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=object->GetDesc().Format;
        if(view.Format==DXGI_FORMAT_R24G8_TYPELESS)view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=mapping; view.Texture2D.MipLevels=1;
        auto cpu=srv->GetCPUDescriptorHandleForHeapStart(); cpu.ptr+=size_t(index)*descriptorSize;
        device->CreateShaderResourceView(object,&view,cpu);
    }
    void bindStencilTexture(ID3D12Resource* object,uint32_t index){
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1,1,1,5);view.Texture2D.MipLevels=1;view.Texture2D.PlaneSlice=1;
        auto cpu=srv->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=size_t(index)*descriptorSize;device->CreateShaderResourceView(object,&view,cpu);
    }
    ID3D12PipelineState* pipelineFor(uint32_t flags,const DbzNativeStencil& source,uint32_t shaderKind=0){
        DbzNativeStencil stencil=source;stencil.reference=0;
        uint32_t f=flags&(0xf3f7f|DBZ_NATIVE_LINE_LIST);
        std::string key(reinterpret_cast<const char*>(&f),sizeof(f));key.append(reinterpret_cast<const char*>(&stencil),sizeof(stencil));key.append(reinterpret_cast<const char*>(&shaderKind),sizeof(shaderKind));
        auto found=pipelineCache.find(key);if(found!=pipelineCache.end())return found->second;
        if(pipelineCache.size()>=4096)throw std::runtime_error("Native pipeline cache limit exceeded");
        auto pipeline=basePipeline;
        pipeline.PrimitiveTopologyType=(f&DBZ_NATIVE_LINE_LIST)?D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        if(shaderKind)pipeline.PS=shaderKind==1?depthCopyShader:stencilCopyShader;
        pipeline.RasterizerState.CullMode=D3D12_CULL_MODE(1+((f>>2)&3));
        pipeline.DepthStencilState.DepthEnable=(f&1)!=0;
        pipeline.DepthStencilState.DepthWriteMask=(f&2)?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC(1+((f>>8)&7));
        pipeline.DepthStencilState.StencilEnable=stencil.enabled;
        pipeline.DepthStencilState.StencilReadMask=stencil.read_mask;pipeline.DepthStencilState.StencilWriteMask=stencil.write_mask;
        auto face=[](const uint8_t* f){return D3D12_DEPTH_STENCILOP_DESC{D3D12_STENCIL_OP(1+f[1]),D3D12_STENCIL_OP(1+f[2]),D3D12_STENCIL_OP(1+f[3]),D3D12_COMPARISON_FUNC(1+f[0])};};
        pipeline.DepthStencilState.FrontFace=face(stencil.front);pipeline.DepthStencilState.BackFace=face(stencil.back);
        auto& blend=pipeline.BlendState.RenderTarget[0];blend={};
        blend.RenderTargetWriteMask=(f>>16)&15;
        UINT mode=(f>>4)&7;blend.BlendEnable=mode!=0;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=(f&DBZ_NATIVE_ALPHA_DEST_ZERO)?D3D12_BLEND_ZERO:(f&DBZ_NATIVE_ALPHA_DEST_INVERSE)?D3D12_BLEND_INV_SRC_ALPHA:(f&0x800)?D3D12_BLEND_ONE:mode==1?D3D12_BLEND_INV_SRC_ALPHA:mode==2||mode==4||mode==5?D3D12_BLEND_ONE:D3D12_BLEND_ZERO;
        blend.SrcBlend=mode==1||mode==4||mode==5?D3D12_BLEND_SRC_ALPHA:mode==3?D3D12_BLEND_DEST_COLOR:D3D12_BLEND_ONE;
        blend.DestBlend=mode==1?D3D12_BLEND_INV_SRC_ALPHA:mode==2||mode==4||mode==5?D3D12_BLEND_ONE:D3D12_BLEND_ZERO;
        if(mode==5)blend.BlendOp=D3D12_BLEND_OP_REV_SUBTRACT;
        if(mode==6){blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_SUBTRACT;}
        ID3D12PipelineState* pso=nullptr;
        check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&pso)),"Create live native pipeline");keep(pso);pipelineCache.emplace(key,pso);return pso;
    }
    ID3DBlob* shader(const char* path, const char* entry, const char* profile) {
        std::ifstream file(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        if (text.empty()) throw std::runtime_error("Shader source missing");
        ID3DBlob *compiled=nullptr,*errors=nullptr;
        HRESULT result=D3DCompile(text.data(),text.size(),path,nullptr,nullptr,entry,profile,
                                 D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&compiled,&errors);
        if (errors) { std::cerr << (char*)errors->GetBufferPointer(); errors->Release(); }
        check(result,"Compile native HLSL"); keep(compiled); return compiled;
    }
    void begin() {
        check(allocator->Reset(),"Reset allocator"); check(commands->Reset(allocator,nullptr),"Reset commands");
    }
    void submit() {
        check(commands->Close(),"Close command list");
        ID3D12CommandList* list=commands; queue->ExecuteCommandLists(1,&list);
        check(queue->Signal(fence,++serial),"Signal fence");
        if (fence->GetCompletedValue()<serial) {
            check(fence->SetEventOnCompletion(serial,event),"Wait fence");
            DWORD wait=WaitForSingleObject(event,15000);
            if (wait!=WAIT_OBJECT_0) throw std::runtime_error("GPU fence timeout / device failure");
        }
        check(device->GetDeviceRemovedReason(),"Device status");
    }
public:
    float yaw=0, zoom=1, rim=0.15f;
    ~Renderer() {dispose();}
    Renderer(const Scene& scene, const char* shaderPath, HWND window): draws(scene.draws),bounds(scene.bounds),
        width(scene.width),height(scene.height),liveDraws(scene.liveDraws),liveRaster(scene.raster),frameClear(scene.clear) {
        visible.resize(draws.size(),true);
        try {
        check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Create DXGI factory"); keep(factory);
        for (UINT index=0; !device; ++index) {
            IDXGIAdapter1* adapter=nullptr;
            HRESULT result=factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter));
            if (result==DXGI_ERROR_NOT_FOUND) break;
            check(result,"Enumerate GPU"); DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
            if (!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) {
                std::wcout << L"Native Direct3D 12 adapter: " << desc.Description << L"\n";
                adapterName=desc.Description;
                keep(device);
            }
            adapter->Release();
        }
        if (!device) throw std::runtime_error("No hardware Direct3D 12 adapter available");
        D3D12_FEATURE_DATA_D3D12_OPTIONS8 options8{};
        if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8,&options8,sizeof(options8))))unalignedBlocks=options8.UnalignedBlockTexturesSupported!=0;
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Create queue"); keep(queue);
        check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"Create allocator"); keep(allocator);
        check(device->CreateCommandList(0,q.Type,allocator,nullptr,IID_PPV_ARGS(&commands)),"Create command list"); keep(commands);
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence"); keep(fence);
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if (!event) throw std::runtime_error("CreateEvent failed");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.NumDescriptors=2; heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtv)),"Create RTV heap"); keep(rtv);
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsv)),"Create DSV heap"); keep(dsv);
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors=draws.size()*3;
        descriptorCapacity=uint32_t(draws.size());
        heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srv)),"Create texture heap"); keep(srv);
        descriptorSize=device->GetDescriptorHandleIncrementSize(heap.Type);
        auto td=textureDescription(width,height,DXGI_FORMAT_R8G8B8A8_UNORM); td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE color{}; color.Format=td.Format; color.Color[0]=0.025f;color.Color[1]=0.038f;color.Color[2]=0.07f;color.Color[3]=1;
        target=resource(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,&color);
        device->CreateRenderTargetView(target,nullptr,rtv->GetCPUDescriptorHandleForHeapStart());
        auto dd=textureDescription(width,height,DXGI_FORMAT_D24_UNORM_S8_UINT); dd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depthClear{};depthClear.Format=dd.Format;depthClear.DepthStencil.Depth=1;
        if(const char* dlss=std::getenv("DBZ3_NATIVE_DLSS");window&&dlss&&std::string(dlss)=="1")dlssWanted=true;
        // DLSS samples the depth: typeless storage, same D24S8 views for drawing
        auto depthStorage=dd;if(dlssWanted)depthStorage.Format=DXGI_FORMAT_R24G8_TYPELESS;
        depth=resource(depthStorage,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depthClear);
        D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=dd.Format;depthView.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth,&depthView,dsv->GetCPUDescriptorHandleForHeapStart());
        uint32_t maskWidth=0,maskHeight=0;bool samplesMask=false,samplesScene=false;
        for(size_t i=0;i<liveDraws.size();++i){
            if(liveDraws[i].flags&DBZ_NATIVE_SAMPLE_MASK)samplesMask=true;
            if(liveDraws[i].flags&DBZ_NATIVE_SAMPLE_SCENE)samplesScene=true;
            if(liveDraws[i].flags&DBZ_NATIVE_TARGET_MASK){
                const auto* v=liveRaster[i].viewport;
                maskWidth=std::max(maskWidth,uint32_t(std::max(1.f,std::ceil(v[0]+v[2]))));
                maskHeight=std::max(maskHeight,uint32_t(std::max(1.f,std::ceil(v[1]+v[3]))));
            }
        }
        if(samplesMask&&!maskWidth)throw std::runtime_error("Native mask sample has no producer");
        if(samplesScene)reserveSceneCopy();
        if(maskWidth){
            if(maskWidth>4096||maskHeight>4096)throw std::runtime_error("Native mask target exceeds limits");
            auto md=textureDescription(maskWidth,maskHeight,DXGI_FORMAT_R8G8B8A8_UNORM);md.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE zero{};zero.Format=md.Format;
            maskTarget=resource(md,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&zero);
            maskRtv=rtv->GetCPUDescriptorHandleForHeapStart();maskRtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(maskTarget,nullptr,maskRtv);
            auto zd=textureDescription(maskWidth,maskHeight,dd.Format);zd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            maskDepth=resource(zd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depthClear);
            maskDsv=dsv->GetCPUDescriptorHandleForHeapStart();maskDsv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            device->CreateDepthStencilView(maskDepth,nullptr,maskDsv);
        }
        vertexBuffer=upload(scene.vertices.data(),scene.vertices.size()*sizeof(Vertex));
        vertexCount=UINT(scene.vertices.size());
        vertexCapacity=vertexCount;indexCapacity=UINT(scene.indices.size());
        vertexView={vertexBuffer->GetGPUVirtualAddress(),UINT(scene.vertices.size()*sizeof(Vertex)),sizeof(Vertex)};
        indexBuffer=upload(scene.indices.data(),scene.indices.size()*sizeof(uint32_t));
        indexView={indexBuffer->GetGPUVirtualAddress(),UINT(scene.indices.size()*sizeof(uint32_t)),DXGI_FORMAT_R32_UINT};
        Texture white{1,1,{255,255,255,0}}, black{1,1,{0,0,0,0}};
        textureObjects.resize(scene.textures.size());
        textureMappings.resize(scene.textures.size(),D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        auto textureFor=[&](uint32_t index,bool ramp){
            ID3D12Resource*& cached=index==UINT32_MAX?(ramp?blackTexture:whiteTexture):textureObjects[index];
            if(!cached)cached=uploadTexture(index==UINT32_MAX?(ramp?black:white):scene.textures[index]);
            return cached;
        };
        for (UINT i=0;i<draws.size();++i) {
            if(!liveDraws.empty()&&(liveDraws[i].flags&(DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE))){
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
                auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=size_t(3*i)*descriptorSize;
                device->CreateShaderResourceView((liveDraws[i].flags&DBZ_NATIVE_SAMPLE_SCENE)?sceneCopy:maskTarget,&view,handle);
            }else bindTexture(textureFor(draws[i].base,false),3*i);
            bindTexture(textureFor(draws[i].ramp,true),3*i+1);
            const auto* palette=scene.palettes.empty()?nullptr:&scene.palettes[i];
            const float zero[4]{};uint32_t rows=palette?UINT(palette->size()/4):0;
            auto buffer=upload(rows?palette->data():zero,rows?size_t(rows)*16:16);
            paletteBuffers.push_back(buffer);paletteRows.push_back(rows);
            if(rows==388)for(uint32_t at=draws[i].first;at<draws[i].first+draws[i].count;++at)
                (liveDraws[i].material[3]==DBZ_NATIVE_SKIN_COLOR?colorSkinnedVertices:skinnedVertices).push_back(scene.indices[at]);
            D3D12_SHADER_RESOURCE_VIEW_DESC description{};
            description.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            description.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            description.Buffer.NumElements=rows?rows:1;description.Buffer.StructureByteStride=16;
            auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=size_t(3*i+2)*descriptorSize;
            device->CreateShaderResourceView(buffer,&description,handle);
        }
        std::sort(skinnedVertices.begin(),skinnedVertices.end());
        skinnedVertices.erase(std::unique(skinnedVertices.begin(),skinnedVertices.end()),skinnedVertices.end());
        std::sort(colorSkinnedVertices.begin(),colorSkinnedVertices.end());
        colorSkinnedVertices.erase(std::unique(colorSkinnedVertices.begin(),colorSkinnedVertices.end()),colorSkinnedVertices.end());
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants={0,0,60}; parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable={1,&range};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC samplers[14]{};
        const D3D12_TEXTURE_ADDRESS_MODE mixed[][2]={{D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP},{D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_MIRROR},{D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_WRAP},{D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_MIRROR},{D3D12_TEXTURE_ADDRESS_MODE_MIRROR,D3D12_TEXTURE_ADDRESS_MODE_WRAP},{D3D12_TEXTURE_ADDRESS_MODE_MIRROR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP}};
        for (int i=0;i<14;++i) {
            auto& s=samplers[i];s.Filter=(i==2||i==3||i==5||(i>=6&&(i&1)))?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU=s.AddressV=s.AddressW=i>=4?D3D12_TEXTURE_ADDRESS_MODE_MIRROR:(i==1||i==2)?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            if(i>=6){s.AddressU=mixed[(i-6)/2][0];s.AddressV=mixed[(i-6)/2][1];}
            s.MaxAnisotropy=1;s.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;s.MaxLOD=D3D12_FLOAT32_MAX;
            if(i>=2)s.MaxLOD=0;
            s.ShaderRegister=i;s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC rootDesc{2,parameters,14,samplers,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ID3DBlob *rootBlob=nullptr,*errors=nullptr;
        HRESULT result=D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,&errors);
        if(errors){std::cerr<<(char*)errors->GetBufferPointer();errors->Release();}
        check(result,"Serialize root signature");keep(rootBlob);
        check(device->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),IID_PPV_ARGS(&root)),"Create root signature");keep(root);
        auto vs=shader(shaderPath,"vertexMain","vs_5_1"), ps=shader(shaderPath,"pixelMain","ps_5_1");
        auto depthPs=shader(shaderPath,"depthCopyPixel","ps_5_1"),stencilPs=shader(shaderPath,"stencilCopyPixel","ps_5_1");depthCopyShader={depthPs->GetBufferPointer(),depthPs->GetBufferSize()};stencilCopyShader={stencilPs->GetBufferPointer(),stencilPs->GetBufferSize()};
        static const D3D12_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root;
        pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pipeline.InputLayout={elements,5};pipeline.SampleMask=UINT_MAX;
        pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable=TRUE;
        pipeline.RasterizerState.FrontCounterClockwise=TRUE;
        pipeline.DepthStencilState.DepthEnable=TRUE;pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        pipeline.DepthStencilState.StencilReadMask=pipeline.DepthStencilState.StencilWriteMask=255;
        pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
        pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=td.Format;pipeline.DSVFormat=dd.Format;pipeline.SampleDesc.Count=1;
        check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&materialPso)),"Create native material pipeline");keep(materialPso);
        pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_FRONT;
        check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&outlinePso)),"Create outline pipeline");keep(outlinePso);
        basePipeline=pipeline;
        for(size_t i=0;i<liveDraws.size();++i)livePipelines.push_back(pipelineFor(liveDraws[i].flags,liveRaster[i].stencil));
        submit();
        for(auto* staging:temporaryUploads)release(staging);temporaryUploads.clear();
        if (window) {
            presentWindow=window;outputWidth=width;outputHeight=height;
            if(dlssWanted)initDlss();
            DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=outputWidth;desc.Height=outputHeight;desc.Format=td.Format;
            desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
            desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
            IDXGISwapChain1* temporary;
            check(factory->CreateSwapChainForHwnd(queue,window,&desc,nullptr,nullptr,&temporary),"Create native swapchain");
            check(temporary->QueryInterface(IID_PPV_ARGS(&swapchain)),"Get swapchain");temporary->Release();keep(swapchain);
            factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
        }
        std::cout << "Native meshes: " << scene.vertices.size() << " vertices, " << scene.indices.size()/3
                  << " triangles, " << draws.size() << " materials.\n";
        } catch(...) {dispose();throw;}
    }
    void enableStreaming(){streamingMode=true;draws.clear();liveDraws.clear();liveRaster.clear();visible.clear();}
    void setPresentInterval(uint32_t interval){if(interval>1)throw std::runtime_error("Invalid native present interval");presentInterval=interval;}
    void reserveGeometry(ID3D12Resource*& buffer,uint32_t& capacity,uint32_t count,size_t stride){
        if(count<=capacity)return;
        uint32_t grown=std::max(1u,capacity);while(grown<count)grown*=2;
        auto replacement=resource(bufferDescription(size_t(grown)*stride),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        if(buffer)release(buffer);buffer=replacement;capacity=grown;
    }
    void reserveDescriptors(uint32_t count){
        if(count<=descriptorCapacity)return;
        uint32_t grown=std::max(8u,descriptorCapacity);while(grown<count)grown*=2;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=grown*3;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ID3D12DescriptorHeap* replacement=nullptr;check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&replacement)),"Grow native descriptors");keep(replacement);
        release(srv);srv=replacement;descriptorCapacity=grown;
    }
    void reserveMask(uint32_t w,uint32_t h){
        if(!w)return;
        if(maskTarget){auto old=maskTarget->GetDesc();if(old.Width>=w&&old.Height>=h)return;w=std::max(w,uint32_t(old.Width));h=std::max(h,old.Height);}
        auto td=textureDescription(w,h,DXGI_FORMAT_R8G8B8A8_UNORM);td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE zero{};zero.Format=td.Format;
        auto color=resource(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&zero);
        auto dd=textureDescription(w,h,DXGI_FORMAT_D24_UNORM_S8_UINT);dd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};clear.Format=dd.Format;clear.DepthStencil.Depth=1;
        auto z=resource(dd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear);
        if(maskTarget)release(maskTarget);if(maskDepth)release(maskDepth);maskTarget=color;maskDepth=z;
        maskRtv=rtv->GetCPUDescriptorHandleForHeapStart();maskRtv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        maskDsv=dsv->GetCPUDescriptorHandleForHeapStart();maskDsv.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        device->CreateRenderTargetView(maskTarget,nullptr,maskRtv);device->CreateDepthStencilView(maskDepth,nullptr,maskDsv);
    }
    uint32_t addTextureBlocks(uint32_t format,uint32_t w,uint32_t h,const uint8_t* rgba,uint32_t bytes,uint32_t swizzle,uint32_t replace=UINT32_MAX){
        if(surfaceTextures.count(replace)||snapshotTextures.count(replace))throw std::runtime_error("CPU uploads cannot replace a native GPU target or snapshot");
        bool blocks=format>=DBZ_NATIVE_BC1&&format<=DBZ_NATIVE_BC3;
        uint64_t expected=blocks?uint64_t((w+3)/4)*((h+3)/4)*(format==DBZ_NATIVE_BC1?8:16):uint64_t(w)*h*(format==DBZ_NATIVE_R8?1:format==DBZ_NATIVE_RG8?2:4);
        if(!streamingMode||!rgba||format>DBZ_NATIVE_R32_FLOAT||!w||!h||w>8192||h>8192||expected!=bytes||(replace==UINT32_MAX?textureObjects.size()>=4096:replace>=textureObjects.size())||(swizzle&~4095u))
            throw std::runtime_error("Invalid streaming texture");
        for(int c=0;c<4;++c)if(((swizzle>>(c*3))&7)>5)throw std::runtime_error("Invalid native texture swizzle");
        if(blocks&&((w|h)&3)&&!unalignedBlocks)throw std::runtime_error("Unaligned native BC textures unavailable");
        uint64_t active=bytes;for(uint32_t i=0;i<textureObjects.size();++i)if(i!=replace){auto desc=textureObjects[i]->GetDesc();
            if(desc.Format==DXGI_FORMAT_BC1_UNORM||desc.Format==DXGI_FORMAT_BC2_UNORM||desc.Format==DXGI_FORMAT_BC3_UNORM)active+=((desc.Width+3)/4)*((desc.Height+3)/4)*(desc.Format==DXGI_FORMAT_BC1_UNORM?8:16);
            else active+=desc.Width*desc.Height*(desc.Format==DXGI_FORMAT_R8_UNORM?1:desc.Format==DXGI_FORMAT_R8G8_UNORM?2:4);
        }
        if(active>1024ull*1024*1024)throw std::runtime_error("Streaming texture memory limit exceeded");
        Texture image{w,h,std::vector<uint8_t>(rgba,rgba+bytes),format};
        begin();auto object=uploadTexture(image);submit();for(auto* staging:temporaryUploads)release(staging);temporaryUploads.clear();
        if(replace!=UINT32_MAX){
            auto old=textureObjects[replace];textureObjects[replace]=object;textureMappings[replace]=swizzle|4096u;
            for(uint32_t i=0;i<draws.size();++i){if(draws[i].base==replace&&!(liveDraws[i].flags&(DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE)))bindTexture(object,3*i,swizzle|4096u);if(draws[i].ramp==replace)bindTexture(object,3*i+1,swizzle|4096u);}
            release(old);return replace;
        }
        textureObjects.push_back(object);textureMappings.push_back(swizzle|4096u);return uint32_t(textureObjects.size()-1);
    }
    uint32_t addTexture(uint32_t w,uint32_t h,const uint8_t* rgba,uint32_t bytes){return addTextureBlocks(DBZ_NATIVE_RGBA8,w,h,rgba,bytes,0x688);}
    void reserveSceneCopy(){if(!sceneCopy)sceneCopy=resource(textureDescription(width,height,DXGI_FORMAT_R8G8B8A8_UNORM),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
    std::pair<uint32_t,uint32_t> addRenderTarget(uint32_t w,uint32_t h,const DbzNativeClear* clear){
        if(!streamingMode||!clear||!w||!h||w>8192||h>8192||surfaces.size()>=64||textureObjects.size()>=4096||uint64_t(w)*h*8+surfaceBytes>512ull*1024*1024)throw std::runtime_error("Invalid native render target or resource budget");
        validateClear(*clear);RenderSurface surface;surface.clear=*clear;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.NumDescriptors=1;desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;ID3D12DescriptorHeap* colorHeap=nullptr;
        check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&colorHeap)),"Create offscreen RTV heap");keep(colorHeap);surface.rtv=colorHeap->GetCPUDescriptorHandleForHeapStart();
        desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;ID3D12DescriptorHeap* depthHeap=nullptr;
        check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&depthHeap)),"Create offscreen DSV heap");keep(depthHeap);surface.dsv=depthHeap->GetCPUDescriptorHandleForHeapStart();
        auto color=textureDescription(w,h,DXGI_FORMAT_R8G8B8A8_UNORM);color.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;D3D12_CLEAR_VALUE colorClear{};colorClear.Format=color.Format;std::copy_n(clear->color,4,colorClear.Color);
        surface.color=resource(color,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&colorClear);device->CreateRenderTargetView(surface.color,nullptr,surface.rtv);
        auto depth=textureDescription(w,h,DXGI_FORMAT_D24_UNORM_S8_UINT);depth.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;D3D12_CLEAR_VALUE depthClear{};depthClear.Format=depth.Format;depthClear.DepthStencil.Depth=clear->depth;depthClear.DepthStencil.Stencil=UINT8(clear->stencil);
        surface.depth=resource(depth,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&depthClear);device->CreateDepthStencilView(surface.depth,nullptr,surface.dsv);
        surface.texture=uint32_t(textureObjects.size());textureObjects.push_back(surface.color);textureMappings.push_back(D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
        uint32_t targetId=uint32_t(surfaces.size()+1);surfaceTextures.emplace(surface.texture,targetId);surfaces.push_back(surface);surfaceBytes+=uint64_t(w)*h*8;return {targetId,surface.texture};
    }
    uint32_t addSnapshotTexture(uint32_t type,uint32_t w,uint32_t h){
        if(!streamingMode||(type!=DBZ_NATIVE_COPY_COLOR&&type!=DBZ_NATIVE_COPY_DEPTH)||!w||!h||w>8192||h>8192||textureObjects.size()>=4096||uint64_t(w)*h*4+surfaceBytes>512ull*1024*1024)throw std::runtime_error("Invalid native snapshot or resource budget");
        auto desc=textureDescription(w,h,type==DBZ_NATIVE_COPY_COLOR?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R24G8_TYPELESS);
        auto object=resource(desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);uint32_t id=uint32_t(textureObjects.size());textureObjects.push_back(object);textureMappings.push_back(D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);snapshotTextures.emplace(id,type);surfaceBytes+=uint64_t(w)*h*4;return id;
    }
    void validateOperations(const std::vector<DbzNativeFrameOperation>& list){
        uint32_t previous=0;for(const auto& op:list){
            bool mask=op.source_target==DBZ_NATIVE_MASK_SURFACE;
            if(op.before_draw<previous||op.before_draw>draws.size()||(!mask&&op.source_target>surfaces.size())||(mask&&(!maskTarget||!maskDepth)))throw std::runtime_error("Invalid native operation order or source target");previous=op.before_draw;
            if(op.type==DBZ_NATIVE_CLEAR_TARGET){validateClear(op.clear);continue;}
            auto found=snapshotTextures.find(op.destination_texture);
            uint32_t snapshotType=op.type==DBZ_NATIVE_RESTORE_DEPTH?DBZ_NATIVE_COPY_DEPTH:op.type;
            if((op.type!=DBZ_NATIVE_COPY_COLOR&&op.type!=DBZ_NATIVE_COPY_DEPTH&&op.type!=DBZ_NATIVE_RESTORE_DEPTH)||found==snapshotTextures.end()||found->second!=snapshotType)throw std::runtime_error("Invalid native snapshot destination or copy type");
            auto source=mask?(op.type==DBZ_NATIVE_COPY_COLOR?maskTarget:maskDepth):op.source_target?(op.type==DBZ_NATIVE_COPY_COLOR?surfaces[op.source_target-1].color:surfaces[op.source_target-1].depth):(op.type==DBZ_NATIVE_COPY_COLOR?target:depth);
            auto a=source->GetDesc(),b=textureObjects[op.destination_texture]->GetDesc();if(op.type==DBZ_NATIVE_COPY_COLOR?(b.Width>a.Width||b.Height>a.Height):(a.Width!=b.Width||a.Height!=b.Height))throw std::runtime_error("Native snapshot dimensions exceed or differ from its source");
        }
        auto copied=initializedSnapshots;size_t cursor=0;
        auto noteOperation=[&](const DbzNativeFrameOperation& op){
            if(op.type==DBZ_NATIVE_RESTORE_DEPTH){if(!copied[op.destination_texture])throw std::runtime_error("Native depth restored before its snapshot copy");}
            else if(op.type==DBZ_NATIVE_COPY_COLOR||op.type==DBZ_NATIVE_COPY_DEPTH)copied[op.destination_texture]=true;
        };
        for(uint32_t i=0;i<draws.size();++i){
            while(cursor<list.size()&&list[cursor].before_draw==i)noteOperation(list[cursor++]);
            for(uint32_t texture:{(!liveDraws.empty()&&(liveDraws[i].flags&(DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE)))?UINT32_MAX:draws[i].base,draws[i].ramp})if(snapshotTextures.count(texture)&&!copied[texture])throw std::runtime_error("Native snapshot sampled before its copy");
        }
        while(cursor<list.size())noteOperation(list[cursor++]);
    }
    void setFrameOperations(const DbzNativeFrameOperation* input,uint32_t count){
        if(!streamingMode||count>10000||(count&&!input))throw std::runtime_error("Invalid native frame operations");
        std::vector<DbzNativeFrameOperation> next;if(count)next.assign(input,input+count);validateOperations(next);operations=std::move(next);
    }
    void setFrame(const DbzNativeFrameVertex* vertices,uint32_t vc,const uint32_t* indices,uint32_t ic,const DbzNativeFrameDraw* input,uint32_t dc,const DbzNativeClear* clear){
        if(!streamingMode||!clear||vc>1000000||ic>6000000||dc>10000||(vc&&!vertices)||(ic&&!indices)||(dc&&!input))throw std::runtime_error("Invalid streaming frame arrays");
        validateClear(*clear);
        for(uint32_t i=0;i<vc;++i){float values[13];std::memcpy(values,vertices+i,sizeof(values));for(float v:values)if(!std::isfinite(v))throw std::runtime_error("Nonfinite streaming vertex");}
        for(uint32_t i=0;i<ic;++i)if(indices[i]>=vc)throw std::runtime_error("Streaming index outside vertex range");
        uint32_t maskW=0,maskH=0;bool samplesMask=false,samplesScene=false;
        std::vector<uint32_t> skin,colorSkin;std::vector<bool> produced(surfaces.size()+1,false);
        for(uint32_t i=0;i<dc;++i){const auto& d=input[i];
            if(d.reserved>surfaces.size()||d.first_index>ic||d.index_count>ic-d.first_index||d.index_count%((d.flags&DBZ_NATIVE_LINE_LIST)?2:3)||
                (d.base_texture!=UINT32_MAX&&d.base_texture>=textureObjects.size())||(d.ramp_texture!=UINT32_MAX&&d.ramp_texture>=textureObjects.size()))throw std::runtime_error("Invalid streaming draw range or texture");
            for(float v:d.view_projection)if(!std::isfinite(v))throw std::runtime_error("Nonfinite streaming matrix");
            for(float v:d.material)if(!std::isfinite(v))throw std::runtime_error("Nonfinite streaming material");
            for(float v:d.diffuse)if(!std::isfinite(v))throw std::runtime_error("Nonfinite streaming diffuse");
            validatePipelineFlags(d.flags,true);validateRaster(d.raster);
            if(d.material[3]==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL){
                auto saved=snapshotTextures.find(d.base_texture);if(saved==snapshotTextures.end()||saved->second!=DBZ_NATIVE_COPY_DEPTH||d.ramp_texture!=d.base_texture||d.index_count!=6||(d.flags&0xf077f)!=0x703||d.flags&(DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE|DBZ_NATIVE_LINE_LIST)||d.raster.stencil.enabled||d.raster.alpha.enabled||d.material[4]!=1||(d.material[5]!=0&&d.material[5]!=1))throw std::runtime_error("Invalid native depth/stencil resample state");
                auto targetDescription=d.reserved?surfaces[d.reserved-1].depth->GetDesc():depth->GetDesc();const float expectedViewport[]={0,0,float(targetDescription.Width),float(targetDescription.Height),0,1};const int32_t expectedScissor[]={0,0,int32_t(targetDescription.Width),int32_t(targetDescription.Height)};
                if(!std::equal(std::begin(expectedViewport),std::end(expectedViewport),d.raster.viewport)||!std::equal(std::begin(expectedScissor),std::end(expectedScissor),d.raster.scissor))throw std::runtime_error("Depth/stencil resampling requires the full destination");
                uint32_t triangleMasks[2]{};
                for(uint32_t k=0;k<6;++k){const auto& v=vertices[indices[d.first_index+k]];float clip[4];if(d.flags&DBZ_NATIVE_INPUT_CLIP){std::copy_n(v.position,3,clip);clip[3]=v.fog;}else for(int row=0;row<4;++row){const auto* m=d.view_projection+row*4;clip[row]=v.position[0]*m[0]+v.position[1]*m[1]+v.position[2]*m[2]+m[3];}
                    if(clip[3]<=0)throw std::runtime_error("Invalid depth resample quad");float x=clip[0]/clip[3],y=clip[1]/clip[3],z=clip[2]/clip[3];
                    if(std::abs(std::abs(x)-1)>1e-5f||std::abs(std::abs(y)-1)>1e-5f||z<0||z>1||std::abs(v.uv[0]-(x+1)*.5f)>1e-5f||std::abs(v.uv[1]-(1-y)*.5f)>1e-5f)throw std::runtime_error("Depth resampling requires a full source quad");triangleMasks[k/3]|=1u<<((x>0?1:0)|(y>0?2:0));
                }
                uint32_t shared=triangleMasks[0]&triangleMasks[1];if((triangleMasks[0]|triangleMasks[1])!=15||(shared!=6&&shared!=9))throw std::runtime_error("Depth resample triangles must cover the destination");
            }
            if(d.reserved&&(d.flags&(DBZ_NATIVE_TARGET_MASK|DBZ_NATIVE_SAMPLE_SCENE)))throw std::runtime_error("Native target conflicts with mask or main scene snapshot");
            for(uint32_t texture:{(d.flags&(DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE))?UINT32_MAX:d.base_texture,d.ramp_texture}){
                auto found=surfaceTextures.find(texture);if(found==surfaceTextures.end())continue;
                if(found->second==d.reserved)throw std::runtime_error("Native render target feedback requires an explicit snapshot");
                if(!produced[found->second])throw std::runtime_error("Native render target sampled before its producer");
            }
            if(d.reserved)produced[d.reserved]=true;
            uint32_t rows=paletteSize(d.material[3]);if(rows!=d.palette_rows||(rows&&!d.palette))throw std::runtime_error("Streaming palette size mismatch");
            for(uint32_t k=0;k<rows*4;++k)if(!std::isfinite(d.palette[k]))throw std::runtime_error("Nonfinite streaming palette");
            if(rows==388)for(uint32_t at=d.first_index;at<d.first_index+d.index_count;++at){uint32_t index=indices[at];bool colored=d.material[3]==DBZ_NATIVE_SKIN_COLOR;
                validateSkin(colored?vertices[index].normal[0]:vertices[index].color[0],colored?vertices[index].normal[1]:vertices[index].color[1]);(colored?colorSkin:skin).push_back(index);
            }
            if(d.flags&DBZ_NATIVE_SAMPLE_MASK)samplesMask=true;
            if(d.flags&DBZ_NATIVE_SAMPLE_SCENE)samplesScene=true;
            if(d.flags&DBZ_NATIVE_TARGET_MASK){const auto* v=d.raster.viewport;float right=v[0]+v[2],bottom=v[1]+v[3];if(right>4096||bottom>4096)throw std::runtime_error("Streaming mask exceeds limits");maskW=std::max(maskW,uint32_t(std::max(1.f,std::ceil(right))));maskH=std::max(maskH,uint32_t(std::max(1.f,std::ceil(bottom))));}
        }
        if(samplesMask&&!maskW&&!maskTarget)throw std::runtime_error("Streaming mask sample has no target");
        std::vector<ID3D12PipelineState*> pipelines;pipelines.reserve(dc);
        for(uint32_t i=0;i<dc;++i){bool copy=input[i].material[3]==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL;pipelines.push_back(pipelineFor(input[i].flags,input[i].raster.stencil,copy?1:0));
            if(copy&&!stencilCopyPipelines[0])for(uint32_t bit=0;bit<8;++bit){DbzNativeStencil stencil{1,0,255,uint8_t(1u<<bit),{7,0,0,2},{7,0,0,2}};stencilCopyPipelines[bit]=pipelineFor(0x700,stencil,2);}
        }
        reserveMask(maskW,maskH);if(samplesScene)reserveSceneCopy();reserveDescriptors(dc);reserveGeometry(vertexBuffer,vertexCapacity,vc,sizeof(Vertex));reserveGeometry(indexBuffer,indexCapacity,ic,sizeof(uint32_t));
        auto copyBuffer=[&](ID3D12Resource* buffer,const void* data,size_t bytes){if(!bytes)return;void* mapped;D3D12_RANGE range{0,0};check(buffer->Map(0,&range,&mapped),"Map streaming buffer");std::memcpy(mapped,data,bytes);buffer->Unmap(0,nullptr);};
        copyBuffer(vertexBuffer,vertices,size_t(vc)*sizeof(Vertex));copyBuffer(indexBuffer,indices,size_t(ic)*sizeof(uint32_t));
        vertexCount=vc;vertexView={vertexBuffer->GetGPUVirtualAddress(),UINT(size_t(vc)*sizeof(Vertex)),sizeof(Vertex)};indexView={indexBuffer->GetGPUVirtualAddress(),ic*4,DXGI_FORMAT_R32_UINT};
        draws.resize(dc);liveDraws.resize(dc);liveRaster.resize(dc);visible.assign(dc,true);drawTargets.resize(dc);
        for(uint32_t i=0;i<dc;++i){const auto& d=input[i];draws[i]={d.first_index,d.index_count,d.base_texture,d.ramp_texture,0,{d.diffuse[0],d.diffuse[1],d.diffuse[2]}};
            std::copy_n(d.view_projection,16,liveDraws[i].matrix);std::copy_n(d.material,20,liveDraws[i].material);liveDraws[i].flags=d.flags;liveRaster[i]=d.raster;drawTargets[i]=d.reserved;
            bindTexture((d.flags&DBZ_NATIVE_SAMPLE_SCENE)?sceneCopy:(d.flags&DBZ_NATIVE_SAMPLE_MASK)?maskTarget:d.base_texture==UINT32_MAX?whiteTexture:textureObjects[d.base_texture],3*i,(d.flags&(DBZ_NATIVE_SAMPLE_MASK|DBZ_NATIVE_SAMPLE_SCENE))||d.base_texture==UINT32_MAX?D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING:textureMappings[d.base_texture]);
            if(d.material[3]==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL)bindStencilTexture(textureObjects[d.ramp_texture],3*i+1);
            else bindTexture(d.ramp_texture==UINT32_MAX?blackTexture:textureObjects[d.ramp_texture],3*i+1,d.ramp_texture==UINT32_MAX?D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING:textureMappings[d.ramp_texture]);
            uint32_t rows=d.palette_rows;size_t bytes=std::max(16u,rows*16);
            if(i>=paletteBuffers.size()){paletteBuffers.push_back(nullptr);paletteRows.push_back(0);}
            if(!paletteBuffers[i]||paletteBuffers[i]->GetDesc().Width<bytes){auto replacement=resource(bufferDescription(bytes),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);if(paletteBuffers[i])release(paletteBuffers[i]);paletteBuffers[i]=replacement;}
            const float zero[4]{};copyBuffer(paletteBuffers[i],rows?d.palette:zero,bytes);paletteRows[i]=rows;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=rows?rows:1;view.Buffer.StructureByteStride=16;
            auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=size_t(3*i+2)*descriptorSize;device->CreateShaderResourceView(paletteBuffers[i],&view,handle);
        }
        for(auto* list:{&skin,&colorSkin}){std::sort(list->begin(),list->end());list->erase(std::unique(list->begin(),list->end()),list->end());}
        skinnedVertices=std::move(skin);colorSkinnedVertices=std::move(colorSkin);livePipelines=std::move(pipelines);frameClear=*clear;operations.clear();
    }
    void updateVertices(const DbzNativeVertex* vertices, uint32_t count) {
        if(!liveDraws.empty())throw std::runtime_error("Live frame scenes require the frame vertex API");
        if (!vertices || count != vertexCount) throw std::runtime_error("Live vertex count does not match the mesh topology");
        for (uint32_t i=0;i<count;++i) {
            const auto& v=vertices[i];
            for (float component : std::array<float,8>{v.position[0],v.position[1],v.position[2],
                v.normal[0],v.normal[1],v.normal[2],v.uv[0],v.uv[1]})
                if (!std::isfinite(component)) throw std::runtime_error("Nonfinite live vertex");
        }
        void* mapped;D3D12_RANGE range{0,0};check(vertexBuffer->Map(0,&range,&mapped),"Map live vertices");
        auto* target=static_cast<Vertex*>(mapped);
        for(uint32_t i=0;i<count;++i)std::memcpy(target+i,vertices+i,sizeof(DbzNativeVertex));
        vertexBuffer->Unmap(0,nullptr);
    }
    void setCamera(const float* matrix,const float* position) {
        if (!matrix || !position) throw std::runtime_error("Missing live camera data");
        for(int i=0;i<16;++i)if(!std::isfinite(matrix[i]))throw std::runtime_error("Nonfinite camera matrix");
        for(int i=0;i<3;++i)if(!std::isfinite(position[i]))throw std::runtime_error("Nonfinite camera position");
        std::copy(matrix,matrix+16,viewProjection.begin());std::copy(position,position+3,eye.begin());liveCamera=true;
    }
    void updateFrameVertices(const DbzNativeFrameVertex* vertices,uint32_t count) {
        static_assert(sizeof(DbzNativeFrameVertex)==sizeof(Vertex));
        if(liveDraws.empty()||!vertices||count!=vertexCount)throw std::runtime_error("Live frame vertex topology mismatch");
        for(uint32_t i=0;i<count;++i){
            float components[13];std::memcpy(components,vertices+i,sizeof(components));
            for(float v:components)if(!std::isfinite(v))throw std::runtime_error("Nonfinite live frame vertex");
        }
        for(uint32_t index:skinnedVertices)validateSkin(vertices[index].color[0],vertices[index].color[1]);
        for(uint32_t index:colorSkinnedVertices)validateSkin(vertices[index].normal[0],vertices[index].normal[1]);
        void* mapped;D3D12_RANGE range{0,0};check(vertexBuffer->Map(0,&range,&mapped),"Map live frame vertices");
        std::memcpy(mapped,vertices,size_t(count)*sizeof(Vertex));vertexBuffer->Unmap(0,nullptr);
    }
    void updateDraw(uint32_t draw,const float* matrix,const float* material) {
        if(draw>=liveDraws.size()||!matrix||!material)throw std::runtime_error("Invalid live draw update");
        for(int i=0;i<16;++i)if(!std::isfinite(matrix[i]))throw std::runtime_error("Nonfinite live draw projection");
        for(int i=0;i<20;++i)if(!std::isfinite(material[i]))throw std::runtime_error("Nonfinite live draw material");
        if(material[3]!=liveDraws[draw].material[3])throw std::runtime_error("Live draw material family cannot change in place");
        auto& state=liveDraws[draw];std::copy(matrix,matrix+16,state.matrix);std::copy(material,material+20,state.material);
    }
    void updatePalette(uint32_t draw,const float* rows,uint32_t count){
        if(draw>=liveDraws.size()||draw>=paletteRows.size()||!rows||!count||paletteRows[draw]!=count)
            throw std::runtime_error("Invalid character palette update");
        for(uint32_t i=0;i<count*4;++i)if(!std::isfinite(rows[i]))throw std::runtime_error("Nonfinite character palette update");
        void* mapped;D3D12_RANGE range{0,0};check(paletteBuffers[draw]->Map(0,&range,&mapped),"Map character palette");
        std::memcpy(mapped,rows,size_t(count)*16);paletteBuffers[draw]->Unmap(0,nullptr);
    }
    void updateRaster(uint32_t draw,const DbzNativeRaster* raster){
        if(!raster||draw>=liveRaster.size())throw std::runtime_error("Invalid live raster update");
        validateRaster(*raster);auto expected=liveRaster[draw].stencil,actual=raster->stencil;
        expected.reference=actual.reference=0;
        if(std::memcmp(&expected,&actual,sizeof(expected)))throw std::runtime_error("Stencil pipeline operations and masks cannot change in place");
        if((liveDraws[draw].flags&DBZ_NATIVE_TARGET_MASK)&&
            (raster->viewport[0]+raster->viewport[2]>float(maskTarget->GetDesc().Width)||raster->viewport[1]+raster->viewport[3]>float(maskTarget->GetDesc().Height)))
            throw std::runtime_error("Updated viewport exceeds fixed mask allocation");
        liveRaster[draw]=*raster;
    }
    void setClear(const DbzNativeClear* clear){if(!clear)throw std::runtime_error("Missing native clear");validateClear(*clear);frameClear=*clear;}
    void render() {
        validateOperations(operations);
        begin();auto rt=rtv->GetCPUDescriptorHandleForHeapStart(),ds=dsv->GetCPUDescriptorHandleForHeapStart();
        if(frameClear.flags&1)commands->ClearRenderTargetView(rt,frameClear.color,0,nullptr);
        D3D12_CLEAR_FLAGS clearFlags=D3D12_CLEAR_FLAGS(((frameClear.flags&2)?D3D12_CLEAR_FLAG_DEPTH:0)|((frameClear.flags&4)?D3D12_CLEAR_FLAG_STENCIL:0));
        if(clearFlags)commands->ClearDepthStencilView(ds,clearFlags,frameClear.depth,UINT8(frameClear.stencil),0,nullptr);
        commands->OMSetRenderTargets(1,&rt,FALSE,&ds);
        int currentTarget=0;
        if(maskTarget){
            const float zero[4]{};barrier(maskTarget,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
            commands->ClearRenderTargetView(maskRtv,zero,0,nullptr);barrier(maskTarget,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        for(const auto& surface:surfaces){
            barrier(surface.color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
            if(surface.clear.flags&1)commands->ClearRenderTargetView(surface.rtv,surface.clear.color,0,nullptr);
            auto flags=D3D12_CLEAR_FLAGS(((surface.clear.flags&2)?D3D12_CLEAR_FLAG_DEPTH:0)|((surface.clear.flags&4)?D3D12_CLEAR_FLAG_STENCIL:0));
            if(flags)commands->ClearDepthStencilView(surface.dsv,flags,surface.clear.depth,UINT8(surface.clear.stencil),0,nullptr);
            barrier(surface.color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        auto selectTarget=[&](int next){
            if(next==currentTarget)return;
            if(currentTarget==-1)barrier(maskTarget,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            else if(currentTarget>0)barrier(surfaces[currentTarget-1].color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            if(next==-1){
                const float zero[4]{};barrier(maskTarget,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
                commands->ClearRenderTargetView(maskRtv,zero,0,nullptr);
                commands->ClearDepthStencilView(maskDsv,D3D12_CLEAR_FLAGS(D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL),1,0,0,nullptr);
                commands->OMSetRenderTargets(1,&maskRtv,FALSE,&maskDsv);
            }else if(next>0){
                const auto& surface=surfaces[next-1];barrier(surface.color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);commands->OMSetRenderTargets(1,&surface.rtv,FALSE,&surface.dsv);
            }else{
                commands->OMSetRenderTargets(1,&rt,FALSE,&ds);
            }
            currentTarget=next;
        };
        size_t operationCursor=0;
        auto applyOperations=[&](uint32_t before){
            while(operationCursor<operations.size()&&operations[operationCursor].before_draw==before){
                const auto& op=operations[operationCursor++];bool main=op.source_target==0,mask=op.source_target==DBZ_NATIVE_MASK_SURFACE;const auto* surface=main||mask?nullptr:&surfaces[op.source_target-1];
                auto* color=main?target:mask?maskTarget:surface->color;auto* z=main?depth:mask?maskDepth:surface->depth;auto colorState=main||(mask?currentTarget==-1:currentTarget==int(op.source_target))?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                if(op.type==DBZ_NATIVE_CLEAR_TARGET){
                    auto colorView=main?rt:mask?maskRtv:surface->rtv,depthView=main?ds:mask?maskDsv:surface->dsv;
                    if(op.clear.flags&1){if(colorState!=D3D12_RESOURCE_STATE_RENDER_TARGET)barrier(color,colorState,D3D12_RESOURCE_STATE_RENDER_TARGET);commands->ClearRenderTargetView(colorView,op.clear.color,0,nullptr);if(colorState!=D3D12_RESOURCE_STATE_RENDER_TARGET)barrier(color,D3D12_RESOURCE_STATE_RENDER_TARGET,colorState);}
                    auto flags=D3D12_CLEAR_FLAGS(((op.clear.flags&2)?D3D12_CLEAR_FLAG_DEPTH:0)|((op.clear.flags&4)?D3D12_CLEAR_FLAG_STENCIL:0));if(flags)commands->ClearDepthStencilView(depthView,flags,op.clear.depth,UINT8(op.clear.stencil),0,nullptr);
                }else if(op.type==DBZ_NATIVE_RESTORE_DEPTH){
                    auto* snapshot=textureObjects[op.destination_texture];barrier(snapshot,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(z,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_DEST);commands->CopyResource(z,snapshot);barrier(z,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_DEPTH_WRITE);barrier(snapshot,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }else{
                    auto* source=op.type==DBZ_NATIVE_COPY_COLOR?color:z;auto sourceState=op.type==DBZ_NATIVE_COPY_COLOR?colorState:D3D12_RESOURCE_STATE_DEPTH_WRITE;auto* destination=textureObjects[op.destination_texture];
                    barrier(source,sourceState,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(destination,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
                    auto a=source->GetDesc(),b=destination->GetDesc();if(a.Width==b.Width&&a.Height==b.Height)commands->CopyResource(destination,source);
                    else{D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source;to.pResource=destination;from.Type=to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_BOX box{0,0,0,UINT(b.Width),b.Height,1};commands->CopyTextureRegion(&to,0,0,0,&from,&box);}
                    barrier(source,D3D12_RESOURCE_STATE_COPY_SOURCE,sourceState);barrier(destination,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
            }
        };
        D3D12_VIEWPORT viewport{0,0,float(width),float(height),0,1};D3D12_RECT scissor{0,0,LONG(width),LONG(height)};
        commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);
        commands->SetGraphicsRootSignature(root);commands->SetDescriptorHeaps(1,&srv);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0,1,&vertexView);commands->IASetIndexBuffer(&indexView);
        float scale=1.8f*zoom/std::max({bounds[3]-bounds[0],bounds[4]-bounds[1],bounds[5]-bounds[2],0.001f});
        float constants[60]={(bounds[0]+bounds[3])*0.5f,(bounds[1]+bounds[4])*0.5f,(bounds[2]+bounds[5])*0.5f,scale,
            std::cos(yaw),std::sin(yaw),float(height)/width,rim, -0.35f,0.7f,0.8f,0, 1,1,1,1};
        constants[16]=eye[0];constants[17]=eye[1];constants[18]=eye[2];constants[19]=liveCamera?1:0;
        std::copy(viewProjection.begin(),viewProjection.end(),constants+20);
        for(int pass=liveDraws.empty()?1:0;pass>=0;--pass){
            constants[11]=float(pass);commands->SetPipelineState(pass?outlinePso:materialPso);
            for(UINT i=0;i<draws.size();++i){
                if(!pass)applyOperations(i);
                if(!visible[i])continue;
                std::copy(draws[i].diffuse,draws[i].diffuse+3,constants+12);constants[15]=draws[i].base!=UINT32_MAX?1:0;
                if(!liveDraws.empty()){
                    constants[19]=(liveDraws[i].flags&DBZ_NATIVE_INPUT_CLIP)?2.f:liveCamera?1.f:0.f;
                    selectTarget((liveDraws[i].flags&DBZ_NATIVE_TARGET_MASK)?-1:drawTargets.empty()?0:int(drawTargets[i]));
                    if(liveDraws[i].flags&DBZ_NATIVE_SAMPLE_SCENE){
                        barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
                        barrier(sceneCopy,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
                        commands->CopyResource(sceneCopy,target);
                        barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
                        barrier(sceneCopy,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
                    const auto& raster=liveRaster[i];const float* v=raster.viewport;
                    D3D12_VIEWPORT viewport{v[0],v[1],v[2],v[3],v[4],v[5]};D3D12_RECT scissor{raster.scissor[0],raster.scissor[1],raster.scissor[2],raster.scissor[3]};
                    commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);commands->OMSetStencilRef(raster.stencil.reference);
                    constants[56]=float(raster.alpha.enabled);constants[57]=float(raster.alpha.function);constants[58]=raster.alpha.reference;
                    constants[59]=(liveDraws[i].flags&DBZ_NATIVE_TOON_CAMERA_W)?liveDraws[i].material[19]:1.f;
                    std::copy(liveDraws[i].matrix,liveDraws[i].matrix+16,constants+20);
                    std::copy(liveDraws[i].material,liveDraws[i].material+20,constants+36);
                    commands->SetPipelineState(livePipelines[i]);
                    commands->IASetPrimitiveTopology((liveDraws[i].flags&DBZ_NATIVE_LINE_LIST)?D3D_PRIMITIVE_TOPOLOGY_LINELIST:D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                }
                commands->SetGraphicsRoot32BitConstants(0,60,constants,0);
                auto handle=srv->GetGPUDescriptorHandleForHeapStart();handle.ptr+=size_t(i*3)*descriptorSize;
                commands->SetGraphicsRootDescriptorTable(1,handle);
                bool depthCopy=!liveDraws.empty()&&liveDraws[i].material[3]==DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL;
                if(depthCopy){auto destinationDepth=currentTarget?surfaces[currentTarget-1].dsv:ds;commands->ClearDepthStencilView(destinationDepth,D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);}
                commands->DrawIndexedInstanced(draws[i].count,1,draws[i].first,0,0);
                if(depthCopy)for(uint32_t bit=0;bit<8;++bit){commands->SetPipelineState(stencilCopyPipelines[bit]);commands->OMSetStencilRef(1u<<bit);float value=float(bit);commands->SetGraphicsRoot32BitConstants(0,1,&value,59);commands->DrawIndexedInstanced(draws[i].count,1,draws[i].first,0,0);}
            }
        }
        applyOperations(uint32_t(draws.size()));
        selectTarget(0);
        ID3D12Resource* backbuffer=nullptr;
        if(swapchain){
            fitSwapchain();
            check(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&backbuffer)),"Get backbuffer");
            ID3D12Resource* source=target;auto sourceState=D3D12_RESOURCE_STATE_RENDER_TARGET;
            if(dlssState==1&&evaluateDlss()){source=dlssOutput;sourceState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;}
            barrier(source,sourceState,D3D12_RESOURCE_STATE_COPY_SOURCE);
            barrier(backbuffer,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
            // a DLSS failure this frame leaves a window-sized backbuffer: the next frame resizes it back
            if(source!=target||(outputWidth==width&&outputHeight==height))commands->CopyResource(backbuffer,source);
            barrier(backbuffer,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
            barrier(source,D3D12_RESOURCE_STATE_COPY_SOURCE,sourceState);
        }
        submit();++submittedFrames;lastPresent=std::chrono::steady_clock::now();
        if(swapchain){auto result=swapchain->Present(presentInterval,0);backbuffer->Release();check(result,"Present native frame");}
        for(const auto& op:operations)if(op.type==DBZ_NATIVE_COPY_COLOR||op.type==DBZ_NATIVE_COPY_DEPTH)initializedSnapshots[op.destination_texture]=true;
    }
    void capture(const char* path) {
        auto desc=target->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;UINT rows;UINT64 rowSize,total;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&total);
        auto staging=resource(bufferDescription(total),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        begin();barrier(target,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=staging;
        destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=target;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        barrier(target,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);submit();
        uint8_t* mapped;D3D12_RANGE range{0,size_t(total)};check(staging->Map(0,&range,(void**)&mapped),"Map native frame");
        std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot write capture");
        file<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x)
            file.write((char*)mapped+footprint.Offset+y*footprint.Footprint.RowPitch+x*4,3);
        D3D12_RANGE written{0,0};staging->Unmap(0,&written);
        release(staging);
        std::cout<<"Captured native GPU frame: "<<path<<"\n";
    }
    void setDrawVisible(uint32_t draw,int value) {
        if(draw>=liveDraws.size()||(value!=0&&value!=1))throw std::runtime_error("Invalid live draw visibility");
        visible[draw]=value!=0;
    }
    DbzNativeStats stats() const{return {uint32_t(draws.size()),textureUploads,uint32_t(pipelineCache.size()),uint32_t(owned.size()),textureBytesUploaded,submittedFrames};}
};

static thread_local std::string abiError;
static Renderer& handle(DbzNativeRenderer renderer) {
    if(!renderer)throw std::runtime_error("Missing native renderer handle");
    return *static_cast<Renderer*>(renderer);
}
static Scene streamingScene(uint32_t width,uint32_t height){
    if(!width||!height||width>8192||height>8192)throw std::runtime_error("Invalid streaming dimensions");
    Scene scene;scene.width=width;scene.height=height;scene.bounds={-1,-1,0,1,1,1};scene.vertices.resize(3);scene.indices={0,1,2};
    scene.draws.push_back({0,3,UINT32_MAX,UINT32_MAX,0,{1,1,1}});
    LiveDraw state{};for(int i=0;i<4;++i)state.matrix[i*4+i]=1;state.material[3]=DBZ_NATIVE_FLAT_COLOR;state.material[8]=state.material[9]=state.material[10]=state.material[11]=1;state.flags=0xf0000|0x700;
    scene.liveDraws.push_back(state);scene.palettes.resize(1);scene.raster.push_back(defaultRaster(width,height));scene.clear.flags=7;return scene;
}
extern "C" DBZ_NATIVE_API const char* dbz_native_last_error() { return abiError.c_str(); }
extern "C" DBZ_NATIVE_API DbzNativeRenderer dbz_native_create(void* hwnd,const char* scene,const char* shaders) {
    try { abiError.clear();if(!scene||!shaders)throw std::runtime_error("Missing native asset path");
        return new Renderer(load(scene),shaders,static_cast<HWND>(hwnd));
    } catch(const std::exception& error){abiError=error.what();return nullptr;}
}
extern "C" DBZ_NATIVE_API DbzNativeRenderer dbz_native_create_stream(void* hwnd,uint32_t width,uint32_t height,const char* shaders){
    try{abiError.clear();if(!shaders)throw std::runtime_error("Missing streaming shader path");auto* renderer=new Renderer(streamingScene(width,height),shaders,static_cast<HWND>(hwnd));renderer->enableStreaming();return renderer;}
    catch(const std::exception& error){abiError=error.what();return nullptr;}
}
extern "C" DBZ_NATIVE_API int dbz_native_add_texture(DbzNativeRenderer renderer,uint32_t width,uint32_t height,const uint8_t* rgba,uint32_t bytes,uint32_t* texture){
    try{abiError.clear();if(!texture)throw std::runtime_error("Missing texture ID output");*texture=handle(renderer).addTexture(width,height,rgba,bytes);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_add_texture_blocks(DbzNativeRenderer renderer,uint32_t format,uint32_t width,uint32_t height,const uint8_t* data,uint32_t bytes,uint32_t swizzle,uint32_t* texture){
    try{abiError.clear();if(!texture)throw std::runtime_error("Missing texture ID output");*texture=handle(renderer).addTextureBlocks(format,width,height,data,bytes,swizzle);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_texture_blocks(DbzNativeRenderer renderer,uint32_t texture,uint32_t format,uint32_t width,uint32_t height,const uint8_t* data,uint32_t bytes,uint32_t swizzle){
    try{abiError.clear();if(texture==UINT32_MAX)throw std::runtime_error("Invalid streaming texture ID");handle(renderer).addTextureBlocks(format,width,height,data,bytes,swizzle,texture);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_frame(DbzNativeRenderer renderer,const DbzNativeFrameVertex* vertices,uint32_t vc,const uint32_t* indices,uint32_t ic,const DbzNativeFrameDraw* draws,uint32_t dc,const DbzNativeClear* clear){
    try{abiError.clear();handle(renderer).setFrame(vertices,vc,indices,ic,draws,dc,clear);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_add_render_target(DbzNativeRenderer renderer,uint32_t width,uint32_t height,const DbzNativeClear* clear,uint32_t* target,uint32_t* texture){
    try{abiError.clear();if(!target||!texture)throw std::runtime_error("Missing native render target ID outputs");auto result=handle(renderer).addRenderTarget(width,height,clear);*target=result.first;*texture=result.second;return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_add_snapshot_texture(DbzNativeRenderer renderer,uint32_t type,uint32_t width,uint32_t height,uint32_t* texture){
    try{abiError.clear();if(!texture)throw std::runtime_error("Missing native snapshot texture ID output");*texture=handle(renderer).addSnapshotTexture(type,width,height);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_frame_operations(DbzNativeRenderer renderer,const DbzNativeFrameOperation* operations,uint32_t count){
    try{abiError.clear();handle(renderer).setFrameOperations(operations,count);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_vertices(DbzNativeRenderer renderer,const DbzNativeVertex* vertices,uint32_t count) {
    try{abiError.clear();handle(renderer).updateVertices(vertices,count);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_camera(DbzNativeRenderer renderer,const float* matrix,const float* eye) {
    try{abiError.clear();handle(renderer).setCamera(matrix,eye);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_frame_vertices(DbzNativeRenderer renderer,const DbzNativeFrameVertex* vertices,uint32_t count) {
    try{abiError.clear();handle(renderer).updateFrameVertices(vertices,count);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_draw(DbzNativeRenderer renderer,uint32_t draw,const float* matrix,const float* material) {
    try{abiError.clear();handle(renderer).updateDraw(draw,matrix,material);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_palette(DbzNativeRenderer renderer,uint32_t draw,const float* rows,uint32_t count) {
    try{abiError.clear();handle(renderer).updatePalette(draw,rows,count);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_update_raster(DbzNativeRenderer renderer,uint32_t draw,const DbzNativeRaster* raster) {
    try{abiError.clear();handle(renderer).updateRaster(draw,raster);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_clear(DbzNativeRenderer renderer,const DbzNativeClear* clear) {
    try{abiError.clear();handle(renderer).setClear(clear);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_render(DbzNativeRenderer renderer) {
    try{abiError.clear();handle(renderer).render();return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_present_interval(DbzNativeRenderer renderer,uint32_t interval){
    try{abiError.clear();handle(renderer).setPresentInterval(interval);return 1;}
    catch(const std::exception& e){abiError=e.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_set_draw_visible(DbzNativeRenderer renderer,uint32_t draw,int visible) {
    try{abiError.clear();handle(renderer).setDrawVisible(draw,visible);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_capture(DbzNativeRenderer renderer,const char* path) {
    try{abiError.clear();if(!path)throw std::runtime_error("Missing capture path");handle(renderer).capture(path);return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API int dbz_native_get_stats(DbzNativeRenderer renderer,DbzNativeStats* stats){
    try{abiError.clear();if(!stats)throw std::runtime_error("Missing stats output");*stats=handle(renderer).stats();return 1;}
    catch(const std::exception& error){abiError=error.what();return 0;}
}
extern "C" DBZ_NATIVE_API void dbz_native_destroy(DbzNativeRenderer renderer) {delete static_cast<Renderer*>(renderer);}

#ifndef DBZ_NATIVE_DLL

static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
int main(int argc,char** argv) {
    try {
        const char *model="assets/goku.dbzgpu",*shaders="materials.hlsl",*capture=nullptr;
        float yaw=0;bool validate=false;
        for(int i=1;i<argc;++i){
            std::string arg=argv[i];
            if(arg=="--validate"){validate=true;continue;}
            if(i+1>=argc)throw std::runtime_error("Missing argument value");
            const char* value=argv[++i];
            if(arg=="--model")model=value;else if(arg=="--shaders")shaders=value;
            else if(arg=="--capture")capture=value;else if(arg=="--yaw")yaw=std::stof(value);
            else throw std::runtime_error("Unknown argument: "+arg);
        }
        Scene scene=load(model);
        if(validate){std::cout<<"Scene validation passed\n";return 0;}
        HWND window=nullptr;
        if(!capture){
            WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);
            cls.lpszClassName=L"DBZ3NativeMaterials";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            if(!RegisterClassW(&cls))throw std::runtime_error("Register window class failed");
            DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
            RECT r{0,0,LONG(scene.width),LONG(scene.height)};AdjustWindowRect(&r,style,FALSE);
            window=CreateWindowW(cls.lpszClassName,L"Budokai 3 | Native D3D12 prototype | Arrows: rotate / zoom | R: rim light | Esc: close",
                                 style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,nullptr);
            if(!window)throw std::runtime_error("Create window failed");
        }
        Renderer renderer(scene,shaders,window);renderer.yaw=yaw;
        if(capture){renderer.render();renderer.capture(capture);return 0;}
        ShowWindow(window,SW_SHOW);MSG message{};bool running=true;
        while(running){
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){running=false;break;}
                if(message.message==WM_KEYDOWN){
                    if(message.wParam==VK_ESCAPE){DestroyWindow(window);running=false;break;}
                    if(message.wParam==VK_LEFT)renderer.yaw-=0.07f;if(message.wParam==VK_RIGHT)renderer.yaw+=0.07f;
                    if(message.wParam==VK_UP)renderer.zoom=std::min(renderer.zoom*1.05f,3.f);
                    if(message.wParam==VK_DOWN)renderer.zoom=std::max(renderer.zoom/1.05f,0.3f);
                    if(message.wParam=='R')renderer.rim=renderer.rim>0?0:0.15f;
                }
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(running)renderer.render();
        }
        return 0;
    } catch(const std::exception& error){std::cerr<<"Native renderer error: "<<error.what()<<"\n";return 1;}
}
#endif
