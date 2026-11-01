// Pase propio de IW: conserva el orden mundo/AME/HUD y las ecuaciones ALPHA
// del GS. No accede a las clases privadas ni cambia las DLL del SDK.
#pragma once
#include "launcher/launcher_state.h"
#include "pasarela.h"
#include "iw_gs_blend.h"
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/logging.h>
#include <imgui.h>
#include <chrono>
#include <map>
#include <memory>
#include <cstring>
#if defined(_WIN32)
// Las DLL canonicas usan R15. El header instalado anterior a R15 omite los
// campos publicos de profundidad y lee command_list en el offset equivocado.
#include "../rexglue-sdk-0.10/include/rex/ui/d3d12/d3d12_presenter.h"
#include <d3dcompiler.h>
#endif

namespace dbz3::iwgs {
// Solo el pintor propio interpreta estos centinelas. Nunca se envian al ImGui
// del SDK (que no ejecuta callbacks arbitrarios).
class Drawer : public rex::ui::UIDrawer {
 public:
  void Draw(rex::ui::UIDrawContext& context) override {
    if(!Visible()) return;
    ImGuiContext* previo=ImGui::GetCurrentContext();
    if(!contexto_) {
      auto* rt=rex::Runtime::instance();
      if(!rt||!rt->imgui_drawer()) return;
      contexto_=ImGui::CreateContext(rt->imgui_drawer()->GetIO().Fonts);
    }
    ImGui::SetCurrentContext(contexto_);
    ImGuiIO& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(float(context.render_target_width()),float(context.render_target_height()));
    const auto ahora=std::chrono::steady_clock::now();
    io.DeltaTime=ultimo_.time_since_epoch().count() ? std::max(.000001f,std::chrono::duration<float>(ahora-ultimo_).count()) : 1.0f/60;
    ultimo_=ahora;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::NewFrame();
    OnDraw(io);
    ImGui::Render();
    Pintar(*ImGui::GetDrawData(),context);
    ImGui::SetCurrentContext(previo);
  }
 protected:
  virtual bool Visible() const=0;
  virtual void OnDraw(ImGuiIO&)=0;
  void ComenzarAtlas() {
#if defined(_WIN32)
    Retirada r{envio_,{}, {}};
    for(auto& [id,t]:texturas_) if(t.recurso) {
      r.recursos.push_back(std::move(t.recurso));r.descriptores.push_back(t.descriptor);
    }
    if(!r.recursos.empty()) retiradas_.push_back(std::move(r));
    texturas_.clear();
#endif
  }
  void RegistrarTextura(ImTextureID id,const pasarela::Imagen& im) {
    if(!id||im.rgba.empty()) return;
#if defined(_WIN32)
    auto& t=texturas_[id];
    // Las direcciones ImmediateTexture pueden reciclarse en la siguiente partida.
    if(t.recurso) retiradas_.push_back({envio_,{t.recurso},{t.descriptor}});
    t={};t.im=im;
    // Misma semantica que ps2::Crear: una CLUT enteramente sin alfa es
    // textura RGB, no una textura totalmente invisible.
    bool opaca=true;
    for(size_t i=3;i<t.im.rgba.size();i+=4) if(t.im.rgba[i]) {opaca=false;break;}
    if(opaca) for(size_t i=3;i<t.im.rgba.size();i+=4) t.im.rgba[i]=255;
#endif
  }
 private:
  ImGuiContext* contexto_=nullptr;
  std::chrono::steady_clock::time_point ultimo_{};
#if defined(_WIN32)
  template<class T> using Com=Microsoft::WRL::ComPtr<T>;
  struct Textura { pasarela::Imagen im; Com<ID3D12Resource> recurso; UINT descriptor=0; };
  struct Retirada { UINT64 envio; std::vector<Com<ID3D12Resource>> recursos; std::vector<UINT> descriptores; };
  std::map<ImTextureID,Textura> texturas_;
  std::vector<Retirada> retiradas_;
  Com<ID3D12Device> dispositivo_;
  Com<ID3D12RootSignature> raiz_;
  Com<ID3D12PipelineState> tuberias_[2];
  Com<ID3D12DescriptorHeap> heap_;
  UINT paso_=0,siguiente_=0;
  std::vector<UINT> libres_;
  UINT64 envio_=0;
  bool intento_=false,fallo_=false;
  Com<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE tipo=D3D12_HEAP_TYPE_UPLOAD) {
    Com<ID3D12Resource> r;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=tipo;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    dispositivo_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&r));
    return r;
  }
  bool Preparar(ID3D12GraphicsCommandList* lista) {
    if(intento_) return !fallo_;
    intento_=true;fallo_=true;
    if(FAILED(lista->GetDevice(IID_PPV_ARGS(&dispositivo_)))) return false;
    auto dll=LoadLibraryW(L"d3dcompiler_47.dll");
    auto dx=LoadLibraryW(L"d3d12.dll");
    auto comp=dll ? reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(dll,"D3DCompile")) : nullptr;
    auto serial=dx ? reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(dx,"D3D12SerializeRootSignature")) : nullptr;
    if(!comp||!serial) return false;
    const char* shader="cbuffer C:register(b0){float2 inv;} struct V{float2 p:POSITION;float2 uv:TEXCOORD;float4 c:COLOR;}; struct O{float4 p:SV_Position;float2 uv:TEXCOORD;float4 c:COLOR;}; O vs(V a){O o;o.p=float4((a.p*inv-.5)*float2(2,-2),0,1);o.uv=a.uv;o.c=a.c;return o;} Texture2D t:register(t0);SamplerState s:register(s0);float4 ps(O a):SV_Target{return t.Sample(s,a.uv)*a.c;}";
    Com<ID3DBlob> vs,ps,error,rs;
    if(FAILED(comp(shader,strlen(shader),"iw_gs",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error))||
       FAILED(comp(shader,strlen(shader),"iw_gs",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error))) return false;
    D3D12_DESCRIPTOR_RANGE rango{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER parametros[2]{};
    parametros[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parametros[0].DescriptorTable={1,&rango};parametros[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parametros[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parametros[1].Constants={0,0,2};parametros[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rd{2,parametros,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    if(FAILED(serial(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rs,&error))||
       FAILED(dispositivo_->CreateRootSignature(0,rs->GetBufferPointer(),rs->GetBufferSize(),IID_PPV_ARGS(&raiz_)))) return false;
    const D3D12_INPUT_ELEMENT_DESC elem[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
      {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
      {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=raiz_.Get();
    pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pd.InputLayout={elem,3};pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets=1;pd.RTVFormats[0]=rex::ui::d3d12::D3D12Presenter::kSwapChainFormat;
    auto& b=pd.BlendState.RenderTarget[0];b.BlendEnable=TRUE;b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    for(int i=0;i<2;++i) {
      b.SrcBlend=i ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
      b.DestBlend=i ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
      b.BlendOp=i ? D3D12_BLEND_OP_SUBTRACT : D3D12_BLEND_OP_REV_SUBTRACT;
      if(FAILED(dispositivo_->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&tuberias_[i])))) return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2048;
    hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(dispositivo_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap_)))) return false;
    paso_=dispositivo_->GetDescriptorHandleIncrementSize(hd.Type);fallo_=false;
    REXLOG_INFO("dbz3 iw GS: tuberias42=Cd-Cs*As y06=Cs-Cd*As creadas; mundo/AME/HUD en orden");
    return true;
  }
  bool Subir(Textura& t,ID3D12GraphicsCommandList* lista,Retirada& ret) {
    if(t.recurso) return true;
    if((siguiente_>=2048&&libres_.empty())||t.im.w<=0||t.im.h<=0) return false;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=t.im.w;d.Height=t.im.h;d.DepthOrArraySize=1;d.MipLevels=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    Com<ID3D12Resource> recurso;
    if(FAILED(dispositivo_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&recurso)))) return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    dispositivo_->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto subida=Buffer(bytes);if(!subida) return false;
    void* p=nullptr;D3D12_RANGE cero{};
    if(FAILED(subida->Map(0,&cero,&p))) return false;
    for(int y=0;y<t.im.h;++y) memcpy(static_cast<uint8_t*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
        t.im.rgba.data()+size_t(y)*t.im.w*4,size_t(t.im.w)*4);
    subida->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=subida.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=recurso.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    lista->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    D3D12_RESOURCE_BARRIER barrera{};barrera.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrera.Transition={recurso.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    lista->ResourceBarrier(1,&barrera);
    if(libres_.empty()) t.descriptor=siguiente_++;
    else {t.descriptor=libres_.back();libres_.pop_back();}
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=d.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(t.descriptor)*paso_;
    dispositivo_->CreateShaderResourceView(recurso.Get(),&srv,cpu);ret.recursos.push_back(subida);
    t.recurso=std::move(recurso);
    return true;
  }
#endif
  void Pintar(const ImDrawData& datos,rex::ui::UIDrawContext& contexto) {
    auto* inmediato=launcher::PreviewDrawer();if(!inmediato) return;
    int modo=0;size_t restas=0;
#if defined(_WIN32)
    auto* dx=dynamic_cast<rex::ui::d3d12::D3D12UIDrawContext*>(&contexto);
    if(dx) {
      envio_=dx->submission_index_current();
      retiradas_.erase(std::remove_if(retiradas_.begin(),retiradas_.end(),[&](const auto& r) {
        if(r.envio>dx->submission_index_completed()) return false;
        libres_.insert(libres_.end(),r.descriptores.begin(),r.descriptores.end());return true;
      }),retiradas_.end());
    }
    Retirada ret{envio_,{}};
#endif
    for(int l=0;l<datos.CmdListsCount;++l) {
      const ImDrawList& lista=*datos.CmdLists[l];
      bool abierto=false;
      auto cerrar=[&] { if(abierto) { inmediato->EndDrawBatch();inmediato->End();abierto=false; } };
#if defined(_WIN32)
      Com<ID3D12Resource> vb,ib;
#endif
      for(const auto& c:lista.CmdBuffer) {
        if(c.UserCallback) {
          if(c.UserCallback==Resta) modo=2;
          else if(c.UserCallback==Inversa) modo=3;
          else if(c.UserCallback==rex::ui::kImDrawBlendAdditive) modo=1;
          else modo=0;
          continue;
        }
        if(!c.ElemCount) continue;
#if defined(_WIN32)
        if(modo>=2&&dx&&dx->command_list()&&Preparar(dx->command_list())) {
          auto it=texturas_.find(c.GetTexID());
          if(it!=texturas_.end()) {
            cerrar();auto* cl=dx->command_list();
            if(Subir(it->second,cl,ret)) {
              if(!vb) {
                vb=Buffer(UINT64(lista.VtxBuffer.Size)*sizeof(ImDrawVert));
                ib=Buffer(UINT64(lista.IdxBuffer.Size)*sizeof(ImDrawIdx));
                if(vb&&ib) {
                  void* p=nullptr;D3D12_RANGE cero{};
                  bool valido=SUCCEEDED(vb->Map(0,&cero,&p));
                  if(valido) {memcpy(p,lista.VtxBuffer.Data,size_t(lista.VtxBuffer.Size)*sizeof(ImDrawVert));vb->Unmap(0,nullptr);}
                  if(valido) {
                    valido=SUCCEEDED(ib->Map(0,&cero,&p));
                    if(valido) {memcpy(p,lista.IdxBuffer.Data,size_t(lista.IdxBuffer.Size)*sizeof(ImDrawIdx));ib->Unmap(0,nullptr);}
                  }
                  if(valido) {ret.recursos.push_back(vb);ret.recursos.push_back(ib);}
                  else {vb.Reset();ib.Reset();}
                }
              }
              if(vb&&ib) {
                ID3D12DescriptorHeap* hs[]={heap_.Get()};cl->SetDescriptorHeaps(1,hs);
                cl->SetGraphicsRootSignature(raiz_.Get());cl->SetPipelineState(tuberias_[modo-2].Get());
                auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(it->second.descriptor)*paso_;
                cl->SetGraphicsRootDescriptorTable(0,gpu);const float inv[]={1/datos.DisplaySize.x,1/datos.DisplaySize.y};
                cl->SetGraphicsRoot32BitConstants(1,2,inv,0);
                D3D12_VERTEX_BUFFER_VIEW v{vb->GetGPUVirtualAddress(),UINT(lista.VtxBuffer.Size*sizeof(ImDrawVert)),sizeof(ImDrawVert)};
                D3D12_INDEX_BUFFER_VIEW i{ib->GetGPUVirtualAddress(),UINT(lista.IdxBuffer.Size*sizeof(ImDrawIdx)),sizeof(ImDrawIdx)==2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
                cl->IASetVertexBuffers(0,1,&v);cl->IASetIndexBuffer(&i);cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                const D3D12_VIEWPORT vp{0,0,float(contexto.render_target_width()),float(contexto.render_target_height()),0,1};cl->RSSetViewports(1,&vp);
                const float sx=contexto.render_target_width()/datos.DisplaySize.x,sy=contexto.render_target_height()/datos.DisplaySize.y;
                const D3D12_RECT sc{LONG(std::max(0.0f,c.ClipRect.x*sx)),LONG(std::max(0.0f,c.ClipRect.y*sy)),
                    LONG(std::min(float(contexto.render_target_width()),c.ClipRect.z*sx)),LONG(std::min(float(contexto.render_target_height()),c.ClipRect.w*sy))};
                cl->RSSetScissorRects(1,&sc);cl->DrawIndexedInstanced(c.ElemCount,1,c.IdxOffset,c.VtxOffset,0);++restas;continue;
              }
            }
          }
        }
#endif
        if(!abierto) {
          inmediato->Begin(contexto,datos.DisplaySize.x,datos.DisplaySize.y);
          rex::ui::ImmediateDrawBatch b;
          b.vertices=reinterpret_cast<const rex::ui::ImmediateVertex*>(lista.VtxBuffer.Data);b.vertex_count=lista.VtxBuffer.Size;
          b.indices=lista.IdxBuffer.Data;b.index_count=lista.IdxBuffer.Size;inmediato->BeginDrawBatch(b);abierto=true;
        }
        rex::ui::ImmediateDraw d;d.count=c.ElemCount;d.index_offset=c.IdxOffset;d.base_vertex=c.VtxOffset;
        d.texture=reinterpret_cast<rex::ui::ImmediateTexture*>(c.GetTexID());d.additive=modo==1;
        d.scissor=true;d.scissor_left=c.ClipRect.x;d.scissor_top=c.ClipRect.y;d.scissor_right=c.ClipRect.z;d.scissor_bottom=c.ClipRect.w;
        inmediato->Draw(d);
      }
      cerrar();
    }
#if defined(_WIN32)
    if(!ret.recursos.empty()) retiradas_.push_back(std::move(ret));
#endif
    if(restas&&!medido_) { medido_=true;REXLOG_INFO("dbz3 iw GS: {} dibujos de resta reales en el primer cuadro; alfa y suma restauradas",restas); }
  }
  bool medido_=false;
};
} // namespace dbz3::iwgs
