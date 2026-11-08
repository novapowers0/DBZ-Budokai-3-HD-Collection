/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <stack>

#include <rex/audio/audio_driver.h>
#include <rex/thread.h>

#include <SDL3/SDL.h>

namespace rex::audio::sdl {

class SDLAudioDriver : public AudioDriver {
 public:
  SDLAudioDriver(memory::Memory* memory, rex::thread::Semaphore* semaphore);
  ~SDLAudioDriver() override;

  bool Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 protected:
  static void SDLCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                          int total_amount);

  // dbz3: abre el flujo SDL (dispositivo por defecto); false si no hay dispositivo.
  bool AbrirFlujo();
  void CerrarFlujo();
  // dbz3: vigilante (solo con dbz3_audio_recuperar): reabre el dispositivo si deja de pedir
  // muestras y, sin dispositivo, marca el ritmo del juego para que no se quede esperando.
  void VigiaMain();

  rex::thread::Semaphore* semaphore_ = nullptr;
  std::thread vigia_;
  std::atomic<bool> vigia_activo_{false};
  std::atomic<int64_t> ultimo_cb_ns_{0};
  bool recuperar_ = false;
  double periodo_ms_ = 10.0;  // buffer del dispositivo, para detectar huecos

  SDL_AudioStream* sdl_stream_ = nullptr;
  bool sdl_initialized_ = false;
  uint8_t sdl_device_channels_ = 0;

  static const uint32_t frame_frequency_ = 48000;
  static const uint32_t frame_channels_ = 6;
  static const uint32_t channel_samples_ = 256;
  static const uint32_t frame_samples_ = frame_channels_ * channel_samples_;
  static const uint32_t frame_size_ = sizeof(float) * frame_samples_;
  std::queue<float*> frames_queued_ = {};
  std::stack<float*> frames_unused_ = {};
  std::mutex frames_mutex_ = {};
};

}  // namespace rex::audio::sdl
