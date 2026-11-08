#pragma once

#include "iw_piccolo_estados.h"

// Adaptador acotado para comprobar la reaccion de MSN010 en el motor de B3.
// No sustituye el scheduler aproximado del port. Normal cerrado: solo escena
// de prueba + DBZ3_TEST_IWX_PICCOLO_REACCION=1 en iw_mini_disparo.cpp.
namespace dbz3::iwpiccolo {

constexpr std::uint32_t AliasReaccion(std::uint32_t codigo) {
  return codigo >= 0x406 && codigo <= 0x40B ? 0xFC8 + codigo - 0x406 : 0;
}

struct ClipMotor {
  std::uint32_t codigo = 0, duracion = 0, vueltas = 0;
  float cuadro = 0;
  std::uint64_t orden_reinicio = 0; // 0 no acredita aplicacion; solo candidato con acuse
};

struct PasoReaccion {
  bool activa = false, regreso = false, observado = false;
  float delta_azimut = 0, delta_inclinacion = 0;
  Error error = Error::Ninguno;
};

class ReaccionEnMotor {
 public:
  const Estado& Ver() const { return estado_; }
  void Reiniciar() { *this = ReaccionEnMotor{}; }
  void Esquiva(std::uint32_t codigo) {
    if (estado_.pose_estado == 3) return;
    estado_.pose_anterior = estado_.pose_estado;
    estado_.pose_estado = 2;
    estado_.codigo = codigo;
  }
  Resultado Golpe(int rol, float salud, float maxima, bool exigir_acuse = false) {
    EntradaGolpe in;
    in.hueso_iw = rol;
    in.salud_antes = salud;
    in.salud_maxima = maxima;
    const Resultado r = AplicarGolpe(estado_, in);
    if (r.error == Error::Ninguno && r.orden.reproducir) {
      estado_ = r.estado;
      pendiente_ = true;
      visto_ = false;
      exigir_acuse_ = exigir_acuse;
      orden_reinicio_ = 0;
    }
    return r;
  }
  bool VincularReinicio(std::uint64_t generacion, std::uint64_t orden) {
    if (!exigir_acuse_ || estado_.pose_estado != 3 || estado_.generacion != generacion ||
        !orden || orden_reinicio_) return false;
    orden_reinicio_ = orden;
    return true;
  }
  std::uint64_t OrdenReinicio() const { return orden_reinicio_; }
  PasoReaccion Paso(const ClipMotor& clip, float salud, float maxima, float inclinacion) {
    PasoReaccion p;
    if (estado_.pose_estado != 3) return p;
    p.activa = true;
    // ReiniciarPose es una orden encolada: el cuerpo todavia puede mostrar
    // el clip anterior (incluso el terminal de otra reaccion del mismo codigo).
    if (exigir_acuse_) {
      if (!orden_reinicio_ || clip.orden_reinicio != orden_reinicio_) return p;
      pendiente_ = false;
    } else if (pendiente_) { pendiente_ = false; return p; }
    if (clip.codigo != AliasReaccion(estado_.codigo) || clip.duracion == 0 ||
        !std::isfinite(clip.cuadro) || clip.cuadro < 0) return p;
    if (!visto_) {
      if (!exigir_acuse_ && (clip.vueltas || clip.cuadro + 1.0f >= float(clip.duracion))) return p;
      visto_ = true;
    }
    p.observado = true;
    const ObservacionClip observacion{estado_.generacion,
        clip.vueltas != 0 || clip.cuadro + 1.0f >= float(clip.duracion), clip.cuadro};
    const Resultado fin = DespacharFinReaccion(estado_, observacion);
    if (fin.error != Error::Ninguno) { p.error = fin.error; return p; }
    estado_ = fin.estado;
    if (estado_.pose_estado != 3) {
      p.activa = false;
      p.regreso = true;
      return p;
    }
    Entrada in;
    in.salud = salud;
    in.salud_maxima = maxima;
    in.inclinacion = inclinacion;
    in.clip = observacion;
    const Resultado giro = ActualizarRotacion(estado_, in);
    if (giro.error != Error::Ninguno) { p.error = giro.error; return p; }
    estado_ = giro.estado;
    p.delta_azimut = giro.delta_azimut;
    p.delta_inclinacion = giro.delta_inclinacion;
    return p;
  }
  float Radio() {
    const Resultado r = ActualizarRadio(estado_);
    if (r.error != Error::Ninguno) return 60.0f;
    estado_ = r.estado;
    return r.radio_usado;
  }

 private:
  Estado estado_;
  bool pendiente_ = false, visto_ = false;
  bool exigir_acuse_ = false;
  std::uint64_t orden_reinicio_ = 0;
};

} // namespace dbz3::iwpiccolo
