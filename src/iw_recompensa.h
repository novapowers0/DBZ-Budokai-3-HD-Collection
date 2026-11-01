// Consumidor de la recompensa de IW, EU285F30 / s_FIX_MSN_RESULT EU287040.
// Toda la composicion y las pistas salen de DATA_USA5/4; no se reconstruyen
// marcos, posiciones, colores ni cifras a partir de una fotografia.
#pragma once
#include "anim_iw.h"
#include "iw_minijuegos.h"
#include "pasarela_iw.h"
#include "ps2_modelo.h"
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace dbz3::iwmini::recompensa {
namespace A = dbz3::anim::ama;
namespace P = dbz3::pasarela;

inline uint32_t U(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
  return v;
}
inline void Poner(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  if (o + 4 <= b.size()) std::memcpy(b.data() + o, &v, 4);
}
struct Recursos {
  A::Obra obra;
  std::array<A::Obra, 3> glifos;
  std::vector<ps2::Tex> atlas, rotulos;
  std::array<std::array<int, 4>, 19> etiquetas{};
};
inline Recursos& Cargar() {
  static Recursos r;
  static std::once_flag una;
  std::call_once(una, [&] {
    P::AfsPs2 usa;
    if (!usa.Abrir(P::BuscarIW(), "DATA_USA.AFS")) return;
    const auto paquete = usa.Entrada(5), ama = P::HijoAmb(paquete, 4);
    if (!r.obra.Cargar(ama)) return;
    // LeerSegmento solo marca FX con claves. Estos nodos tambien tienen FX
    // 08/10/20 que heredan su propia muestra aun sin ninguna clave de FX.
    for (auto& s : r.obra.Segs()) s.tiene_fx |= (s.fx.flags & 0x38) != 0;
    for (int i = 0; i < 3; ++i) {
      auto b = ama;
      const uint32_t raiz = U(b, U(b, 0x14));
      const int indice = i == 0 ? 54 : i == 1 ? 70 : 78;
      // SprAnim_DrawAtTime usa la tabla de SEGMENTOS (+1C), no la de objetos.
      Poner(b, raiz + 16, U(b, U(b, 0x1c) + 4 * indice));
      r.glifos[i].Cargar(b);
    }
    const auto tex = P::HijoAmb(paquete, 5), labels = P::HijoAmb(paquete, 7);
    for (int i = 0, n = P::CuantasAmt(tex); i < n; ++i) r.atlas.push_back(ps2::Crear(P::TexturaAmt(tex, i)));
    for (int i = 0, n = P::CuantasAmt(labels); i < n; ++i) r.rotulos.push_back(ps2::Crear(P::TexturaAmt(labels, i)));
    const auto elf = P::LeerElf(P::BuscarIW());
    const uint32_t tabla = elf.size() == 3786800 ? 0x473240 : 0x472a40;
    for (int i = 0; i < 19; ++i)
      for (int j = 0; j < 4; ++j) {
        const size_t o = tabla - 0x100000 + 0x80 + i * 8 + j * 2;
        r.etiquetas[i][j] = o + 2 <= elf.size() ? int(elf[o] | (elf[o+1] << 8)) : -1;
      }
    REXLOG_INFO("dbz3 iw recompensa: AMA5/4 {} objetos, atlas {}, rotulos {}; consumidor original",
                r.obra.NumObjetos(), r.atlas.size(), r.rotulos.size());
  });
  return r;
}
inline A::Canal Cuadro(uint16_t tex, const float uv[4]) {
  A::Canal c;
  c.n = c.dur = 1;
  c.tiempos.push_back({0, A::kEscalon});
  c.claves.resize(32);
  Poner(c.claves, 0, tex);
  Poner(c.claves, 4, 5);
  std::memcpy(c.claves.data() + 16, uv, 16);
  return c;
}
inline A::MuestraCuadro Glifo(const Recursos& r, int cual, int cifra) {
  const auto& o = r.glifos[cual];
  A::Muestras m;
  A::Entrada e;
  const int raiz = o.Objeto(0);
  if (raiz >= 0) A::DibujarEnTiempo(o, o.Nodo(raiz).anim, float(cifra), {}, m, e);
  return m.cuadro;
}
inline A::Segmento* Unico(A::Obra& o, int nodo) {
  if (nodo < 0 || o.Nodo(nodo).anim < 0) return nullptr;
  const A::Segmento s = o.Seg(o.Nodo(nodo).anim);
  const int n = int(o.Segs().size());
  o.Segs().push_back(s);
  o.Nodos()[size_t(nodo)].anim = n;
  return &o.Segs().back();
}
inline int Hermano(const A::Obra& o, int nodo, int pasos) {
  while (nodo >= 0 && pasos-- > 0) nodo = o.Nodo(nodo).hermano;
  return nodo;
}
inline void Ocultar(A::Obra& o, int nodo) {
  if (nodo >= 0) { o.Nodos()[size_t(nodo)].anim = -1; o.Nodos()[size_t(nodo)].hijo = -1; }
}
inline bool Dibujar(ImDrawList* dl, float x, float y, float w, float h, const Resultado& resultado,
                    float cuadro, const ps2::Tex* titulo, float animacion = -1) {
  Recursos& r = Cargar();
  if (!r.obra.Valida() || r.atlas.size() < 7) return false;
  A::Obra o = r.obra;
  const float tiempo_ama = animacion < 0 ? cuadro : animacion;
  // EU287040: objetos 0, 5-rango, 7, 8, 10, 12. El titulo es el
  // primer nodo del objeto0, sustituido por el AMT de la mision (+FC).
  const int nt = o.Nodo(o.Objeto(0)).hijo;
  if (titulo && *titulo)
    if (auto* s = Unico(o, nt)) {
      const float uv[4] = {0, 0, float(titulo->w), float(titulo->h)};
      s->b.c1 = Cuadro(0xfffe, uv);
      s->rect[0] = -float(titulo->w) / 2; s->rect[2] = -s->rect[0];
      s->rect[1] = -float(titulo->h) / 2; s->rect[3] = -s->rect[1];
    }
  // EU3496B0/324C50/30A200: una fila tipo2. NB006/NB014:
  // EU377A00/32F0F0 escriben tres filas tipo3, no una cuarta fila RANK.
  const bool estrellas = resultado.clave == 0x7D3 || resultado.clave == 0x7D7 || resultado.clave == 0x7D8 || resultado.clave == 0x7DE;
  // 31/10: PCSX2: NB007 y vuelo una fila TIME (tipo1); NB012 y MSN019 GREAT/GOOD/NORMAL/TOTAL (tipo4); ladrillos estrellas.
  const bool tiempo = resultado.clave == 0x7D5 || resultado.clave == 0x7D6 || resultado.clave == 0x7E0;
  const bool notas = resultado.clave == 0x7DA || resultado.clave == 0x7E1;
  const bool porcentajes = resultado.clave == 0x7D4 || resultado.clave == 0x7DC;
  const int indice_campo=resultado.clave==0x7D0?0:resultado.clave==0x7D1?1:resultado.clave==0x7D2?2:
                         resultado.clave==0x7D9?10:resultado.clave==0x7DD?14:resultado.clave==0x7DF?16:-1;
  const bool campo=indice_campo>=0;
  if (!estrellas && !porcentajes && !campo && !tiempo && !notas) return false;
  const int indice = campo?indice_campo:resultado.clave == 0x7D3 ? 3 : resultado.clave == 0x7D5 ? 6 : resultado.clave == 0x7D6 ? 7 :
                     resultado.clave == 0x7DA ? 11 : resultado.clave == 0x7E0 ? 17 : resultado.clave == 0x7E1 ? 18 :
                     resultado.clave == 0x7D4 ? 5 : resultado.clave == 0x7DC ? 13 :
                     resultado.clave == 0x7D7 ? 8 : resultado.clave == 0x7D8 ? 9 : 15;
  const int primero = o.Nodo(o.Objeto(7)).hijo;
  const int label = Hermano(o, primero, 8);
  for (int fila = 0; fila < 4; ++fila) {
    const int nodo = Hermano(o, label, fila);
    if (fila >= (notas ? 4 : porcentajes || campo ? 3 : 1)) {
      Ocultar(o, nodo);
      Ocultar(o, Hermano(o, primero, 4 + fila));
      continue;
    }
    const int tex = r.etiquetas[indice][fila];
    if (tex >= 0 && tex < int(r.rotulos.size()))
      if (auto* s = Unico(o, nodo)) {
        const auto& t = r.rotulos[tex];
        const float uv[4] = {0, 0, float(t.w), float(t.h)};
        s->b.c1 = Cuadro(uint16_t(1000 + tex), uv);
        s->rect[0] = s->rect[1] = 0;
        s->rect[2] = float(t.w); s->rect[3] = float(t.h);
      }
  }
  auto numeros = [&](int objeto, int valor, bool total) {
    const int raiz = o.Objeto(objeto);
    int nodo = o.Nodo(raiz).hijo;
    // Los nodos del numero son HERMANOS de la raiz en este AMA.
    if (nodo < 0) nodo = o.Nodo(raiz).hermano;
    const int original = valor;
    RelojRecompensaIW reloj; reloj.cuadro = cuadro;
    const int fijas = reloj.Cifras(valor, total);
    for (int k = 0; nodo >= 0 && k < 7; ++k, nodo = o.Nodo(nodo).hermano) {
      if (k == 6) { if (original < 1000) Ocultar(o, nodo); break; } // separador de miles
      if (valor < 1 && k != 0) { Ocultar(o, nodo); continue; }
      if (k < fijas)
        if (auto* s = Unico(o, nodo)) {
          const auto gl = Glifo(r, total ? 1 : 0, valor % 10);
          s->b.c1 = Cuadro(uint16_t(gl.crudo), gl.uv);
        }
      valor /= 10;
    }
  };
  numeros(10, std::max(resultado.zeni >= 0 ? resultado.zeni : resultado.puntos, 0), false);
  const int saldo = resultado.saldo >= 0 ? resultado.saldo :
      std::min(999900, P::LeerProgreso("iw_zeni", 0) + (resultado.zeni >= 0 ? resultado.zeni : resultado.puntos));
  numeros(12, std::max(saldo, 0), true);
  std::vector<A::Entrada> entradas;
  auto objeto = [&](int id) {
    auto a = A::CrearArbol(o, o.Objeto(uint32_t(id)), tiempo_ama);
    A::MarcarArbol(a, tiempo_ama);
    A::ActualizarNodo(a, a.raiz, {}, entradas);
  };
  objeto(0);
  if (resultado.rango >= 0) objeto(5 - std::clamp(resultado.rango, 0, 4));
  objeto(7); objeto(8); objeto(10); objeto(12);
  if (estrellas) {
    // EU286xxx: tipo2 usa objeto14; de sus siete estrellas solo rango+1
    // reciben tinte255. Se conservan sus posiciones/pistas originales.
    const size_t antes = entradas.size();
    objeto(14);
    for (size_t i = antes; i < entradas.size(); ++i) {
      auto& e = entradas[i];
      if (i - antes < size_t(std::max(resultado.clave == 0x7D3 ? resultado.objetos : resultado.rango + 1, 0)))   // ladrillos: uno por acierto
        e.tinte[0] = e.tinte[1] = e.tinte[2] = 1;
    }
  } else if(porcentajes) {
    const int valores[3] = {resultado.offence, resultado.defence, resultado.score};
    for (int fila = 0; fila < 3; ++fila) {
      A::Obra pct = o;
      const int raiz = pct.Objeto(17);
      int nodo = pct.Nodo(pct.Nodo(raiz).hijo).hermano; // conservar el signo %
      int valor = std::clamp(valores[fila], 0, 100);
      for (int k = 0; nodo >= 0 && k < 3; ++k, nodo = pct.Nodo(nodo).hermano) {
        if (k && !valor) { Ocultar(pct, nodo); continue; }
        if (auto* s = Unico(pct, nodo)) {
          const auto gl = Glifo(r, 2, valor % 10);
          s->b.c1 = Cuadro(uint16_t(gl.crudo), gl.uv);
        }
        valor /= 10;
      }
      auto arbol = A::CrearArbol(pct, raiz, tiempo_ama);
      A::MarcarArbol(arbol, tiempo_ama);
      A::Contexto ctx;
      // EU286xxx: desplazamiento obtenido de las flechas de cada fila.
      A::Muestras base, actual; A::Entrada eb, ea;
      const int f0 = Hermano(o, primero, 4), fi = Hermano(o, primero, 4 + fila);
      A::DibujarEnTiempo(o, o.Nodo(f0).anim, tiempo_ama, {}, base, eb);
      A::DibujarEnTiempo(o, o.Nodo(fi).anim, tiempo_ama, {}, actual, ea);
      ctx.pos[1] = ea.pos[1] - eb.pos[1];
      A::ActualizarNodo(arbol, arbol.raiz, ctx, entradas);
    }
  } else if(campo || tiempo || notas) {
    // EU323150 escribe {tipo1 tiempo centesimas, tipo3 porcentaje,
    // tipo4 score=zeni/10}. EU285F30 usa objetos13/17/16 y glifos del
    // segmento78; se conservan los dos separadores del reloj del AMA.
    const int total_notas=std::clamp(resultado.score,0,9999);
    const int valores[4]={notas ? std::max(resultado.great,0) : std::max(resultado.centesimas,0),
        notas ? std::max(resultado.good,0) : resultado.objetos_total>0?std::clamp(resultado.objetos*100/resultado.objetos_total,0,100):0,
        notas ? std::max(resultado.normal,0) : std::clamp(resultado.score,0,9999), total_notas};
    const int objetos[4]={notas?16:13,notas?16:17,16,16};
    for(int fila=0;fila<(notas?4:tiempo?1:3);++fila) {
      A::Obra obra=o;const int raiz=obra.Objeto(uint32_t(objetos[fila]));
      int nodo=obra.Nodo(raiz).hijo;
      const bool reloj=objetos[fila]==13;
      if(objetos[fila]==17)nodo=obra.Nodo(nodo).hermano; // % queda en su sitio
      int valor=valores[fila];
      const int n=reloj?8:objetos[fila]==17?3:4;
      for(int k=0;nodo>=0 && k<n;++k,nodo=obra.Nodo(nodo).hermano) {
        if(reloj && (k==2 || k==5))continue;
        if(!reloj && k && !valor) {Ocultar(obra,nodo);continue;}
        const int base=reloj && k==4?6:10;
        if(auto* s=Unico(obra,nodo)) {
          const auto gl=Glifo(r,2,valor%base);s->b.c1=Cuadro(uint16_t(gl.crudo),gl.uv);
        }
        valor/=base;
      }
      auto arbol=A::CrearArbol(obra,raiz,tiempo_ama);A::MarcarArbol(arbol,tiempo_ama);
      A::Muestras base,actual;A::Entrada eb,ea;
      const int f0=Hermano(o,primero,4),fi=Hermano(o,primero,4+fila);
      A::DibujarEnTiempo(o,o.Nodo(f0).anim,tiempo_ama,{},base,eb);
      A::DibujarEnTiempo(o,o.Nodo(fi).anim,tiempo_ama,{},actual,ea);
      A::Contexto ctx;ctx.pos[1]=ea.pos[1]-eb.pos[1];
      A::ActualizarNodo(arbol,arbol.raiz,ctx,entradas);
    }
  }
  std::stable_sort(entradas.begin(), entradas.end(), [](const auto& a, const auto& b) { return a.pos[2] > b.pos[2]; });
  for (const auto& e : entradas) {
    const ps2::Tex* t = e.textura == 0xfffe ? titulo : e.textura >= 1000 && e.textura < 1000 + r.rotulos.size() ?
        &r.rotulos[e.textura - 1000] : e.textura < r.atlas.size() ? &r.atlas[e.textura] : nullptr;
    if (!t || !*t || e.SinTextura()) continue;
    A::Quad q; A::DibujarEntrada(e, q);
    auto at = [&](int i) { return ImVec2(x + q.xy[i][0] * w / 600, y + q.xy[i][1] * h / 450); };
    auto uv = [&](int i) { return ImVec2(q.uv[i][0] / t->w, q.uv[i][1] / t->h); };
    auto c = [&](int i) { return std::clamp(int(e.tinte[i]*255 + e.rgba[i] + .5f), 0, 255); };
    dl->AddImageQuad(t->id(), at(0), at(1), at(2), at(3), uv(0), uv(1), uv(2), uv(3), IM_COL32(c(0),c(1),c(2),c(3)));
  }
  return true;
}
}  // namespace dbz3::iwmini::recompensa
