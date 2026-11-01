// Incluir UNA VEZ dentro de namespace dbz3::pasarela::nave, despues del cierre
// del segundo namespace anonimo (el que termina antes de DibujarFiguraB3En).
// Antes, incluir iw_nave_hud_b2.h y combate.h fuera de namespaces.
// Reutiliza Tex/Anim2/LeerAma2/PoseAma2/LienzoDe/ElfB2/AfsPs2 de esta TU.
// b2.c: 1BA8C0 carga; 1B9F10/1B9E20/1B9CE0/1B9B90 dibujan por modo;
// 1BE260/1BD130/1BD4A0 cifras; 1C44A0/1C4040 barras; 1C4800 caras.
namespace hud_b2_detalle {
struct Banco {
  std::vector<Tex> tx;
  std::vector<Imagen> cpu;
  std::map<std::pair<int, uint32_t>, Tex> fog;
  std::vector<uint64_t> tex1_original;
};
struct Paquete { std::vector<Anim2> a; Banco b; std::vector<noche::MetaClip> meta; };
struct Orden {
  Anim2 a;
  Pose2 q;
  Pz2 p;
  Banco* banco = nullptr;
};
struct Recursos {
  std::filesystem::path fuente;
  std::string idioma;
  AfsPs2 afs;
  Paquete reloj, cuerpo, planta[4];
  Paquete dos_jugadores[4];
  InformacionDosJugadoresB2 info2p[4];
  bool intento2p[4] = {};
  std::map<int, Paquete> caras;
  std::vector<Orden> cola;
  bool valido = false, intentado = false, propietario = false;
  int contador_anterior = -1, planta_anterior = -1;
  float cambio_contador = 0;
  bool medio_texel = false;
  noche::ContextoVisual noche;
};
Recursos& R() { static Recursos r; return r; }

bool DentroArnesNave() {
  const char* e = std::getenv("DBZ3_TEST_MISIONES");
  return e && e[0] == '5' && (e[1] == '\0' || e[1] == ',');
}
bool OpcionPrueba(const char* nombre) {
  const char* valor = std::getenv(nombre);
  return DentroArnesNave() && valor && std::strcmp(valor, "1") == 0;
}
noche::OpcionesPrueba Opciones(const MuestraMarcadorB2& m) {
  noche::OpcionesPrueba normal;
  normal.caras = noche::kCarasVerificadasEnPort;
  // Habilitar solo la forma realmente observada; las demas extensiones
  // conservan su puerta, incluso si su tabla de correspondencia existe.
  bool kaioken=false, visibles_verificados=true;
  for(int i=0;i<(m.planta==0?2:1);++i) {
    if(m.forma_b3[i]==0)continue;
    const bool visto=m.personaje_b3[i]==0 && m.forma_b3[i]==1;
    kaioken=kaioken||visto;visibles_verificados=visibles_verificados&&visto;
  }
  normal.caras=normal.caras || (noche::kKaiokenVerificadoEnPort && kaioken && visibles_verificados);
  normal.transitorios = noche::kTransitoriosVerificadosEnPort;
  if (!DentroArnesNave()) return normal;
  return {normal.caras || m.prueba.caras || OpcionPrueba("DBZ3_TEST_GP2_HUD_CARAS"),
          m.prueba.medio_texel || OpcionPrueba("DBZ3_TEST_GP2_HUD_MEDIO_TEXEL"),
          normal.transitorios || m.prueba.transitorios || OpcionPrueba("DBZ3_TEST_GP2_HUD_TRANSITORIOS"),
          m.prueba.cargar2p || OpcionPrueba("DBZ3_TEST_GP2_HUD_RECURSOS_2P")};
}
bool PruebaActiva() {
  const char* flag = std::getenv("DBZ3_TEST_GP2_HUD");
  const char* escenario = std::getenv("DBZ3_TEST_MISIONES");
  // r2_nave ya pone MISIONES=5,reset; los otros dos permisos son explicitos.
  return std::getenv("DBZ3_TEST_NAVE") != nullptr && flag && std::strcmp(flag, "1") == 0 &&
         escenario && escenario[0] == '5' && (escenario[1] == '\0' || escenario[1] == ',');
}

// Tabla VA del ELF REAL del jugador. Usa los PT_LOAD, no el atajo VA-0x100000.
bool DatoElf(uint32_t va, uint32_t& valor) {
  const auto& d = ElfB2();
  auto u = [&](size_t o) { return o + 4 <= d.size() ? U32(d, o) : 0u; };
  if (d.size() < 52 || std::memcmp(d.data(), "\x7f" "ELF", 4) || d[4] != 1 || d[5] != 1) return false;
  const uint32_t ph = u(28), sz = uint32_t(d[42]) | uint32_t(d[43]) << 8;
  const uint32_t n = uint32_t(d[44]) | uint32_t(d[45]) << 8;
  if (sz < 32 || n > 128) return false;
  for (uint32_t i = 0; i < n; ++i) {
    const uint64_t p = uint64_t(ph) + uint64_t(i) * sz;
    if (p + 32 > d.size()) return false;
    if (u(size_t(p)) != 1) continue;
    const uint32_t begin = u(size_t(p) + 8), bytes = u(size_t(p) + 16);
    if (va < begin || uint64_t(va) + 4 > uint64_t(begin) + bytes) continue;
    const uint64_t o = uint64_t(u(size_t(p) + 4)) + va - begin;
    if (o + 4 > d.size()) return false;
    valor = u(size_t(o)); return true;
  }
  return false;
}
int Nodo(int lado, unsigned campo) {
  uint32_t n = ~0u;
  if (!DatoElf((lado ? 0x373D64u : 0x373C90u) + campo, n) || n > 1023) return -1;
  return int(n);
}
uint64_t Tex1Original(const std::vector<uint8_t>& d, int indice) {
  if (d.size() < 0x18 || indice < 0) return 0;
  const uint64_t tabla = U32(d, 0x14), ranura = tabla + uint64_t(indice)*4;
  if (ranura + 4 > d.size()) return 0;
  const uint64_t p = U32(d, size_t(ranura));
  if (p + 0x30 > d.size()) return 0;
  auto s16 = [&](size_t a) { return int16_t(uint16_t(d[a]) | uint16_t(d[a+1]) << 8); };
  const uint32_t lo = (uint32_t(int32_t(s16(size_t(p)+0x2E))) << 19) | 0x200u | (U32(d,size_t(p)+4) & 0x1FDu);
  const uint32_t hi = uint32_t(int32_t(s16(size_t(p)+0x2C)));
  return uint64_t(lo) | uint64_t(hi) << 32;
}
bool Texturas(Banco& b, const std::vector<uint8_t>& amt) {
  auto* dr = dbz3::launcher::PreviewDrawer();
  const int n = CuantasAmt(amt);
  if (!dr || n <= 0 || n > 256) return false;
  for (int i = 0; i < n; ++i) {
    Imagen im = TexturaAmt(amt, i);
    Tex t;
    if (im) {
      t.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear,
                              false, im.rgba.data());
      t.w = float(im.w); t.h = float(im.h);
    }
    b.tx.push_back(std::move(t)); b.cpu.push_back(std::move(im));
    b.tex1_original.push_back(Tex1Original(amt, i));
  }
  return true;
}
bool Cargar(Paquete& p, const std::vector<uint8_t>& amb, unsigned ama, unsigned amt) {
  const auto datos = HijoAmb(amb, ama);
  p.a = LeerAma2(datos);
  p.meta.clear();
  if (datos.size() < 16) return false;
  const uint64_t tabla = U32(datos, 12);
  for (size_t i = 0; i < p.a.size(); ++i) {
    const uint64_t o = tabla + i*0x40;
    if (o + 0x40 > datos.size()) return false;
    p.meta.push_back({U32(datos,size_t(o)+0x20),U32(datos,size_t(o)+0x24),U32(datos,size_t(o)+0x28)});
  }
  return !p.a.empty() && Texturas(p.b, HijoAmb(amb, amt));
}
bool Preparar() {
  auto& r = R();
  if (!dbz3::launcher::PreviewDrawer()) return false;
  const auto fuente = BuscarJuego("budokai 2");
  const std::string lang = T::LangCode();
  if (r.intentado && r.fuente == fuente && r.idioma == lang) return r.valido;
  r.fuente = fuente; r.idioma = lang; r.intentado = true; r.valido = false;
  r.reloj = {}; r.cuerpo = {}; r.caras.clear();
  for (auto& p : r.planta) p = {};
  for (int i = 0; i < 4; ++i) { r.dos_jugadores[i] = {}; r.info2p[i] = {}; r.intento2p[i] = false; }
  // Estas VA estan contrastadas solo con SLES_518.39. Fallar cerrado en otro ELF.
  uint32_t test = 0;
  if (fuente.empty() || !DatoElf(0x373ED0 + 6 * 4, test) || test != 0x1C56E0) return false;
  std::string afs = lang == "es" ? "DATA_SP.AFS" : lang == "fr" ? "DATA_FR.AFS" :
                    lang == "it" ? "DATA_IT.AFS" : lang == "de" ? "DATA_DU.AFS" : "DATA_UK.AFS";
  if (!r.afs.Abrir(fuente, afs) && !r.afs.Abrir(fuente, "DATA_UK.AFS")) return false;
  // gp80C0 == 18: AMA 0 / AMT 1 reloj y numeros; AMA 3 / AMT 4 HUD comun.
  const auto comun = r.afs.Entrada(18);
  if (!Cargar(r.reloj, comun, 0, 1) || !Cargar(r.cuerpo, comun, 3, 4)) return false;
  for (int i = 0; i < 4; ++i) {
    uint32_t id = ~0u;
    if (!DatoElf(0x373C50 + (6 + i) * 4, id) || id >= r.afs.Cuantas() ||
        !Cargar(r.planta[i], r.afs.Entrada(id), 0, 1)) return false;
  }
  r.valido = r.reloj.a.size() > 3 && r.cuerpo.a.size() > 82;
  return r.valido;
}
Paquete* Cara(int b2) {
  auto& r = R();
  if (b2 < 0 || b2 > 42) return nullptr;
  auto it = r.caras.find(b2);
  if (it == r.caras.end()) {
    uint32_t id = ~0u;
    if (!DatoElf(0x373BA0 + uint32_t(b2) * 4, id) || id >= r.afs.Cuantas()) return nullptr;
    it = r.caras.emplace(b2, Paquete{}).first;
    if (!Cargar(it->second, r.afs.Entrada(id), 0, 1)) return nullptr;
  }
  return it->second.a.empty() ? nullptr : &it->second;
}
const Pz2* Pieza(const Anim2& a, float c) {
  if (a.p.empty()) return nullptr;
  const Pz2* p = &a.p.front();
  for (const auto& x : a.p) { if (x.t > c) break; p = &x; }
  return p;
}
bool Poner(const std::vector<Anim2>& a, Banco& b, int nodo, float c = 0,
           int textura = -1, float dx = 0, int cifra = -1, bool barra = false) {
  if (nodo < 0 || nodo >= int(a.size())) return false;
  const auto& src = a[size_t(nodo)];
  const auto* p = Pieza(src, c);
  if (!p) return false;
  Orden o{src, PoseAma2(src, c, false), *p, &b};
  if (textura >= 0) o.p.tex = textura;
  if (o.p.tex < 0 || o.p.tex >= int(b.tx.size()) || !b.tx[size_t(o.p.tex)].t) return false;
  o.q.x += dx;
  if (cifra >= 0) {   // FUN_001BE260: atlas de 5 columnas x 2 filas, NO fuente host.
    const float w = o.p.u1, h = o.p.v1;
    o.p.u0 = w * float(cifra % 5); o.p.u1 = o.p.u0 + w;
    const float v = cifra < 5 ? 0.0f : h;
    o.p.v1 = o.p.v0 + v + h; o.p.v0 += v;
  }
  if (barra) {   // FUN_001C44A0: UV de 2 px se extienden a la longitud muestreada.
    const float w = std::abs((src.x1 - src.x0) * o.q.sx);
    if (src.x0 < 0) { o.p.u0 = std::max(0.0f, w - 1); o.p.u1 = 1; }
    else { o.p.u0 = 0; o.p.u1 = w; }
  }
  R().cola.push_back(std::move(o)); return true;
}
bool Numero(Paquete& ama, Banco& atlas, int ancla, int cero, int numero) {
  if (ancla < 0 || cero < 0 || ancla >= int(ama.a.size()) || cero >= int(ama.a.size())) return false;
  const auto& a = ama.a[size_t(ancla)];
  const auto& z = ama.a[size_t(cero)];
  const Pose2 qa = PoseAma2(a, 0, false), qz = PoseAma2(z, 0, false);
  if (!Poner(ama.a, atlas, ancla)) return false;
  // 1BD4A0 modo 1: incluye el signo x y coloca las cifras consecutivas desde el.
  float x = qa.x + std::abs(a.x1 - a.x0);
  const auto s = std::to_string(std::clamp(numero, 0, 999999));
  for (char c : s) {
    if (!Poner(ama.a, atlas, cero, 0, -1, x - qz.x, c - '0')) return false;
    x += std::abs(z.x1 - z.x0);
  }
  return true;
}
bool ColorEstado(Orden& o, int clip, float tick);
bool Retrato(int lado, Paquete& cara, int frame, const noche::EstadoVida* estado = nullptr) {
  auto& r = R();
  const auto* p = Pieza(cara.a[0], float(frame));
  if (!p) return false;
  const size_t inicio = r.cola.size();
  if (!Poner(r.cuerpo.a, r.cuerpo.b, Nodo(lado, 0x4C)) ||
      !Poner(r.cuerpo.a, cara.b, Nodo(lado, 0x50), 0, p->tex) ||
      !Poner(r.cuerpo.a, r.cuerpo.b, Nodo(lado, 0x54))) return false;
  if (estado) {
    if ((estado->flags & 0x20) && !ColorEstado(r.cola[inicio],46,estado->tiempo_critico)) return false;
    if ((estado->flags & 0x200) && !ColorEstado(r.cola[inicio+1],47,estado->tiempo_ko)) return false;
  }
  return true;
}
bool Barras(int lado, const MuestraMarcadorB2& m, const noche::EstadoVida* estado = nullptr) {
  auto& p = R().cuerpo;
  const float vida = estado ? float(estado->frente) : std::clamp(m.vida[lado] * 0.1f, 0.0f, 700.0f);
  const float ki = std::clamp(m.ki[lado] * 0.1f, 0.0f, 700.0f);
  const size_t marco = R().cola.size()+2;
  if (!Poner(p.a, p.b, Nodo(lado, 8), vida) ||
      !Poner(p.a, p.b, Nodo(lado, 16), vida, -1, 0, -1, true) ||
      !Poner(p.a, p.b, Nodo(lado, 0x5C))) return false;
  if (estado) {
    const float z_frente = R().cola[marco-1].q.z;
    if ((estado->flags & 0xC0) != 0) {
      if (!Poner(p.a,p.b,Nodo(lado,12),float(estado->estela),-1,0,-1,true)) return false;
      const int barra = estado->estela / 100;
      if (estado->frente / 100 != barra && estado->estela % 100 != 0) {
        if (!Poner(p.a,p.b,Nodo(lado,12),float(barra*100),-1,0,-1,true)) return false;
        R().cola.back().q.z = z_frente+1;
      }
    }
    if ((estado->flags & 0x20) && !ColorEstado(R().cola[marco],46,estado->tiempo_critico)) return false;
  }
  const int fill = Nodo(lado, 0x64);
  if (!Poner(p.a, p.b, fill)) return false;
  auto& o = R().cola.back();
  const float frac = int(ki) < int(m.ki_max[lado] * 0.1f) ? float(int(ki) % 100) / 100.0f : 1.0f;
  o.a.x0 *= frac; o.a.x1 *= frac;
  // Los marcos y las cifras de ki tambien son originales (FUN_001C4040).
  return int(ki) < int(m.ki_max[lado] * 0.1f) ?
      Poner(p.a, R().reloj.b, Nodo(lado, 0x68), 0, -1, 0, int(ki) / 100) :
      Poner(p.a, R().reloj.b, Nodo(lado, 0x6C));
}
ImTextureID ConFog(Orden& o) {
  auto& b = *o.banco;
  if ((o.q.niebla >> 24) == 0) return reinterpret_cast<ImTextureID>(b.tx[size_t(o.p.tex)].t.get());
  const auto key = std::make_pair(o.p.tex, o.q.niebla);
  auto it = b.fog.find(key);
  if (it == b.fog.end()) {
    Imagen im = b.cpu[size_t(o.p.tex)];
    const float fog = float(o.q.niebla >> 24) / 255.0f;
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) for (int ch = 0; ch < 3; ++ch)
      im.rgba[i + ch] = uint8_t(std::clamp(float(im.rgba[i + ch]) * (1 - fog) +
                                    float((o.q.niebla >> (ch * 8)) & 255) * fog, 0.0f, 255.0f));
    Tex t;
    if (auto* dr = dbz3::launcher::PreviewDrawer())
      t.t = dr->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
    if (!t.t) return {};
    it = b.fog.emplace(key, std::move(t)).first;
  }
  return reinterpret_cast<ImTextureID>(it->second.t.get());
}
bool Descargar(ImDrawList* dl) {
  auto& cola = R().cola;
  std::stable_sort(cola.begin(), cola.end(), [](const Orden& a, const Orden& b) { return a.q.z > b.q.z; });
  const auto L = LienzoDe(S::GameArea(ImGui::GetIO()));
  // Resolver TODAS las texturas antes de emitir: no esconder B3 tras un fallo a medias.
  std::vector<ImTextureID> ids;
  for (auto& o : cola) { auto id = ConFog(o); if (!id) return false; ids.push_back(id); }
  for (size_t i = 0; i < cola.size(); ++i) {
    const auto& o = cola[i]; const auto& q = o.q; const auto& a = o.a; const auto& p = o.p;
    if (q.a <= 0 || q.sx == 0 || q.sy == 0) continue;
    const auto& tx = o.banco->tx[size_t(p.tex)];
    const float ang = q.rot * 0.0174532925199433f, cs = std::cos(ang), sn = std::sin(ang);
    auto xy = [&](float x, float y) { return L.P(q.x + x*cs - y*sn, q.y + x*sn + y*cs); };
    float x0 = a.x0*q.sx, y0 = a.y0*q.sy, x1 = (a.x1+1)*q.sx, y1 = (a.y1+1)*q.sy;
    if (R().medio_texel) {
      // 1A7020: medio texel DESPUES de escala, antes de giro. A/B independiente, sin +2px global.
      x0 -= 0.5f; y0 -= 0.5f; x1 = a.x1*q.sx+0.5f; y1 = a.y1*q.sy+0.5f;
    }
    auto c8 = [](float f) { return int(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
    const ImU32 col = IM_COL32(c8(q.m[0]), c8(q.m[1]), c8(q.m[2]), c8(q.a));
    dl->AddImageQuad(ids[i], xy(x0,y0),xy(x1,y0),xy(x1,y1),xy(x0,y1),
                     {p.u0/tx.w,p.v0/tx.h},{p.u1/tx.w,p.v0/tx.h},
                     {p.u1/tx.w,p.v1/tx.h},{p.u0/tx.w,p.v1/tx.h}, col);
  }
  return true;
}
float Fl(uint8_t* base, uint32_t a) {
  const uint32_t bits = REX_LOAD_U32(a); float f; std::memcpy(&f, &bits, 4); return f;
}
int B2DeB3(int id) { for (int i : kAnilloB2) if (i != 63 && B3DeB2(i) == id) return i; return -1; }
bool Puerta(uint8_t* base, uint32_t mgr) {
  const dbz3::GA flags = 0x823CD870;
  return REX_LOAD_U32(dbz3::GA(0x82324064)) != 0 && REX_LOAD_U8(mgr + 35253) != 1 &&
         ((REX_LOAD_U32(flags) & REX_LOAD_U32(flags + 4)) & 0x80000002u) == 0x80000002u &&
         REX_LOAD_U32(flags - 0x11A4) != 0;
}
}  // namespace hud_b2_detalle

#include "iw_nave_hud_b2_noche.inl"
#include "iw_nave_hud_b2_2p.inl"

bool PintarMarcadorB2(ImDrawList* dl, const MuestraMarcadorB2& entrada) {
  using namespace hud_b2_detalle;
  auto& r = R();
  MuestraMarcadorB2 m = entrada;  // el fixture/propietario conserva su instantanea; no hay lectura guest aqui
  if (!dl || m.planta < 0 || m.planta > 3 || !std::isfinite(m.cuadros) || m.cuadros < 0 ||
      m.cuadros > 100000000 || m.fase_resultado || !Preparar()) return false;
  for (int i = 0; i < 2; ++i)
    if (!std::isfinite(m.vida[i]) || !std::isfinite(m.ki[i]) || !std::isfinite(m.ki_max[i]) ||
        std::abs(m.vida[i]) > 100000000 || std::abs(m.ki[i]) > 100000000 || std::abs(m.ki_max[i]) > 100000000) return false;
  r.cola.clear();
  const auto opciones = Opciones(m);
  r.medio_texel = opciones.medio_texel;
  if (m.dos_jugadores) {
    if (m.muestra_2p && OpcionPrueba("DBZ3_TEST_GP2_HUD_FIXTURE")) return original2p::PintarFixture(dl,*m.muestra_2p);
    InformacionDosJugadoresB2 info;
    if (opciones.cargar2p) Cargar2P(m.planta,info,true);
    return false;  // fuentes de estado/visual2P aun sin cotejar. No duplicar1P ni ocultar B3.
  }
  auto& contexto = m.contexto ? *m.contexto : r.noche;
  if (!PrepararContexto(m,opciones.transitorios,contexto) || (opciones.caras && !ResolverCaras(m,contexto))) return false;
  const noche::EstadoVida* estado[2] = {opciones.transitorios ? &contexto.lado[0].vida : nullptr,
                                       opciones.transitorios ? &contexto.lado[1].vida : nullptr};
  auto& p = r.planta[m.planta];
  auto* jugador = Cara(m.personaje_b2[0]);
  auto* rival = m.planta == 0 ? Cara(m.personaje_b2[1]) : nullptr;
  if (!jugador || (m.planta == 0 && !rival) || !Retrato(0, *jugador, m.cara_b2[0],estado[0])) return false;
  if (m.planta == 0 && !Retrato(1, *rival, m.cara_b2[1],estado[1])) return false;
  if (m.planta <= 1 && !Barras(0, m,estado[0])) return false;
  if (m.planta == 0 && !Barras(1, m,estado[1])) return false;
  if (m.planta == 0) {
    const auto* cara = Pieza(rival->a[0], float(m.cara_b2[1]));
    if (!Poner(p.a,r.cuerpo.b,1) || !Poner(p.a,p.b,6) || !cara ||
        !Poner(p.a,rival->b,2,0,cara->tex) || !Numero(p,r.reloj.b,4,5,m.vencidos)) return false;
  } else if (m.planta == 1) {
    const int seg = std::clamp((int(m.cuadros) + 59) / 60, 0, 999);
    if (!Poner(p.a,p.b,1)) return false;
    for (int i = 0, div = 100; i < 3; ++i, div /= 10)
      if (!Poner(p.a,r.reloj.b,2+i,0,-1,0,(seg/div)%10)) return false;
  } else {
    const int cuenta = m.planta == 2 ? m.golpes : m.ondas;
    if (r.contador_anterior != cuenta || r.planta_anterior != m.planta || m.cuadros < r.cambio_contador) {
      r.cambio_contador = m.cuadros; r.contador_anterior = cuenta; r.planta_anterior = m.planta;
    }
    const float anim = cuenta > 0 ? std::max(0.0f,m.cuadros-r.cambio_contador) : 0.0f;
    if (!Poner(p.a,p.b,1) || !Poner(p.a,p.b,2,anim) ||
        !Numero(p,r.reloj.b,m.planta==2?5:3,m.planta==2?6:4,cuenta)) return false;
    const int restante = std::max(0, m.limite_cuadros-int(m.cuadros));
    const int seg = std::clamp((restante+59)/60,0,99);
    if (!Poner(r.reloj.a,r.reloj.b,2,0,-1,0,seg/10) ||
        !Poner(r.reloj.a,r.reloj.b,3,0,-1,0,seg%10)) return false;
  }
  return Descargar(dl);
}
void SoltarMarcadorB2() {
  auto& r = hud_b2_detalle::R();
  if (r.propietario) { dbz3::combate::OcultarHud(false); r.propietario = false; }
}
#include "iw_nave_hud_b2_fixture.inl"
void MarcadorCombateB2(ImGuiIO& io, bool activo, int golpes) {
  using namespace hud_b2_detalle;
  auto& r = R();
  const bool prueba = PruebaActiva();
  if (!activo) { r.noche.iniciado = false; SoltarMarcadorB2(); return; }
  if ((!kMarcadorB2Probado && !prueba) || dbz3::GameDimmed() || dbz3::OverlayOpen()) {
    SoltarMarcadorB2(); return;
  }
  if (OpcionPrueba("DBZ3_TEST_GP2_HUD_FIXTURE")) {
    if (EstadoCombate()==3 || EstadoCombate()==4) fixture_b2::Dibujar();
    else SoltarMarcadorB2();
    return;
  }
  if (g_2p.load()) {
    InformacionDosJugadoresB2 info;
    if (OpcionPrueba("DBZ3_TEST_GP2_HUD_RECURSOS_2P")) LeerRecursosDosJugadoresB2(g_prueba,info);
    SoltarMarcadorB2(); return;
  }
  auto* rt = rex::Runtime::instance(); uint8_t* base = rt ? rt->virtual_membase() : nullptr;
  const uint32_t mgr = base ? REX_LOAD_U32(dbz3::GA(0x823D6CF4)) : 0;
  if (!mgr || (EstadoCombate()!=3 && EstadoCombate()!=4) || !Puerta(base,mgr) ||
      (!r.propietario && !dbz3::combate::HudNativoVisible())) { SoltarMarcadorB2(); return; }
  MuestraMarcadorB2 m;
  m.planta = g_prueba; m.vencidos = g_vencidos; m.golpes = golpes;
  m.ondas = dbz3::combate::OndasDevueltas(0);
  m.cuadros = Fl(base,mgr+35144); m.limite_cuadros = int(REX_LOAD_U32(mgr+2028));
  m.personaje_b2[0] = B2DeB3(g_j3); m.personaje_b2[1] = B2DeB3(g_r3);
  m.encuentro = mgr;
  for (int i=0; i<2; ++i) {
    const uint32_t x = mgr+2096+8240*uint32_t(i);
    m.vida[i]=Fl(base,x+6080); m.ki[i]=Fl(base,x+6100); m.ki_max[i]=Fl(base,x+6104);
    m.personaje_b3[i]=int(REX_LOAD_U32(x+noche::CamposB3::id));
    m.forma_b3[i]=int(REX_LOAD_U32(x+noche::CamposB3::forma));
    // La forma B3 no es el indice de cara B2. Cerrar hasta aportar esa correspondencia.
    if (!std::isfinite(m.vida[i]) || !std::isfinite(m.ki[i]) ||
        !std::isfinite(m.ki_max[i])) { SoltarMarcadorB2(); return; }
  }
  const bool probar_caras = Opciones(m).caras;
  for(int i=0;i<(m.planta==0?2:1);++i)
    if(!probar_caras && m.forma_b3[i]!=0) {SoltarMarcadorB2();return;}
  // No API publica de eventos de daño HUD: dano[].valido sigue false; no inferir fin de combo por ms/accion.
  (void)io;
  if (PintarMarcadorB2(ImGui::GetForegroundDrawList(),m)) {
    if (!r.propietario) { dbz3::combate::OcultarHud(true); r.propietario=true; }
    if (prueba) {
      static std::array<int, 4> ultimo_log{{-60, -60, -60, -60}};
      const int frame = ImGui::GetFrameCount();
      if (frame - ultimo_log[size_t(m.planta)] >= 60) {
        ultimo_log[size_t(m.planta)] = frame;
        const int reloj = m.planta == 0 ? -1 : m.planta == 1 ? (int(m.cuadros) + 59) / 60 :
            std::clamp((std::max(0, m.limite_cuadros - int(m.cuadros)) + 59) / 60, 0, 99);
        const int contador = m.planta == 0 ? m.vencidos : m.planta == 1 ? reloj : m.planta == 2 ? m.golpes : m.ondas;
        REXLOG_INFO("dbz3 nave hud-b2: planta={} contador={} reloj={} cuadros={}", m.planta + 1, contador, reloj, m.cuadros);
      }
    }
  } else SoltarMarcadorB2();
}
