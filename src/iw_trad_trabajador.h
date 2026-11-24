// Un solo paso en vuelo: nunca descarta entradas ni acumula una cola de logica.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace dbz3::iwtrad {
template<class Resultado> class Trabajador {
 public:
  explicit Trabajador(std::function<Resultado(uint16_t)> paso) : paso_(std::move(paso)), hilo_([this] { Bucle(); }) {}
  ~Trabajador() {
    { std::lock_guard<std::mutex> lk(mu_); salir_ = true; }
    cv_.notify_one();
    hilo_.join();
  }
  // El consumidor recoge ANTES de enviar el paso siguiente. La copia no
  // contiene referencias a la RAM mutable del interprete.
  Resultado Recoger() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] { return !pendiente_; });
    if (error_) std::rethrow_exception(error_);
    return resultado_;
  }
  void Enviar(uint16_t botones) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] { return !pendiente_; });
    botones_ = botones; pendiente_ = true;
    lk.unlock(); cv_.notify_one();
  }
 private:
  void Bucle() {
    for (;;) {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return pendiente_ || salir_; });
      if (!pendiente_ && salir_) return;
      const uint16_t botones = botones_;
      lk.unlock();
      Resultado nuevo{};
      std::exception_ptr error;
      try { nuevo = paso_(botones); } catch (...) { error = std::current_exception(); }
      lk.lock();
      resultado_ = std::move(nuevo); error_ = error; pendiente_ = false;
      lk.unlock(); cv_.notify_all();
    }
  }
  std::function<Resultado(uint16_t)> paso_;
  std::mutex mu_;
  std::condition_variable cv_;
  Resultado resultado_{};
  std::exception_ptr error_;
  uint16_t botones_ = 0;
  bool pendiente_ = false, salir_ = false;
  std::thread hilo_;
};
}  // namespace dbz3::iwtrad
