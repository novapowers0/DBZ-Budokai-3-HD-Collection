/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <array>
#include <vector>
#include <memory>
#include <utility>

#include <rex/math.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/d3d12_submission_tracker.h>
#include <rex/ui/presenter.h>
#include <rex/ui/surface.h>

namespace rex::ui::d3d12 {

class D3D12UIDrawContext final : public UIDrawContext {
 public:
  D3D12UIDrawContext(Presenter& presenter, uint32_t render_target_width,
                     uint32_t render_target_height, ID3D12GraphicsCommandList* command_list,
                     UINT64 submission_index_current, UINT64 submission_index_completed)
      : UIDrawContext(presenter, render_target_width, render_target_height),
        command_list_(command_list),
        submission_index_current_(submission_index_current),
        submission_index_completed_(submission_index_completed) {}

  ID3D12GraphicsCommandList* command_list() const { return command_list_.Get(); }
  UINT64 submission_index_current() const { return submission_index_current_; }
  UINT64 submission_index_completed() const { return submission_index_completed_; }

  // dbz3 (R15): scene depth of the painted guest output (R32_FLOAT, in kDbz3TemporalState) and
  // the guest output rectangle in render target pixels (x, y, width, height), for the
  // depth-tested immediate draws. Null when there is none.
  ID3D12Resource* dbz3_depth = nullptr;
  float dbz3_depth_rect[4] = {};
  bool dbz3_depth_inverted = false;

 private:
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  UINT64 submission_index_current_;
  UINT64 submission_index_completed_;
};

class D3D12Presenter final : public Presenter {
 public:
  static constexpr DXGI_FORMAT kGuestOutputFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
  static constexpr D3D12_RESOURCE_STATES kGuestOutputInternalState =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  static constexpr DXGI_FORMAT kGuestOutputIntermediateFormat = DXGI_FORMAT_R10G10B10A2_UNORM;

  // The format used internally by Windows composition.
  static constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

  // dbz3: temporal upscaling inputs that travel with a guest output (one set
  // per mailbox slot). Depth is R32_FLOAT (host depth, 1 = far unless
  // depth_inverted), motion is R16G16_FLOAT in render pixels (current to
  // previous). Both have the guest output size and rest in kDbz3TemporalState.
  static constexpr D3D12_RESOURCE_STATES kDbz3TemporalState =
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  static constexpr DXGI_FORMAT kDbz3DepthFormat = DXGI_FORMAT_R32_FLOAT;
  static constexpr DXGI_FORMAT kDbz3MotionFormat = DXGI_FORMAT_R16G16_FLOAT;
  // Translucent scene draws (shadows, effects): high where the upscaler should
  // trust the current frame over the history, 0 elsewhere.
  static constexpr DXGI_FORMAT kDbz3ReactiveFormat = DXGI_FORMAT_R8_UNORM;
  struct Dbz3TemporalInputs {
    Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    Microsoft::WRL::ComPtr<ID3D12Resource> motion;
    Microsoft::WRL::ComPtr<ID3D12Resource> reactive;
    // Whether depth (and motion, if has_motion) belong to this guest output.
    bool valid = false;
    bool has_motion = false;
    bool reset = false;
    bool depth_inverted = false;
    float jitter_x = 0.0f, jitter_y = 0.0f;
    float frame_time_ms = 16.666f;
    // Increments once per guest frame, so a repaint of the same frame does
    // not feed the temporal history twice.
    uint64_t frame = 0;
  };

  // The callback must use the main direct queue of the provider.
  class D3D12GuestOutputRefreshContext final : public GuestOutputRefreshContext {
   public:
    D3D12GuestOutputRefreshContext(bool& is_8bpc_out_ref, ID3D12Resource* resource,
                                   D3D12Presenter* presenter = nullptr,
                                   uint32_t mailbox_index = UINT32_MAX)
        : GuestOutputRefreshContext(is_8bpc_out_ref),
          resource_(resource),
          presenter_(presenter),
          mailbox_index_(mailbox_index) {}

    // dbz3: the temporal inputs of this guest output, created on first use
    // with the given size. Null if unavailable. valid is cleared on every
    // refresh, the refresher sets it after writing the textures.
    Dbz3TemporalInputs* AcquireDbz3Temporal(uint32_t width, uint32_t height) const;

    // kGuestOutputFormat, supports UAV. The initial state in the callback is
    // kGuestOutputInternalState, and the callback must also transition it back
    // to kGuestOutputInternalState before finishing.
    ID3D12Resource* resource_uav_capable() const { return resource_.Get(); }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12Presenter* presenter_;
    uint32_t mailbox_index_;
  };

  static std::unique_ptr<D3D12Presenter> Create(HostGpuLossCallback host_gpu_loss_callback,
                                                const D3D12Provider& provider) {
    auto presenter =
        std::unique_ptr<D3D12Presenter>(new D3D12Presenter(host_gpu_loss_callback, provider));
    if (!presenter->InitializeSurfaceIndependent()) {
      return nullptr;
    }
    return presenter;
  }

  ~D3D12Presenter();

  const D3D12Provider& provider() const { return provider_; }

  Surface::TypeFlags GetSupportedSurfaceTypes() const override;

  bool CaptureGuestOutput(RawImage& image_out) override;

  void AwaitUISubmissionCompletionFromUIThread(UINT64 submission_index) {
    ui_submission_tracker_.AwaitSubmissionCompletion(submission_index);
  }

 protected:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& new_surface, uint32_t new_surface_width, uint32_t new_surface_height,
      bool was_paintable, bool& is_vsync_implicit_out) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;

  bool RefreshGuestOutputImpl(uint32_t mailbox_index, uint32_t frontbuffer_width,
                              uint32_t frontbuffer_height,
                              std::function<bool(GuestOutputRefreshContext& context)> refresher,
                              bool& is_8bpc_out) override;

  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;

 private:
  struct GuestOutputPaintRectangleConstants {
    union {
      struct {
        float x;
        float y;
      };
      float offset[2];
    };
    union {
      struct {
        float width;
        float height;
      };
      float size[2];
    };
  };

  enum class GuestOutputPaintRootParameter : UINT {
    kSource,
    kRectangle,
    kEffectConstants,

    kCount,
  };

  enum GuestOutputPaintRootSignatureIndex : size_t {
    kGuestOutputPaintRootSignatureIndexBilinear,
#if defined(REX_HAS_FIDELITYFX_SDK)
    kGuestOutputPaintRootSignatureIndexCasSharpen,
    kGuestOutputPaintRootSignatureIndexCasResample,
    kGuestOutputPaintRootSignatureIndexFsrEasu,
    kGuestOutputPaintRootSignatureIndexFsrRcas,
#endif

    kGuestOutputPaintRootSignatureCount,
  };

  static constexpr GuestOutputPaintRootSignatureIndex GetGuestOutputPaintRootSignatureIndex(
      GuestOutputPaintEffect effect) {
    switch (effect) {
      case GuestOutputPaintEffect::kBilinear:
      case GuestOutputPaintEffect::kBilinearDither:
        return kGuestOutputPaintRootSignatureIndexBilinear;
#if defined(REX_HAS_FIDELITYFX_SDK)
      case GuestOutputPaintEffect::kCasSharpen:
      case GuestOutputPaintEffect::kCasSharpenDither:
        return kGuestOutputPaintRootSignatureIndexCasSharpen;
      case GuestOutputPaintEffect::kCasResample:
      case GuestOutputPaintEffect::kCasResampleDither:
        return kGuestOutputPaintRootSignatureIndexCasResample;
      case GuestOutputPaintEffect::kFsrEasu:
        return kGuestOutputPaintRootSignatureIndexFsrEasu;
      case GuestOutputPaintEffect::kFsrRcas:
      case GuestOutputPaintEffect::kFsrRcasDither:
        return kGuestOutputPaintRootSignatureIndexFsrRcas;
#endif
      default:
        assert_unhandled_case(effect);
        return kGuestOutputPaintRootSignatureCount;
    }
  }

  struct PaintContext {
    explicit PaintContext() = default;
    PaintContext(const PaintContext& paint_context) = delete;
    PaintContext& operator=(const PaintContext& paint_context) = delete;

    static constexpr uint32_t kSwapChainBufferCount = 3;

    enum RTVIndex : UINT {
      // Swap chain buffers - updated when creating the swap chain
      // (connection-specific).
      kRTVIndexSwapChainBuffer0,

      // Intermediate textures - the last usage is
      // guest_output_intermediate_texture_paint_last_usage_.
      kRTVIndexGuestOutputIntermediate0 = kRTVIndexSwapChainBuffer0 + kSwapChainBufferCount,

      kRTVCount = kRTVIndexGuestOutputIntermediate0 + kGuestOutputMailboxSize - 1,
    };

    enum ViewIndex : UINT {
      // Guest output textures - indices are the same as in
      // guest_output_resource_paint_refs, and the last usage is tied to them.
      kViewIndexGuestOutput0Srv,

      // Intermediate textures - the last usage is
      // guest_output_intermediate_texture_paint_last_usage_.
      kViewIndexGuestOutputIntermediate0Srv = kViewIndexGuestOutput0Srv + kGuestOutputMailboxSize,

      kViewCount = kViewIndexGuestOutputIntermediate0Srv + kMaxGuestOutputPaintEffects - 1,
    };

    void AwaitSwapChainUsageCompletion() {
      // Presentation engine usage.
      present_submission_tracker.AwaitAllSubmissionsCompletion();
      // Paint (render target) usage. While the presentation fence is signaled
      // on the same queue, and presentation happens after painting, awaiting
      // anyway for safety just to make less assumptions in the architecture.
      paint_submission_tracker.AwaitAllSubmissionsCompletion();
    }

    void DestroySwapChain();

    // Connection-independent.

    // Signaled before presenting.
    D3D12SubmissionTracker paint_submission_tracker;
    // Signaled after presenting.
    D3D12SubmissionTracker present_submission_tracker;

    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kSwapChainBufferCount>
        command_allocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;

    // Descriptor heaps for views of the current resources related to the guest
    // output and to painting, updated either during painting or during
    // connection lifetime management if outdated after awaiting usage
    // completion.
    // RTV heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
    // Shader-visible CBV/SRV/UAV heap.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> view_heap;

    // Refreshed and cleaned up during guest output painting. The first is the
    // paint submission index in which the guest output texture (and its
    // descriptors) was last used, the second is the reference to the texture,
    // which may be null. The indices are not mailbox indices here, rather, if
    // the reference is not in this array yet, the most outdated reference, if
    // needed, is replaced with the new one, awaiting the completion of the last
    // paint usage.
    std::array<std::pair<UINT64, Microsoft::WRL::ComPtr<ID3D12Resource>>, kGuestOutputMailboxSize>
        guest_output_resource_paint_refs;

    // Current intermediate textures for guest output painting, refreshed when
    // painting guest output. While not in use, they are in
    // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE.
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxGuestOutputPaintEffects - 1>
        guest_output_intermediate_textures;
    UINT64 guest_output_intermediate_texture_last_usage = 0;

    // Connection-specific.

    uint32_t swap_chain_width = 0;
    uint32_t swap_chain_height = 0;
    bool swap_chain_allows_tearing = false;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kSwapChainBufferCount> swap_chain_buffers;
  };

  explicit D3D12Presenter(HostGpuLossCallback host_gpu_loss_callback, const D3D12Provider& provider)
      : Presenter(host_gpu_loss_callback), provider_(provider) {}

  bool dxgi_supports_tearing() const { return dxgi_supports_tearing_; }

  bool InitializeSurfaceIndependent();

#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
  bool EnsureTemporalUpscalerContext(uint32_t render_width, uint32_t render_height,
                                     uint32_t output_width, uint32_t output_height,
                                     uint32_t flags = 0);
  bool DispatchTemporalUpscaler(ID3D12GraphicsCommandList* command_list,
                                ID3D12Resource* input_resource, uint32_t input_width,
                                uint32_t input_height, ID3D12Resource* output_resource,
                                uint32_t output_width, uint32_t output_height,
                                const GuestOutputPaintConfig& config,
                                const Dbz3TemporalInputs* temporal);
  void DestroyTemporalUpscalerContext();
#endif

  const D3D12Provider& provider_;

  // Whether DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING is supported by DXGI (depends in
  // particular on the Windows 10 version and hardware support), primarily for
  // variable refresh rate support.
  bool dxgi_supports_tearing_ = false;

  // Static objects for guest output presentation, used only when painting the
  // main target (can be destroyed only after awaiting main target usage
  // completion).
  std::array<Microsoft::WRL::ComPtr<ID3D12RootSignature>, kGuestOutputPaintRootSignatureCount>
      guest_output_paint_root_signatures_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, size_t(GuestOutputPaintEffect::kCount)>
      guest_output_paint_intermediate_pipelines_;
  std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, size_t(GuestOutputPaintEffect::kCount)>
      guest_output_paint_final_pipelines_;

  // DBZ3 (nGPU): tiempo de GPU del pintado del guest output (las pasadas de
  // presentacion: bilineal/FSR/CAS, el resolve final a la ventana) medido con
  // timestamps D3D12. Solo se graba con dbz3_perf_logging; se publica como
  // pres_ms= en la linea `perf` del presentador. Sirve para saber si en una
  // grafica floja el coste esta en las pasadas de presentacion o en el juego.
  static constexpr uint32_t kDbz3PresentTimeSlots = 16;
  // D3D12 pide alinear el destino de ResolveQueryData; 512 B por slot deja 16 B
  // utiles (dos timestamps de 8 B), igual que en el command processor.
  static constexpr uint32_t kDbz3PresentTimeSlotStride = 512;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> dbz3_present_time_heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> dbz3_present_time_readback_;
  uint64_t* dbz3_present_time_mapping_ = nullptr;
  uint64_t dbz3_present_time_freq_ = 0;
  uint64_t dbz3_present_time_fence_values_[kDbz3PresentTimeSlots] = {};
  uint32_t dbz3_present_time_slot_ = 0;
  bool dbz3_present_time_available_ = false;
  bool dbz3_present_time_init_failed_ = false;
  // Reutiliza un slot libre y recoge los ya terminados. Devuelve true si este
  // frame debe grabar timestamps (y deja el slot elegido en slot_out).
  bool Dbz3PresentTimeBegin(uint32_t& slot_out);
  void Dbz3PresentTimeConsume();

  // The first is the refresher submission tracker fence value at which the
  // guest output texture was last refreshed, the second is the reference to the
  // texture, which may be null. The indices are the mailbox indices.
  std::array<std::pair<UINT64, Microsoft::WRL::ComPtr<ID3D12Resource>>, kGuestOutputMailboxSize>
      guest_output_resources_;
  // The guest output resources are protected by two submission trackers - the
  // refresher ones (for writing to them via the guest_output_resources_
  // references) and the paint one (for presenting it via the
  // paint_context_.guest_output_resource_paint_refs references taken from
  // guest_output_resources_).
  D3D12SubmissionTracker guest_output_resource_refresher_submission_tracker_;

  // UI submission tracker with the submission index that can be given to UI
  // drawers (accessible from the UI thread only, at any time).
  D3D12SubmissionTracker ui_submission_tracker_;

  // Accessible only by painting and by surface connection lifetime management
  // (ConnectOrReconnectPaintingToSurfaceFromUIThread,
  // DisconnectPaintingFromSurfaceFromUIThreadImpl) by the thread doing it, as
  // well as by presenter initialization and shutdown.
  PaintContext paint_context_;

#if defined(REX_HAS_FIDELITYFX_RUNTIME) && REX_HAS_FIDELITYFX_RUNTIME
  void* temporal_upscaler_context_ = nullptr;
  uint32_t temporal_upscaler_max_render_width_ = 0;
  uint32_t temporal_upscaler_max_render_height_ = 0;
  uint32_t temporal_upscaler_max_output_width_ = 0;
  uint32_t temporal_upscaler_max_output_height_ = 0;
  uint32_t temporal_upscaler_flags_ = 0;
  bool temporal_upscaler_provider_logged_ = false;
  // Frame number of the last guest output fed to the upscaler (repaints of
  // the same frame reuse the previous output).
  uint64_t temporal_upscaler_last_frame_ = UINT64_MAX;
  ID3D12Resource* temporal_upscaler_last_output_ = nullptr;
#endif

  // dbz3: NVIDIA DLSS for present_effect=dlss (REX_HAS_DLSS builds). The NGX
  // handles are opaque so the class layout doesn't depend on the define.
  bool DispatchDlss(ID3D12GraphicsCommandList* command_list, ID3D12Resource* input_resource,
                    uint32_t input_width, uint32_t input_height, ID3D12Resource* output_resource,
                    uint32_t output_width, uint32_t output_height,
                    const Dbz3TemporalInputs& temporal, bool reset);
  void DestroyDlss();
  void* dlss_feature_ = nullptr;
  void* dlss_params_ = nullptr;
  // 0 = not tried yet, 1 = NGX ready, -1 = unavailable (falls back to FSR 3).
  int dlss_state_ = 0;
  uint32_t dlss_render_width_ = 0;
  uint32_t dlss_render_height_ = 0;
  uint32_t dlss_output_width_ = 0;
  uint32_t dlss_output_height_ = 0;
  int dlss_flags_ = 0;

 public:
  Dbz3TemporalInputs* Dbz3EnsureTemporal(uint32_t mailbox_index, uint32_t width, uint32_t height);

 private:
  // Protected by the guest output mailbox like guest_output_resources_.
  std::array<Dbz3TemporalInputs, kGuestOutputMailboxSize> dbz3_temporal_;
  // Textures replaced on a size change, kept alive for a few guest frames in
  // case a paint still references them on the GPU.
  std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>> dbz3_temporal_retired_;
  uint64_t dbz3_temporal_refresh_count_ = 0;
};

}  // namespace rex::ui::d3d12
