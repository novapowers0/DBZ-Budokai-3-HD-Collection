// Replay del limite UPDATE fin9 de Maquina::Paso con LIVE47 del callback original.
// pose47.h/fixtures se suministran fuera del repo; no contiene recursos del juego.
// Se detiene ANTES de evaluar D2: no prueba IA, colision ni dibujo posterior.
#include "../src/iw_bubbles_gp2.h"
#include "pose47.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace bb = dbz3::iwbubbles;

std::vector<std::uint8_t> leer(const char* nombre) {
  std::ifstream f(nombre, std::ios::binary);
  if (!f) throw std::runtime_error(nombre);
  return {std::istreambuf_iterator<char>(f), {}};
}

struct CambioA0 {};
struct AzarNoConsumido {
  unsigned Memory(unsigned) { throw std::runtime_error("fin9 consumio RNG"); }
};
struct PoseOriginal {
  bb47::Decoder& decoder;
  std::array<std::int16_t, 47> mapa{};
  std::uint32_t packed;
  bb47::Live live{};
  unsigned remuestreos = 0;
  float ultimo_cuadro = -1;

  bb::V Evaluar(unsigned codigo, float cuadro) {
    if (codigo == 0xD2 && cuadro == 0) throw CambioA0{};
    if (codigo != 0xE2) throw std::runtime_error("codigo inesperado en fin9");
    ++remuestreos;
    ultimo_cuadro = cuadro;
    const auto target = decoder.sample(packed, cuadro, mapa);
    for (unsigned i = 0; i < 47; ++i) {
      live[i].q = target[i].q;
      live[i].p = target[i].p;
      live[i].s = target[i].s;
    }
    return {live[1].p.x, live[1].p.y, live[1].p.z};
  }
  void AplicarRaiz(bb::V raiz) { live[1].p = {raiz.x, raiz.y, raiz.z, 1}; }
};

float error(bb47::V4 a, bb47::V4 b) {
  return std::max({std::fabs(a.x-b.x), std::fabs(a.y-b.y), std::fabs(a.z-b.z)});
}

int main() {
  try {
    const auto amo = leer("amo.bin"), amm = leer("amm.bin"), seno = leer("lut.bin");
    const auto antes = leer("fin_before.bin"), despues = leer("fin_after.bin");
    const auto metadatos = leer("fin_meta.bin"), target = leer("target2.bin");
    const bb47::Bytes meta{metadatos}, ammb{amm};
    if (antes.size() != sizeof(bb47::Live) || despues.size() != sizeof(bb47::Live)
        || metadatos.size() != 512 || target.size() != 102+47*64
        || (meta.u(24)&65535) != 9 || meta.u(28) != 0xE2 || meta.u(36) != 1)
      throw std::runtime_error("fixture no es el limite fin9 original");
    std::vector<float> lut(seno.size()/4);
    std::memcpy(lut.data(), seno.data(), seno.size());
    bb47::Decoder decoder(amo, amm, lut);
    PoseOriginal pose{decoder, {}, meta.u(124)};
    std::memcpy(pose.mapa.data(), target.data()+8, 94);
    std::memcpy(pose.live.data(), antes.data(), antes.size());

    bb::Maquina m;
    m.estado = 9;
    m.codigo = meta.u(28);
    m.pose_codigo = m.codigo;
    // El callback se capturo DESPUES del reset del reloj; LIVE/TARGET conservan
    // la ultima fase35.999973. Reproducir solo ese ultimo avance PAL hasta UPDATE.
    m.cuadro = m.pose_cuadro = meta.at<float>(108);
    const auto desc = ammb.u(20)+16*(meta.u(124)&65535);
    m.largos[m.codigo] = float(ammb.u(desc+8));
    const auto actor = meta.v(48), euler = meta.v(96), raiz = pose.live[1].p;
    m.p = {actor.x, actor.y, actor.z};
    m.raiz.local50 = {raiz.x, raiz.y, raiz.z};
    // Direccion horizontal de la matriz Euler capturada, no yaw crudo del Work.
    const double cx=std::cos(euler.x), sx=std::sin(euler.x);
    const double cy=std::cos(euler.y), sy=std::sin(euler.y);
    const double cz=std::cos(euler.z), sz=std::sin(euler.z);
    m.frente = {float(cz*sy*cx+sz*sx), 0, float(cy*cx)};
    AzarNoConsumido rng;
    bb::wcf::Backend sin_usar;
    bb::Entrada entrada;
    bool limite_alcanzado = false;
    try { m.Paso(entrada, rng, pose, sin_usar); }
    catch (const CambioA0&) { limite_alcanzado = true; }

    float qps = 0;
    for (unsigned i=0; i<47; ++i) {
      bb47::Live128 esperado;
      std::memcpy(&esperado, despues.data()+i*128, 128);
      qps = std::max({qps, error(pose.live[i].p, esperado.p), error(pose.live[i].s, esperado.s)});
      for (unsigned k=0; k<4; ++k)
        qps = std::max(qps, std::fabs(pose.live[i].q[k]-esperado.q[k]));
    }
    const float err_raiz = error({m.ultima_raiz.x,m.ultima_raiz.y,m.ultima_raiz.z,1}, raiz);
    const auto final = meta.v(256+48);
    const float err_actor = error({m.p.x,m.p.y,m.p.z,1}, final);
    const bool ok = limite_alcanzado && m.estado == 0 && m.flags_consumidos == meta.u(44)
        && pose.remuestreos == 0 && err_raiz == 0 && qps == 0 && err_actor == 0;
    std::cout << std::setprecision(9) << "PASO_FIN9 " << (ok ? "PASS" : "FAIL")
      << " EE=" << meta.u(8) << " fase_original=" << meta.at<float>(108)
      << " remuestreos_E2=" << pose.remuestreos << " ultimo_cuadro=" << pose.ultimo_cuadro
      << " error_raiz=" << err_raiz << " error_qps47=" << qps << " error_actor=" << err_actor
      << " flags=" << m.flags_consumidos << " antes_D2=" << limite_alcanzado
      << " normal=false paridad_global=false\n";
    return ok ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
