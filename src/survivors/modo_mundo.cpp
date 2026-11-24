// Dragon Budokai Survivors: el mundo de la partida (camara, zoom, suelo y decorado, enemigos, efectos de B3 y de la tanda 3, cifras). Parte de modo.cpp (P0).
#include "modo_interno.h"
#include "mundo_p1.h"
#include "fx_real.h"
#include "fx_catalogo_b3.h"
#include "kienzan_b3.h"
#include "../ingame/native_ui.h"
#include "../region.h"

#include <memory>

// Ajustes de P1 (apagados de serie; con todos apagados el dibujo y la partida son los de siempre).
REXCVAR_DEFINE_BOOL(dbz3_survivors_jugo, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: luchador mas grande con anillo, zoom de serie 1,8, impacto (parpadeo, retroceso, pausa de golpe) y cifras escaladas")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_autoapuntado, false, "DBZ3/Mods",
                    "Dragon Budokai Survivors: los ataques del jugador apuntan solos (al jefe si esta a la vista, si no al enemigo mas cercano); el movimiento es libre")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_fx_b3, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: efectos de tecnica como B3 (mezcla aditiva, color y escala del juego, tope de saturacion)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_formas, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: el luchador se transforma con sus formas reales de B3 (barra de ki, evolucion)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_ia, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: enemigos con conductas (tirador, embestida, Saibaman que explota, elite con escudo)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_jefes_dos_fases, true, "DBZ3/Mods",
                    "Survivors: dos fases, con las formas inicial y final de cada jefe");
REXCVAR_DEFINE_BOOL(dbz3_survivors_fx_jefe_nativo, false, "DBZ3/Mods",
                    "Survivors: cuerpo y punta de los rayos de jefe por el motor B3 US");
REXCVAR_DEFINE_BOOL(dbz3_survivors_jefes_v2, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: jefes con su kit de B3 y 2 fases, minijefes en los minutos 5 y 10, jefe a las 12:00 y eventos con aviso")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_kienzan_texturas, true, "DBZ3/Mods",
                    "Dragon Budokai Survivors: dibujo texturizado del Kienzan; apagado usa el disco simple")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_kienzan_malla, true, "DBZ3/Mods",
                    "Survivors: malla original del Kienzan normal (experimental; requiere kienzan_texturas)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::survivors::interno {

namespace {
// A01: el luchador del jugador se dibuja mas grande (los enemigos y la simulacion no cambian). 1,0 con dbz3_survivors_jugo apagado.
constexpr float kEscJugador = 1.6f;   // 13/11 (juicio de laminas): 1,45 aun se perdia
constexpr float kEscJefe = 1.6f;   // 13/11 (juicio de laminas 3): el jefe se veia diminuto; solo con jugo o jefes_v2
float g_escj = 1.0f;
uint64_t g_pasos_prev = ~0ULL;   // detecta partida nueva (los pasos vuelven a 0): un unico Modo vive en el juego
// A02: temblor de camara en curso (t_ real del Modo en que empezo, intensidad 0..1) y cuantos avisos se han anotado en el registro
float g_tem_t0 = -100, g_tem_amp = 0;
int g_log_pausa = 0, g_log_tem = 0;
constexpr float kTemblorDura = 0.35f;
// A04 (dbz3_survivors_fx_b3): factor 0..1 que recorta el brillo de los resplandores aditivos del cuadro cuando entre todos los rayos cubren mas
// de kTopeFx de la pantalla (Super Kamehameha a zoom alto no blanquea el cuadro), y el valor que se anoto en el registro.
float g_fxk = 1.0f, g_fxk_dicho = 1.0f;
constexpr float kTopeFx = 0.10f;
int g_fxk_log = 0;
// A05: formas reales de B3 de cada luchador (mod center hd/tactics_manifest.toml: modelo y forma de cada transformacion). Piccolo y los
// demas sin forma propia no cambian de modelo (solo aura y destello). forma 1.. = la transformacion de ese indice (recortada a las que haya).
struct FormaB3 { int base, fid[3], b3, forma[3]; };
// 13/11: Goku (264 -> 265/266/267) fuera: sus modelos de forma salen ROSA PLANO (texturas; laminas/P1_A05). Hasta arreglar sus .sk, Goku
// se transforma solo con aura y destello, como Piccolo. Vegeta (417/419) se ve bien.
// 19/11 (SUR-02): el rosa era el Kaioken, que es la forma 1 de Goku (265): la forma 2 es SSJ (266) y la 3 SSJ2 (267) en el manifiesto.
constexpr FormaB3 kFormasB3[] = {   // Goku: Super Saiyan y SSJ2 (sin Kaioken)
                                 {264, {266, 267, 267}, 0, {2, 3, 3}},     // Goku: SSJ, SSJ2
                                 {416, {417, 419, 419}, 7, {1, 4, 4}},     // Vegeta: SSJ, Super Vegeta
                                 {235, {236, 237, 237}, 3, {1, 2, 2}},     // Gohan
                                 {390, {391, 391, 391}, 8, {1, 1, 1}},     // Trunks
                                 {298, {299, 299, 299}, 6, {1, 1, 1}}};    // Goten
float g_forma_t0 = -100;   // t_ del Modo en que empezo la ultima transformacion (destello); g_forma_log = ya anotadas
int g_forma_log = 0;
int g_log_evento = 0;   // A08: lineas de evento anotadas
// Pruebas de P1 (tools/pruebas.py --env): DBZ3_TEST_SURVIVORS_DAR=id[,id...] da esas tecnicas al nivel 5 (o esa evolucion) al empezar;
// DBZ3_TEST_SURVIVORS_KI=S llena la barra de ki a los S segundos de partida (A05).
bool g_ki_forzado = false, g_forma2_forzada = false;
Modelo FormaDe(int fid, int forma, bool* hay) {
  for (const FormaB3& f : kFormasB3)
    if (f.base == fid && forma >= 1) {
      const int k = std::min(forma, 3) - 1;
      *hay = true;
      return {f.fid[k], f.b3, f.forma[k]};
    }
  *hay = false;
  return ModeloDe(fid);
}
// A07 (jefes_v2): el modelo real de B3 del jefe en cada fase (manifiesto de Tactics: Cell 146/147/148 = formas 0/1/2, Frieza 182-185 = formas 1-4,
// Cooler 166 base y 167 forma final). Super Buu no tiene otra forma en B3: la fase 2 es solo destello, aura y furia. Fuera de jefes_v2 (o un jefe sin
// tabla): el de siempre.
Modelo ModeloJefe(const DefJefe& dj, int fase, bool v2, bool dos_fases) {
  if (v2) {
    const int f = fase >= 2 ? 1 : 0;
    if (!dj.fases_fid.empty()) {   // SUR-8: modelo y forma de cada fase, de los toml (fid del manifiesto)
      const size_t k = size_t(IndiceFormaJefe(dj, fase, dos_fases));
      return Modelo{dj.fases_fid[k], ModeloDe(dj.fid).b3, k < dj.fases_forma.size() ? dj.fases_forma[k] : 0};
    }
    if (dj.id == "cell") return f ? Modelo{148, 33, 2} : Modelo{147, 33, 1};
    if (dj.id == "cell_perfecto") return Modelo{148, 33, 2};
    if (dj.id == "freezer") return f ? Modelo{185, 27, 4} : Modelo{184, 27, 3};
    if (dj.id == "cooler") return f ? Modelo{167, 38, 1} : Modelo{166, 38, 0};
  }
  return ModeloDe(dj.fid);
}
float g_fase_t0 = -100;   // t_ en que empezo el cambio de fase del jefe (el modelo cambia a mitad del destello)
int g_log_kit = 0;
float g_intro_t0 = -100;   // t_ en que aparecio el jefe (jefes_v2): la camara se acerca y vuelve
float g_jefe_t0 = -1;     // tiempo de partida en que aparecio el jefe
bool g_fase2_forzada = false;
std::vector<uint64_t> g_fx_reales; // solo IDs con geometria efectivamente dibujada
uint64_t IdHojaReal(int i,float t0) {
  return (1ull<<63) | (uint64_t(std::max(0.0, std::round(double(t0)*1000)))<<16) | uint64_t(i);
}

bool DibujarKienzan(ImDrawList* dl, ImVec2 centro, float r, float sp, float cp, float ang) {
  namespace N = dbz3::ingame::native_ui;
  struct Cache {
    bool intentado = false, listo = false, dibujado = false;
    kienzan::Malla malla;
    std::array<std::unique_ptr<rex::ui::ImmediateTexture>, 2> tex;
  };
  static Cache cache[2];  // igual que style::Img: recursos host residentes para las listas en vuelo
  const bool eu = dbz3::settings::Region() == "eu";
  const char* const region = eu ? "eu" : "us";
  auto& c = cache[eu];
  if (!c.intentado) {
    auto* drawer = dbz3::launcher::PreviewDrawer();
    if (!drawer) return false;
    c.intentado = true;
    const auto t0 = Reloj::now();
    const auto root = dbz3::HostDataRoot();
    const auto b = root.empty() ? std::vector<uint8_t>{} : N::LeerEntrada(root / region / "data_cmn.afs", kienzan::kBsp);
    const auto f = kienzan::Leer(b);
    std::array<std::unique_ptr<rex::ui::ImmediateTexture>, 2> tex;
    const char* fallo = b.empty() ? "sin_bsp" : "formato";
    if (f) {
      for (unsigned k = 0; k < tex.size(); ++k) {
        N::Image im;
        fallo = "dds";
        if (!N::DecodificarDds(f->dds[k], im)) break;
        fallo = "gpu";
        tex[k] = drawer->CreateTexture(uint32_t(im.w), uint32_t(im.h), rex::ui::ImmediateTextureFilter::kLinear, false, im.rgba.data());
        if (!tex[k]) break;
      }
      if (tex[0] && tex[1]) {
        c.malla = f->malla;
        c.tex = std::move(tex);
        c.listo = true;
      }
    }
    if (c.listo) REXLOG_INFO("dbz3 survivors: kienzan malla carga region={} bsp=539 ms={:.2f}", region, Ms(t0, Reloj::now()));
    else REXLOG_WARN("dbz3 survivors: kienzan malla respaldo region={} bsp=539 causa={}", region, fallo);
  }
  if (!c.listo) return false;
  const ImVec2 mn = dl->GetClipRectMin(), mx = dl->GetClipRectMax();
  const auto p = kienzan::Proyectar(c.malla, {centro.x, centro.y}, r, sp, cp, ang, {mn.x, mn.y}, {mx.x, mx.y});
  if (!p) return false;
  // DDS/UV y asignacion de material originales. El mezclador guest aun no se replica: alfa del pase UI.
  for (unsigned k = 0; k < kienzan::kTramos.size(); ++k) {
    const auto lote = kienzan::kTramos[k];
    dl->PushTextureID(reinterpret_cast<ImTextureID>(c.tex[k].get()));
    dl->PrimReserve(int(lote.indices), int(lote.vertices));
    const auto base = dl->_VtxCurrentIdx;  // tras PrimReserve (puede empezar otro VtxOffset)
    for (unsigned i = lote.indice; i < lote.indice + lote.indices; ++i)
      dl->PrimWriteIdx(ImDrawIdx(base + c.malla.i[i] - lote.vertice));
    for (unsigned i = lote.vertice; i < lote.vertice + lote.vertices; ++i) {
      const auto& uv = c.malla.v[i];
      dl->PrimWriteVtx({(*p)[i].x, (*p)[i].y}, {uv.u, uv.v}, IM_COL32_WHITE);
    }
    dl->PopTextureID();
  }
  if (!c.dibujado) {
    c.dibujado = true;
    REXLOG_INFO("dbz3 survivors: kienzan malla dibujada region={} bsp=539 triangulos=98 dds=0,1", region);
  }
  return true;
}
}  // namespace

void Modo::MundoAlEvento(const Evento& e) {   // gancho de Eventos (P1): pausa de golpe y temblor (dbz3_survivors_jugo)
  if (e.tipo == TipoEvento::Pausa) {
    if (g_log_pausa++ < 12) REXLOG_INFO("dbz3 survivors: jugo: pausa {} ms", e.a);
  } else if (e.tipo == TipoEvento::AvisoEvento) {   // A08
    if (g_log_evento++ < 40) REXLOG_INFO("dbz3 survivors: aviso evento={} en {:.0f} s", Juego::NombreEvento(e.a), e.v);
    static const char* const kEs[] = {"¡Se acerca un enjambre!", "¡Te rodean!", "¡Lluvia de meteoros!", "¡Llega un campeón!"};   // SUR-05: el aviso se ve en partida
    static const char* const kEn[] = {"A swarm is coming!", "You are surrounded!", "Meteor shower!", "A champion arrives!"};
    if (e.a >= 0 && e.a < 4 && j_->jefe.estado == EstadoJefe::Ausente) Aviso(T::T(kEs[e.a], kEn[e.a]), true);
  } else if (e.tipo == TipoEvento::EventoOleada) {
    if (g_log_evento++ < 40) REXLOG_INFO("dbz3 survivors: evento={} t={:.0f}", Juego::NombreEvento(e.a), j_->tiempo);
  } else if (e.tipo == TipoEvento::JefeFuria) {
    REXLOG_INFO("dbz3 survivors: jefe furia t={:.0f}", j_->tiempo);
  } else if (e.tipo == TipoEvento::JefeAparece) {   // A07: intro del jefe (jefes_v2): zoom de camara (Mundo) junto al rotulo de presentacion de modo.cpp
    if (j_->config.jefes_v2) g_intro_t0 = t_, min_dicho_ = 99;   // y no salen mas rotulos «Minuto N» encima de la presentacion y de los avisos del jefe
  } else if (e.tipo == TipoEvento::JefeFase) {   // A07
    g_fase_t0 = t_;
    REXLOG_INFO("dbz3 survivors: jefe fase={} t={:.0f} vida={:.0f}", e.a, j_->tiempo, j_->jefe.vida);
    char buf[128];
    std::snprintf(buf, sizeof buf, T::T("¡%s se transforma!", "%s transforms!"), Es(j_->DJ().txt));
    Aviso(buf, true);
  } else if (e.tipo == TipoEvento::JefeAtaque) {
    if (g_log_kit++ < 60) REXLOG_INFO("dbz3 survivors: jefe ataque={} fase={} aviso={:.1f} s t={:.0f} skc={}", NombreAtaque(AtaqueJefe(e.a)), j_->jefe.fase, e.v, j_->tiempo, CartaJefe(AtaqueJefe(e.a)));
    // SUR-04: avisos con rotulo para los ataques que sin texto no se leen (crias de Cell Jr y tiron de absorcion)
    // (sin «Cell»: la barra del jefe ya lo dice encima)
    if (AtaqueJefe(e.a) == AtaqueJefe::InvocaCellJr) Aviso(T::T("¡Llegan las crías!", "Here come the Juniors!"), true);
    else if (AtaqueJefe(e.a) == AtaqueJefe::Absorcion) Aviso(T::T("¡Absorción! ¡Aléjate!", "Absorption! Get away!"), true);
    else if (const AtaqueJefe at = AtaqueJefe(e.a); at == AtaqueJefe::Kamehameha || at == AtaqueJefe::RayoMortal || at == AtaqueJefe::OndaMortal ||
                                                    at == AtaqueJefe::RayoDestructor || at == AtaqueJefe::DiscoSauzer)   // SUR-08: leyenda de la franja roja (los jueces no la entendian)
      Aviso(T::T("¡Sal de la franja roja!", "Get out of the red lane!"), true);
  } else if (e.tipo == TipoEvento::Forma) {   // A05
    g_forma_t0 = t_;
    if (g_forma_log++ < 20) REXLOG_INFO("dbz3 survivors: formas: forma={} ({:.0f} s)", e.a, e.v);
  } else if (e.tipo == TipoEvento::Temblor) {
    if (SacudidaActiva() && g_opciones.sacudida && e.v > g_tem_amp * std::max(0.0f, 1.0f - (t_ - g_tem_t0) / kTemblorDura)) g_tem_t0 = t_, g_tem_amp = e.v;
    if (g_log_tem++ < 12) REXLOG_INFO("dbz3 survivors: jugo: temblor origen={} intensidad={:.2f}", e.a, e.v);
  }
}

void Modo::Cambiar(Anim& a, int clip, float dura) {
  if (clip != a.clip) a.prev = a.clip, a.t_prev = ta_ - a.t0, a.mez0 = ta_;
  a.clip = clip, a.t0 = ta_, a.dura = dura;
}

bool Modo::Pintar3D(const Anim& a, const ps2::Camara& cam, const Modelo& m, int fid, float x, float y, float alto, float giro) {
  const float mz = std::clamp((ta_ - a.mez0) / kMezcla, 0.0f, 1.0f);
  return pasarela::nave::DibujarFiguraB3En(&esc_, cam, m.b3, m.forma, {x, 0, -y}, alto, giro, a.clip, ta_ - a.t0, fid, a.dura, -1.0f, mz < 1.0f ? a.prev : -1,
                                           a.t_prev, mz);
}

// El manifiesto selecciona una tecnica del personaje, tambien durante el movimiento.
void Modo::Accion(int clip, float dura, float dx, float dy) {
  const int pedido = clip;
  if (log_clip_[pedido & 7]++ < 4)
    REXLOG_INFO("dbz3 survivors: anim jugador clip={} -> {}", pedido, clip);
  // El movimiento no debe impedir la animacion del ataque.
  Cambiar(pj_, clip, dura);
  pj_.fin = ta_ + dura, pj_.giro = std::atan2(dx, -dy);
}

void Modo::AnimJugador() {
  const Juego& j = *j_;
  if (fin_) return;
  if (pj_.fin > ta_) return;
  const int base = j.jugador.moviendo ? kClipAndar : kClipEspera;
  if (pj_.clip != base) Cambiar(pj_, base);
}

void Modo::AnimJefe() {
  const Jefe& b = j_->jefe;
  int clip = kClipAndar;
  switch (b.estado) {
    case EstadoJefe::AvisoCarga:
    case EstadoJefe::AvisoAnillo:
    case EstadoJefe::AvisoLluvia:
    case EstadoJefe::AvisoKit:
    case EstadoJefe::Kit:
    case EstadoJefe::Transforma: clip = kClipTecnica; break;
    case EstadoJefe::Carga: clip = kClipAtaque; break;
    case EstadoJefe::Muerto: clip = kClipDerrota; break;
    default: clip = jefe_.fin > ta_ ? kClipDano : kClipAndar; break;
  }
  if (clip != jefe_.clip) Cambiar(jefe_, clip, clip == kClipDano ? 0.3f : 0.0f);
}

uint32_t Modo::Bits(float sy) {   // y de pantalla -> clave que ordena como el float
  const float v = std::clamp(sy + 100000.0f, 0.0f, 1e9f);
  uint32_t u;
  std::memcpy(&u, &v, 4);
  return u;
}

bool Modo::TeclaWin(int vk) {
#if defined(_WIN32)
  DWORD pid = 0;
  const HWND h = GetForegroundWindow();
  if (!h || (GetWindowThreadProcessId(h, &pid), pid != GetCurrentProcessId())) return false;
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
}

void Modo::Zoom() {
  const ImGuiIO& io = ImGui::GetIO();
  float d = io.MouseWheel * 0.12f;
  const float ry = float(g_ry.load()) / 32767.0f;
  if (std::fabs(ry) > 0.3f) d += ry * 1.2f * io.DeltaTime;
  const bool mas = ImGui::IsKeyDown(ImGuiKey_Equal) || ImGui::IsKeyDown(ImGuiKey_KeypadAdd) || ImGui::IsKeyDown(ImGuiKey_PageUp) ||
                   TeclaWin(0xBB /* VK_OEM_PLUS */) || TeclaWin(0x6B /* VK_ADD */) || TeclaWin(0x21 /* VK_PRIOR */);
  const bool menos = ImGui::IsKeyDown(ImGuiKey_Minus) || ImGui::IsKeyDown(ImGuiKey_KeypadSubtract) || ImGui::IsKeyDown(ImGuiKey_PageDown) ||
                     TeclaWin(0xBD /* VK_OEM_MINUS */) || TeclaWin(0x6D /* VK_SUBTRACT */) || TeclaWin(0x22 /* VK_NEXT */);
  if (mas) d += 1.2f * io.DeltaTime;
  if (menos) d -= 1.2f * io.DeltaTime;
  if (d == 0.0f) return;
  {   // registro: la primera vez que llega el zoom por cada camino (las pruebas lo leen)
    const int via = io.MouseWheel != 0.0f ? 0 : (mas || menos) ? 1 : 2;
    if (!(zoom_via_ & (1u << via))) {
      zoom_via_ |= 1u << via;
      static const char* const kVia[3] = {"rueda", "teclado", "palanca derecha"};
      REXLOG_INFO("dbz3 survivors: zoom por {}", kVia[via]);
    }
  }
  zoom_ = std::clamp(zoom_ * std::exp(d), 1.0f, 2.2f), zoom_tocado_ = true;
  if (std::fabs(zoom_ - zoom_dicho_) >= 0.1f) {
    zoom_dicho_ = zoom_;
    REXLOG_INFO("dbz3 survivors: zoom={:.2f}", zoom_);
  }
}

void Modo::Mundo(ImDrawList* dl, float W, float H) {
  const Juego& j = *j_;
  const float pic = Impostores::kPicado;
  if (j.pasos < g_pasos_prev) {   // partida nueva: ajustes de P1 (por si Empezar no llamo a AjustesP1) y ganchos de prueba
    j_->AjustesTardios(Jugo(), Formas(), IaV2(), JefesV2());
    REXLOG_INFO("dbz3 survivors: ajustes P1 jugo={} fx_b3={} formas={} ia={} jefes_v2={} meta_v2={}", j.config.jugo, FxB3(), j.config.formas, j.config.ia, j.config.jefes_v2,
                j.config.meta_v2);
    g_ki_forzado = g_forma2_forzada = false;
    if (std::getenv("DBZ3_TEST_SURVIVORS_SOLO_JEFE")) j_->SoloJefe();   // A07 (pruebas): el jefe sin horda
    if (const char* at = std::getenv("DBZ3_TEST_SURVIVORS_ATAQUE"))   // SUR-04 (pruebas): el jefe repite este ataque ("absorcion", "taiyoken"...)
      for (int a = 1; a <= int(AtaqueJefe::Absorcion); ++a)
        if (std::strcmp(NombreAtaque(AtaqueJefe(a)), at) == 0) {
          j_->ataque_prueba = AtaqueJefe(a);
          REXLOG_INFO("dbz3 survivors: prueba: ataque del jefe forzado {}", at);
        }
    if (const char* dar = std::getenv("DBZ3_TEST_SURVIVORS_DAR")) {
      std::string lista = dar;
      for (size_t a = 0; a < lista.size();) {
        const size_t b = std::min(lista.find(',', a), lista.size());
        const std::string id = lista.substr(a, b - a);
        for (int t = 0; t < int(datos_.tecnicas.size()); ++t)
          if (datos_.tecnicas[size_t(t)].id == id) {
            const DefTecnica& dt = datos_.tecnicas[size_t(t)];
            if (dt.evoluciona_de >= 0) j_->Dar(TipoOpcion::Tecnica, dt.evoluciona_de, kNivelMax), j_->Dar(TipoOpcion::Evolucion, t, kNivelMax);
            else j_->Dar(TipoOpcion::Tecnica, t, kNivelMax);
            REXLOG_INFO("dbz3 survivors: prueba: tecnica dada {}", id);
          }
        a = b + 1;
      }
    }
  }
  // A07 (pruebas): DBZ3_TEST_SURVIVORS_JEFE_FASE2=S lleva al jefe al 49 % de vida S segundos despues de aparecer (ve la fase 2 sin pelear 2 minutos)
  if (j.pasos < g_pasos_prev) g_jefe_t0 = -1, g_fase2_forzada = false;
  if (j.jefe.estado != EstadoJefe::Ausente && g_jefe_t0 < 0) g_jefe_t0 = j.tiempo;
  if (j.estado == Estado::Jugando && std::getenv("DBZ3_TEST_SURVIVORS_INMORTAL")) j_->jugador.vida = j.jugador.vida_max;   // pruebas: el piloto sobrevive al jefe
  if (std::getenv("DBZ3_TEST_SURVIVORS_SOLO_JEFE")) {   // pruebas: sin subidas de nivel y el jefe nace a 5 pasos del jugador (a la vista)
    j_->jugador.exp = 0;
    if (j.jefe.estado == EstadoJefe::Persigue && j.jefe.t < 0.05f) j_->jefe.x = j.jugador.x + 3.5f, j_->jefe.y = j.jugador.y + 2.5f;
  }
  // SUR-04 (pruebas): con el ataque forzado, el jefe persigue siempre a la vista (a menos de 5 de mundo) y sus avisos salen en pantalla
  if (j.ataque_prueba != AtaqueJefe::Ninguno && j.jefe.estado == EstadoJefe::Persigue &&
      std::hypot(j.jefe.x - j.jugador.x, j.jefe.y - j.jugador.y) > 5.0f)
    j_->jefe.x = j.jugador.x + 3.0f, j_->jefe.y = j.jugador.y + 2.0f;
  if (const char* f2 = std::getenv("DBZ3_TEST_SURVIVORS_JEFE_FASE2")) {   // SUR-8: cada S segundos pasa a la fase siguiente (hasta la ultima)
    static float t_prueba = 0;
    if (g_jefe_t0 < 0) t_prueba = 0;
    if (g_jefe_t0 >= 0 && j.jefe.estado == EstadoJefe::Persigue && j.jefe.fase < j.NFases() && j.tiempo - std::max(g_jefe_t0, t_prueba) >= float(std::atof(f2))) {
      t_prueba = j.tiempo, g_fase2_forzada = true, j_->jefe.vida = j.jefe.vida_max * (j.UmbralFase_(j.jefe.fase) - 0.01f);
      REXLOG_INFO("dbz3 survivors: prueba: jefe al {:.0f} % de vida (fase {} de {})", 100 * (j.UmbralFase_(j.jefe.fase) - 0.01f), j.jefe.fase, j.NFases());
    }
  }
  if (const char* ki = std::getenv("DBZ3_TEST_SURVIVORS_KI"))
    if (!g_ki_forzado && j.tiempo >= float(std::atof(ki)) && j.estado == Estado::Jugando) g_ki_forzado = true, j_->DarKi(1.0f);
  if (const char* f2 = std::getenv("DBZ3_TEST_SURVIVORS_FORMA2"))   // SUR-02 (pruebas): a los S segundos pasa a la forma 2 (la evolucion mientras esta transformado)
    if (!g_forma2_forzada && j.tiempo >= float(std::atof(f2)) && j.estado == Estado::Jugando && j.forma > 0) g_forma2_forzada = true, j_->ForzarForma(2);
  g_escj = Jugo() ? kEscJugador : 1.0f;
  if (j.pasos < g_pasos_prev && Jugo() && !zoom_tocado_ && zoom_ == 1.5f) zoom_ = zoom_dicho_ = 1.8f;   // A01: zoom de serie 1,8 (si el usuario no lo ha tocado)
  Zoom();
  float sx = 0, sy = 0;   // A02: temblor de camara (solo jefe, bomba y muerte de elite; decae solo)
  if (Jugo()) {
    const float u = (t_ - g_tem_t0) / kTemblorDura;
    if (u >= 0 && u < 1) {
      const float a = g_tem_amp * (1 - u) * (1 - u) * H * 0.012f;
      sx = a * std::sin(t_ * 83.0f), sy = a * std::cos(t_ * 71.0f);
    }
  }
  float zi = 1.0f;   // A07: al aparecer el jefe la camara se acerca un 22 % y vuelve en 2 s
  if (j.config.jefes_v2) {
    const float u = (t_ - g_intro_t0) / 2.0f;
    if (u >= 0 && u < 1) zi = 1.0f + 0.12f * std::sin(u * 3.14159265f);
  }
  // 13/11 (juicio 4): con el jefe vivo y cerca, la camara baja un poco al luchador para que la cabeza del jefe no se corte por arriba ni quede tras el reloj
  // (la cabeza pide estar por debajo del 22 % de la pantalla; el luchador no pasa del 80 %). Suave; se reinicia en cada partida.
  static float cam_dy = 0, cam_t = 0;
  if (j.pasos < g_pasos_prev) cam_dy = 0;
  float objetivo = 0;
  if ((Jugo() || j.config.jefes_v2) && j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) {
    const float S0 = H / 22.0f * zoom_ * zi, pie = H * 0.55f - S0 * std::sin(pic) * (j.jefe.y - j.jugador.y);
    const float cabeza = pie - S0 * kAlto * j.DJ().escala * kEscJefe * std::cos(pic);
    if (pie > 0.1f * H) objetivo = std::clamp(0.34f * H - cabeza, 0.0f, 0.34f * H);   // 0.34: bajo la barra del jefe (SUR-04: con 0.31 la barra tapaba la cabeza de Cell en el aviso de la Bomba Espiritual)
  }
  cam_dy += (objetivo - cam_dy) * std::min(1.0f, std::max(0.0f, t_ - cam_t) * 4.0f), cam_t = t_;
  const Vista v{H / 22.0f * zoom_ * zi, W * 0.5f + sx, H * 0.55f + cam_dy + sy, std::sin(pic), std::cos(pic), j.jugador.x, j.jugador.y};
  // suelo repetible que se desplaza con la camara (la tesela no se aplasta con el picado: la llanura no lo nota)
  const DefEscenario& es = j.Escenario();
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Rgb(es.deco.color, 1.0f));
  DibujarSuelo(dl, es.id.c_str(), ImVec2(0, 0), ImVec2(W, H), ImVec2(v.S * v.jx - v.cx, -(v.cy + v.S * v.sp * v.jy)), v.S * 20.0f);
  vista_ = v;
  // decorado del escenario (tanda 3 / 4): zonas de suelo, manchas y adornos planos aqui; los demas adornos van a spr_[0..m) con las figuras
  auto td = Reloj::now();
  int m = Decorado(dl, v, W, H, 0);
  deco_suma_ += Ms(td, Reloj::now());

  // A04: cuanto de la pantalla cubren los rayos de este cuadro (anchura x largo en pixeles); por encima del tope se atenua su resplandor
  g_fxk = 1.0f;
  if (FxB3()) {
    float area = 0;
    for (int i = 0; i < j.nEfectos; ++i) {
      const Efecto2D& e = j.efectos[i];
      if (e.forma != Forma::Rayo || e.hostil) continue;
      const ImVec2 a = v.P(e.x, e.y), b = v.P(e.x2, e.y2);
      area += std::hypot(b.x - a.x, b.y - a.y) * std::max(3.0f, e.ancho * v.S) * 2.2f;
    }
    if (area > kTopeFx * W * H) g_fxk = std::max(0.18f, kTopeFx * W * H / area);
    if (std::fabs(g_fxk - g_fxk_dicho) > 0.15f && g_fxk < 1.0f && g_fxk_log < 8) {
      ++g_fxk_log;
      g_fxk_dicho = g_fxk;
      REXLOG_INFO("dbz3 survivors: fx_b3: tope de saturacion x{:.2f} (rayos cubren {:.0f} % de la pantalla)", g_fxk, 100.0f * area / (W * H));
    }
  }
  // efectos de suelo y gemas
  CargarFx();
  auto tf = Reloj::now();
  if(!fxreal::Activo() || j.pasos<g_pasos_prev)g_fx_reales.clear();
  for (int i = 0; i < j.nEfectos; ++i)
    if(j.efectos[i].hostil || std::find(g_fx_reales.begin(),g_fx_reales.end(),j.efectos[i].id_visual)==g_fx_reales.end())
      EfectoSuelo(dl, v, j.efectos[i]);
  double fx_ms = Ms(tf, Reloj::now());
  td = Reloj::now();
  Hojas(dl, v, true, W, H);   // grietas y polvo
  double hojas_ms = Ms(td, Reloj::now());
  const DefRecogibles& rc = datos_.recogibles;
  for (int i = 0; i < j.nGemas; ++i) {
    const ImVec2 p = v.P(j.gx[i], j.gy[i]);
    if (p.x < -40 || p.x > W + 40 || p.y < -60 || p.y > H + 40) continue;
    const float val = j.gvalor[i];
    const float b = 0.5f + 0.5f * std::sin(t_ * 5.0f + float(i));
    // gemas de la tanda 3: la del valor, sin igualar tamanos (su escala es el valor); el lienzo de 128 px mide 0,8 de mundo
    if (const ps2::Tex* g = T3(val >= rc.gema_roja ? kGemaRoja : val >= rc.gema_verde ? kGemaVerde : kGemaAzul)) {
      dl->AddEllipseFilled(p, ImVec2(v.S * 0.2f, v.S * 0.1f * v.sp), IM_COL32(0, 0, 0, 60));
      DibujarCuadro(dl, g, 0, 1, ImVec2(64, 124), ImVec2(p.x, p.y - v.S * 0.08f * b), v.S * 0.8f / 128.0f);
      continue;
    }
    const uint32_t c = val >= rc.gema_roja ? 0xFF4060 : val >= rc.gema_verde ? 0x50E070 : 0x50B0FF;
    const float r = v.S * (val >= rc.gema_roja ? 0.32f : val >= rc.gema_verde ? 0.26f : 0.21f);   // sin el arte: el rombo de antes
    const ImVec2 q(p.x, p.y - r * (0.9f + 0.15f * b));   // flota un poco sobre el suelo
    dl->AddEllipseFilled(p, ImVec2(r * 0.8f, r * 0.8f * v.sp * 0.5f), IM_COL32(0, 0, 0, 60));
    dl->AddCircleFilled(q, r * 1.7f, Rgb(c, 0.16f + 0.1f * b), 12);
    dl->AddQuadFilled(ImVec2(q.x, q.y - r * 1.25f), ImVec2(q.x + r * 0.8f, q.y), ImVec2(q.x, q.y + r * 1.25f), ImVec2(q.x - r * 0.8f, q.y), Rgb(c, 1));
    dl->AddQuadFilled(ImVec2(q.x, q.y - r * 1.0f), ImVec2(q.x + r * 0.35f, q.y - r * 0.15f), ImVec2(q.x, q.y + r * 0.2f), ImVec2(q.x - r * 0.25f, q.y - r * 0.2f),
                      IM_COL32(255, 255, 255, 170));
  }

  // enemigos: cuadro de cada uno, fuera los de fuera, sombra, y orden por los pies (sin memoria nueva)
  for (int i = 0, n = j.NumEnemigos(); i < n && m < kSpr; ++i) {
    const VistaEnemigo e = j.Enemigo(i);
    const ImVec2 pies = v.P(e.x, e.y);
    const float giro = std::atan2(v.jx - e.x, e.y - v.jy);   // de cara al jugador
    Sprite s;
    // A06: mientras avisa de su ataque, la hoja de ataque de B3 (tipos 2n..3n-1, si la pantalla de carga la horneo); si no, la de andar
    const int t_at = 2 * int(datos_.enemigos.size()) + e.tipo;
    const int t_golpe = 3 * int(datos_.enemigos.size()) + e.tipo;
    const bool golpe = e.destello > 0 && imp_.Cuadro(t_golpe, giro, std::min((1.0f - e.destello) / (7.0f * imp_.Vuelta(t_golpe)), 0.999f),
                                                       pies, v.S * kAlto * e.escala, &s);
    const bool atq = !golpe && e.aviso > 0 && imp_.Estado(t_at) == 2 &&
                     imp_.Cuadro(t_at, giro, std::fmod(t_ / imp_.Vuelta(t_at) + 0.37f * float(i), 1.0f), pies, v.S * kAlto * e.escala, &s);
    if (!golpe && !atq && !imp_.Cuadro(e.tipo, giro, e.fase, pies, v.S * kAlto * e.escala, &s)) continue;
    if (s.b.x < 0 || s.a.x > W || s.a.y > H || s.b.y < 0) continue;
    if ((golpe || atq) && e.tipo >= 0 && e.tipo < 32) {   // SUR-03 (prueba): primera vez que se ve su hoja de ataque / golpe real de B3, por tipo
      static uint32_t visto_atq = 0, visto_golpe = 0;
      uint32_t& vis = golpe ? visto_golpe : visto_atq;
      if (!(vis & (1u << e.tipo))) {
        vis |= 1u << e.tipo;
        REXLOG_INFO("dbz3 survivors: animacion {} en pantalla: enemigo {}", golpe ? "de golpe" : "de ataque", datos_.enemigos[size_t(e.tipo)].id);
      }
    }
    const float rs = v.S * 0.42f * e.escala;
    dl->AddEllipseFilled(pies, ImVec2(rs, rs * v.sp * 0.5f), IM_COL32(0, 0, 0, 70), 0, 10);
    if (e.anillo > 0 && e.anillo <= 3)   // tanda 4: anillo de elite bajo los pies (ya pintado a 55 grados: sin aplastar; tinta de 203 px)
      if (const ps2::Tex* an = T3(kAnilloOro + e.anillo - 1))
        DibujarCuadro(dl, an, 0, 1, ImVec2(128, 126), pies, datos_.enemigos[size_t(e.tipo)].radio * 3.2f * v.S / 203.0f,
                      IM_COL32(255, 255, 255, int(200 + 55 * std::sin(t_ * 5.0f + float(i)))));
    spr_[m] = s, dest_[m] = e.destello, alfa_[m] = 1.0f;
    if (e.aviso > 0) dest_[m] = std::max(dest_[m], 0.55f + 0.45f * std::sin(t_ * 25.0f));   // A06: parpadea mientras avisa de su ataque
    if (e.escudo > 0 && fx_.halo) {   // A06: escudo del elite (burbuja aditiva detras de la figura; se apaga segun se gasta)
      Aditiva(dl, true);
      Spr(dl, fx_.halo, ImVec2(pies.x, pies.y - v.S * 0.9f * e.escala), v.S * 2.1f * e.escala, v.S * 2.6f * e.escala, Rgb(0x50C8FF, 0.30f + 0.45f * e.escudo));
      Aditiva(dl, false);
    }
    claves_[m] = (uint64_t(Bits(pies.y)) << 32) | uint32_t(m);
    ++m;
  }
  // caidos: el clip de derrota de su tipo una vez (los cuadros del atlas van del primero al ultimo), quietos en la ultima pose y
  // desvaneciendose. Los que ya acabaron salen de la lista (sin memoria nueva).
  const int ntip = int(datos_.enemigos.size());
  for (int i = 0; i < n_caidos_ && m < kSpr;) {
    const Caido& c = caidos_[i];
    const float vuelta = imp_.Vuelta(ntip + c.tipo), t = ta_ - c.t0, fuera = std::clamp((t - vuelta - 0.25f) / 0.45f, 0.0f, 1.0f);
    if (fuera >= 1.0f || c.tipo < 0 || c.tipo >= ntip) {
      caidos_[i] = caidos_[--n_caidos_];
      continue;
    }
    ++i;
    const ImVec2 pies = v.P(c.x, c.y);
    const float esc = datos_.enemigos[size_t(c.tipo)].escala;
    Sprite s;
    if (!imp_.Cuadro(ntip + c.tipo, c.giro, std::min(t / vuelta, 0.999f), pies, v.S * kAlto * esc, &s)) continue;
    if (s.b.x < 0 || s.a.x > W || s.a.y > H || s.b.y < 0) continue;
    spr_[m] = s, dest_[m] = std::max(0.0f, 1.0f - t * 4.0f), alfa_[m] = 1.0f - fuera;
    claves_[m] = (uint64_t(Bits(pies.y)) << 32) | uint32_t(m);
    ++m;
  }
  // objetos (tanda 3) con su ancla en la base, ordenados con las figuras; flotan un poco salvo el cofre
  for (int i = 0; i < j.nObjetos && m < kSpr; ++i) {
    const Objeto& o = j.objetos[i];
    const ps2::Tex* t = T3(kObjeto0 + int(o.tipo));
    if (!t || t->w <= 0) continue;
    const bool cofre = o.tipo == TipoObjeto::Cofre;
    const float esc = v.S * (cofre ? 1.4f : 1.0f) / 128.0f;
    const ImVec2 pies = v.P(o.x, o.y), q(pies.x, pies.y - (cofre ? 0.0f : v.S * (0.12f + 0.08f * std::sin(t_ * 4.0f + float(i)))));
    if (pies.x < -128 * esc || pies.x > W + 128 * esc || pies.y < 0 || pies.y - 128 * esc > H) continue;
    dl->AddEllipseFilled(pies, ImVec2(v.S * 0.32f, v.S * 0.16f * v.sp), IM_COL32(0, 0, 0, 70));
    Sprite& s = spr_[m];
    s.tex = t->id(), s.a = ImVec2(q.x - 64 * esc, q.y - 124 * esc), s.b = ImVec2(s.a.x + 128 * esc, s.a.y + 128 * esc), s.uv0 = ImVec2(0, 0), s.uv1 = ImVec2(1, 1);
    dest_[m] = 0, alfa_[m] = 1;
    claves_[m] = (uint64_t(Bits(pies.y)) << 32) | uint32_t(m);
    ++m;
  }
  // tanda 4: rompibles (entero o roto en el mismo origen y ancla 128,252; 256 px = 1,7 de mundo), ordenados con las figuras
  for (int i = 0; i < j.nRompibles && m < kSpr; ++i) {
    const Rompible& r = j.rompibles[i];
    const ps2::Tex* t = RompTex(r.tipo, r.roto >= 0);
    if (!t || t->w <= 0) continue;
    const float esc = 1.7f * v.S / 256.0f;   // un barril queda en ~3/4 del alto de un luchador
    const ImVec2 pies = v.P(r.x, r.y);
    if (pies.x < -128 * esc || pies.x > W + 128 * esc || pies.y < 0 || pies.y - 256 * esc > H) continue;
    if (r.roto < 0) dl->AddEllipseFilled(pies, ImVec2(v.S * 0.75f, v.S * 0.75f * v.sp * 0.5f), IM_COL32(0, 0, 0, 70), 0, 16);
    Sprite& sp = spr_[m];
    sp.tex = t->id(), sp.a = ImVec2(pies.x - 128 * esc, pies.y - 252 * esc), sp.b = ImVec2(sp.a.x + 256 * esc, sp.a.y + 256 * esc);
    sp.uv0 = ImVec2(0, 0), sp.uv1 = ImVec2(1, 1);
    dest_[m] = r.roto >= 0 ? std::max(0.0f, 1.0f - r.roto * 5.0f) : 0.0f, alfa_[m] = 1;   // destello al romperse
    claves_[m] = (uint64_t(Bits(pies.y)) << 32) | uint32_t(m);
    ++m;
  }
  std::sort(claves_, claves_ + m);
  const int corte = int(std::lower_bound(claves_, claves_ + m, uint64_t(Bits(v.cy)) << 32) - claves_);   // detras del jugador | delante
  Enemigos(dl, 0, corte);

  // jugador y jefe en 3D: camara de perspectiva de fov pequeno y lejos, con la misma inclinacion (prueba.cpp)
  ps2::Camara cam;
  cam.fov = 0.1f, cam.c = ImVec2(v.cx, v.cy), cam.h = ImVec2(W * 0.5f, H * 0.5f), cam.ok = true, cam.za = cam.zb = 0;
  const float D = cam.Foco() / v.S, ex = v.jx, ey = v.sp * D, ez = -v.jy + v.cp * D;   // ojo = pies del jugador + atras * D (z = -y)
  cam.m[0] = 1, cam.m[4] = 0, cam.m[8] = 0, cam.m[12] = -ex;
  cam.m[1] = 0, cam.m[5] = v.cp, cam.m[9] = -v.sp, cam.m[13] = -(v.cp * ey - v.sp * ez);
  cam.m[2] = 0, cam.m[6] = v.sp, cam.m[10] = v.cp, cam.m[14] = -(v.sp * ey + v.cp * ez);
  cam.m[15] = 1;
  esc_.Empezar();
  const Jugador& p = j.jugador;
  const DefPersonaje& dp = datos_.personajes[size_t(j.personaje)];
  const Modelo mp = ModeloDe(dp.fid);
  bool hay_forma = false;
  const Modelo mf = Formas() && j.forma > 0 ? FormaDe(dp.fid, j.forma, &hay_forma) : mp;
  if (Formas() && j.pasos < g_pasos_prev)   // A05: se piden ya los modelos de las transformaciones (tardan minutos la primera vez)
    for (int f = 1; f <= std::min(2, datos_.formas.max); ++f) {
      bool ok = false;
      const Modelo m = FormaDe(dp.fid, f, &ok);
      if (ok) pasarela::nave::EstadoFiguraB3(m.b3, m.forma, m.fid);
    }
  const ImVec2 pj = v.P(p.x, p.y);
  dl->AddEllipseFilled(pj, ImVec2(v.S * 0.5f * g_escj, v.S * 0.5f * g_escj * v.sp * 0.5f), IM_COL32(0, 0, 0, 90), 0, 16);
  if (Jugo()) {   // A01: anillo de color en los pies (el de la tecnica inicial del luchador): el jugador se encuentra siempre en la horda
    const uint32_t col = dp.tecnica >= 0 && dp.tecnica < int(datos_.tecnicas.size()) ? datos_.tecnicas[size_t(dp.tecnica)].color : 0xFFD040;
    const float pul = 0.85f + 0.15f * std::sin(t_ * 4.0f), R = std::max(0.78f * v.S * g_escj, 34.0f * k_);   // con el zoom mas lejano no baja de ~34 px (a 1080)
    if (const ps2::Tex* an = Arte("tanda5/jugador_anillo.png")) {   // B10: la pieza del encargo (256x128, blanca: se tine); ya viene en perspectiva
      const float w = R * 2.4f, h = w * 0.5f, d = std::max(2.0f, v.S * 0.04f);
      dl->AddEllipseFilled(pj, ImVec2(R * 0.95f, R * 0.47f), Rgb(col, 0.12f), 0, 32);
      DibujarImagen(dl, an, ImVec2(pj.x - w * 0.5f + d, pj.y - h * 0.5f + d), ImVec2(pj.x + w * 0.5f + d, pj.y + h * 0.5f + d), IM_COL32(0, 0, 0, 140));   // sombra: se lee sobre suelo claro
      DibujarImagen(dl, an, ImVec2(pj.x - w * 0.5f, pj.y - h * 0.5f), ImVec2(pj.x + w * 0.5f, pj.y + h * 0.5f), Rgb(col, 0.8f + 0.2f * pul));
    } else {   // sin la pieza: el anillo dibujado por codigo
      const ImVec2 rr(R, R * v.sp), rr2(R * 1.18f, R * 1.18f * v.sp);
      dl->AddEllipseFilled(pj, rr, Rgb(col, 0.14f), 0, 32);
      dl->AddEllipse(pj, rr, IM_COL32(0, 0, 0, 150), 0, 32, std::max(4.0f, v.S * 0.1f));   // contorno oscuro: se lee sobre suelo claro y oscuro
      dl->AddEllipse(pj, rr, Rgb(col, 0.95f * pul), 0, 32, std::max(2.0f, v.S * 0.06f));
      dl->AddEllipse(pj, rr2, Rgb(col, 0.35f * pul), 0, 32, std::max(1.5f, v.S * 0.025f));
    }
    if (j.pasos < g_pasos_prev || (j.pasos % 600) == 0)   // registro: alto nominal del luchador y su anillo (las pruebas lo leen)
      REXLOG_INFO("dbz3 survivors: jugo: jugador alto={:.0f} px (nominal, x{:.2f}) zoom={:.2f} anillo={:06X}", v.S * kAlto * g_escj, g_escj, zoom_, col);
  }
  g_pasos_prev = j.pasos;
  if (Formas()) {   // A05: aura aditiva mientras esta transformado y barra de ki bajo los pies
    if (j.forma > 0) {
      const float lat = 0.9f + 0.1f * std::sin(t_ * 23.0f);
      Aditiva(dl, true);
      Spr(dl, fx_.halo, ImVec2(pj.x, pj.y - v.S * 0.9f * g_escj), v.S * 1.6f * g_escj * lat, v.S * 2.4f * g_escj, Rgb(j.forma > 1 ? 0xFF6020 : 0xFFD030, 0.30f));   // 13/11: sin nucleo blanco (blanqueaba)
      Aditiva(dl, false);
    }
    const float bw = v.S * 1.1f, bh = std::max(3.0f, v.S * 0.09f), by = pj.y + v.S * 0.82f * v.sp;
    dl->AddRectFilled(ImVec2(pj.x - bw * 0.5f - 1, by - 1), ImVec2(pj.x + bw * 0.5f + 1, by + bh + 1), IM_COL32(0, 0, 0, 170), bh * 0.5f);
    dl->AddRectFilled(ImVec2(pj.x - bw * 0.5f, by), ImVec2(pj.x - bw * 0.5f + bw * std::clamp(j.ki, 0.0f, 1.0f), by + bh),
                      j.forma > 0 ? IM_COL32(255, 200, 40, 255) : IM_COL32(90, 190, 255, 230), bh * 0.5f);
  }
  const bool parpadeo = !fin_ && p.invulnerable > 0 && std::fmod(t_, 0.14f) < 0.07f;
  const bool accion = fin_ || pj_.fin > ta_;   // en una accion mira hacia donde lanza; si no, hacia donde anda
  if (!parpadeo) {
    const float gj = accion && !fin_ ? pj_.giro : giro_;
    if (!(hay_forma && Pintar3D(pj_, cam, mf, mf.fid, p.x, p.y, kAlto * g_escj, gj))) Pintar3D(pj_, cam, mp, dp.fid, p.x, p.y, kAlto * g_escj, gj);   // sin el modelo de la forma, el de siempre
  }
  const Jefe& b = j.jefe;
  if (b.estado != EstadoJefe::Ausente) {
    const DefJefe& dj = j.DJ();
    const ImVec2 pb = v.P(b.x, b.y);
    const bool v2 = j.config.jefes_v2;
    const float esj = dj.escala * (Jugo() || v2 ? kEscJefe : 1.0f);   // 13/11: el jefe se dibuja ~1,6x mas grande (la simulacion no cambia)
    dl->AddEllipseFilled(pb, ImVec2(v.S * 0.5f * esj, v.S * 0.25f * esj * v.sp), IM_COL32(0, 0, 0, 110), 0, 16);
    if (Jugo() || v2) {   // anillo rojo en los pies: se lee sobre cualquier suelo (contorno oscuro + color que late)
      const float pul = 0.8f + 0.2f * std::sin(t_ * 5.0f), R = v.S * 0.55f * esj;
      const ImVec2 rr(R, R * v.sp * 0.5f);
      dl->AddEllipseFilled(pb, rr, Rgb(0xFF3020, 0.14f), 0, 32);
      dl->AddEllipse(pb, rr, IM_COL32(0, 0, 0, 160), 0, 32, std::max(4.0f, v.S * 0.1f));
      dl->AddEllipse(pb, rr, Rgb(0xFF4030, 0.95f * pul), 0, 32, std::max(2.5f, v.S * 0.06f));
    }
    const int fase_dib = std::max(1, b.fase - (b.estado == EstadoJefe::Transforma && b.t < 0.6f ? 1 : 0));   // la forma nueva aparece a mitad del destello
    if (v2 && j.pasos < g_pasos_prev)   // se piden ya los dos modelos del jefe (la primera vez tardan)
      for (int f = 1; f <= std::max(2, j.NFases()); ++f) {
        const Modelo m = ModeloJefe(dj, f, true, j.config.jefes_dos_fases);
        pasarela::nave::EstadoFiguraB3(m.b3, m.forma, m.fid);
      }
    const Modelo mj = ModeloJefe(dj, fase_dib, v2, j.config.jefes_dos_fases);
    Pintar3D(jefe_, cam, mj, mj.fid, b.x, b.y, kAlto * esj, std::atan2(b.dx, -b.dy));
  }
  // Efectos propios detras del luchador para que siga visible; los hostiles conservan su orden.
  tf = Reloj::now();
  pase_rayo_ = 1;
  std::vector<fxreal::Pedido> fx_pedidos;
  std::vector<uint64_t> fx_dibujados;
  if(fxreal::Activo()) {
    const int b3j=B3Jugador();
    const int bsp_j=b3j>=0 && b3j<64 ? fxreal::kBspB3[b3j] : -1;
    const int aura_j=b3j>=0 && b3j<64 ? fxreal::kAuraB3[b3j] : -1;
    const auto ahora_atlas=Reloj::now();
    const auto atlas=[&](int bsp) {
      // Carga perezosa por entrada, tambien al cambiar de luchador. Un fallo
      // de E/S no se repite por cada efecto ni en cada cuadro.
      struct Carga { bool ok=false; Reloj::time_point reintento{}; };
      static Carga cargas[556]{};
      if(bsp<0 || bsp>=int(sizeof cargas/sizeof *cargas))return false;
      auto& c=cargas[bsp];
      if(!c.ok && ahora_atlas>=c.reintento) {
        c.reintento=ahora_atlas+std::chrono::seconds(10);
        c.ok=dbz3::ingame::native_ui::CombatAtlas(bsp);
      }
      return c.ok;
    };
    atlas(504);
    const auto anclar_rayo=[&](fxreal::Pedido& p,const Efecto2D& e,int id,int bsp,int codigo) {
      const float dx=e.x2-e.x, dy=e.y2-e.y, l=std::hypot(dx,dy);
      p.b3=id; p.bsp=bsp; p.cst_codigo=codigo; p.repetir=true;
      p.x=e.x2; p.y=e.y2; p.giro=std::atan2(dx,-dy);
      p.escala=std::clamp(e.ancho*.1f,.015f,.1f);
      p.largo_rayo=(l-std::min(kManos,l*.5f))/p.escala;
    };
    const auto poner=[&](fxreal::Pedido p) {
      // Ojo de la camara del modo; se deshace el giro del ancla para calcular
      // el perpendicular de 82150500 en las mismas coordenadas que el ACE.
      const float cg=std::cos(p.giro),sg=std::sin(p.giro);
      p.ojo={-1000*v.cp*sg,1000*v.sp,1000*v.cp*cg};
      fx_pedidos.push_back(p);
    };
    for(int i=0;i<j.nEfectos;++i) {
      const auto& e=j.efectos[i];
      if(e.hostil && e.forma==Forma::Explosion && e.def==255 && e.radio>=5.0f) {   // SUR-8: estallido de transformacion del jefe = explosion del motor real de B3
        fxreal::Pedido p{e.id_visual,e.x,e.y};
        p.ace=12; p.escala=.07f;
        poner(p);
        continue;
      }
      if(e.hostil) {
        // PM-26: muestra la cabeza nativa como complemento del rayo hostil.
        // La ayuda, la franja, el cuerpo y el dano siguen bajo control del modo.
        static const bool prueba_jefe=[] {
          const char* val=std::getenv("DBZ3_TEST_SURVIVORS_FX_JEFE");
          return val && std::strcmp(val,"1")==0;
        }();
        if(!(prueba_jefe || REXCVAR_GET(dbz3_survivors_fx_jefe_nativo)) || !combate_ || !j.config.jefes_v2 || e.def!=255 ||
           e.forma!=Forma::Rayo || b.estado!=EstadoJefe::Kit || fondo_b3_[1]!=B3Jefe())continue;
        const int carta=CartaJefe(b.ataque);
        const int portador=carta==117 ? 33 : carta==100 ? 27 : carta==134 ? 38 : -1;
        if(portador<0 || portador!=fondo_b3_[1])continue;
        const int id=fondo_b3_[1], bsp=fxreal::kBspB3[id];
        if(!atlas(bsp))continue;
        // Bit 60: lado 1. La franja de peligro sigue siendo del modo.
        fxreal::Pedido p{e.id_visual|(1ull<<60),e.x2,e.y2};
        anclar_rayo(p,e,id,bsp,0);
        poner(p);
        continue;
      }
      fxreal::Pedido p{e.id_visual,e.x,e.y};
      switch(e.forma) {
        case Forma::Bola: p.repetir=true; break;
        case Forma::Rayo:
          if(!combate_ || fondo_b3_[0]!=b3j || !atlas(bsp_j))continue;
          p.id|=1ull<<62;
          // Ten Shin Han y Raditz tienen la otra entrada de haz en codigo 1.
          anclar_rayo(p,e,b3j,bsp_j,b3j==12 || b3j==18 ? 1 : 0);
          break;
        case Forma::Explosion:
          p.ace=12; p.escala=.04f;
          if(b3j==0 && combate_ && fondo_b3_[0]==0 && atlas(bsp_j))p.b3=0,p.banco=2,p.ace=4,p.bsp=bsp_j;
          break;
        case Forma::Aura:
          if(!combate_ || fondo_b3_[0]!=b3j || !atlas(aura_j))continue;
          // 820BB1E8 -> 820BB6C8: ACE 10 del AMB0 del bin de aura.
          p.b3=b3j; p.banco=6; p.ace=10; p.bsp=aura_j;
          p.alto=0; p.escala=.025f; p.repetir=true;
          break;
        case Forma::Anillo: p.ace=8; p.alto=0; break;
        case Forma::Cono: p.ace=1; break;
        case Forma::Disco:
          // El AWO de Kienzan se dibuja en AireNativo; no publicar el sprite ACE por separado.
          continue;
        default: continue;
      }
      poner(p);
    }
    // Los impactos son sucesos, no proyectiles. ID por celda y nacimiento:
    // nunca se reutiliza solo el indice del buffer circular.
    for(int i=0;i<std::min(n_vis_,kVis);++i) {
      const auto& f=vis_[i];
      if(f.hoja!=kImpacto && f.hoja!=kExplosion)continue;
      const float edad=(f.real ? t_ : ta_)-f.t0;
      if(edad<0 || edad>=float(kHojas[f.hoja].cuadros)/kHojas[f.hoja].fps)continue;
      fxreal::Pedido p{IdHojaReal(i,f.t0),f.sigue ? j.jugador.x : f.x,f.sigue ? j.jugador.y : f.y};
      p.ace=f.hoja==kImpacto ? 1 : 12; p.alto=f.alto; p.escala=.04f*f.mult;
      poner(p);
    }
    fxreal::Publicar(j.pasos,fx_pedidos);
    for(const auto& q:fxreal::Leer()) {
      const auto it=std::find_if(fx_pedidos.begin(),fx_pedidos.end(),[&](const auto& p){return p.id==q.id;});
      if(it==fx_pedidos.end() || it->bsp!=q.bsp)continue;
      const int ordinal=fxreal::OrdinalAtlas(q.bsp,q.textura);
      if(q.textura>=0 && ordinal<0)continue;
      const auto nombre="fxreal/"+std::to_string(q.bsp)+"/"+std::to_string(ordinal);
      const auto tex=dbz3::ingame::style::Img(nombre.c_str());
      if(q.textura>=0 && !tex)continue;
      ImVec2 pos[4];
      const float cg=std::cos(it->giro),sg=std::sin(it->giro);
      const auto proyectar=[&](fxreal::Vertice a) {
        ImVec2 r=v.P(it->x+(cg*a.x+sg*a.z)*it->escala,it->y-(-sg*a.x+cg*a.z)*it->escala);
        r.y-=v.S*(it->alto+v.cp*a.y*it->escala);
        return r;
      };
      const ImVec2 centro=proyectar(q.centro);
      for(int k=0;k<4;++k) {
        // Unidades de combate -> mundo Survivors. La camara y el ancla son
        // las del modo; giro, tamano, color y cuadro vienen del invitado.
        if(q.frontal)pos[k]={centro.x+v.S*q.v[k].x*it->escala,centro.y-v.S*q.v[k].y*it->escala};
        else pos[k]=proyectar(q.v[k]);
      }
      const float area=std::abs((pos[1].x-pos[0].x)*(pos[3].y-pos[0].y)-(pos[1].y-pos[0].y)*(pos[3].x-pos[0].x));
      if(!std::isfinite(area) || area<.01f)continue; // una estela sin direccion no sustituye al respaldo
      float u0=q.uv[0],v0=q.uv[1],u1=q.uv[2],v1=q.uv[3];
      if(tex && std::max(std::abs(u0),std::abs(u1))>1.01f)u0/=tex.w,u1/=tex.w;
      if(tex && std::max(std::abs(v0),std::abs(v1))>1.01f)v0/=tex.h,v1/=tex.h;
      const auto ch=[](float f){return int(std::clamp(f,0.f,1.f)*255.f);};
      if(q.aditiva)Aditiva(dl,true);
      if(tex)dl->AddImageQuad(tex.tex,pos[0],pos[1],pos[2],pos[3],ImVec2(u0,v0),ImVec2(u1,v0),ImVec2(u1,v1),ImVec2(u0,v1),IM_COL32_WHITE);
      else {
        const ImVec2 uv=ImGui::GetFontTexUvWhitePixel();
        dl->PrimReserve(6,4); dl->PrimQuadUV(pos[0],pos[1],pos[2],pos[3],uv,uv,uv,uv,IM_COL32_WHITE);
      }
      for(int k=0;k<4;++k) {
        const auto* c=q.rgba[k];
        dl->VtxBuffer[dl->VtxBuffer.Size-4+k].col=IM_COL32(ch(c[0]),ch(c[1]),ch(c[2]),ch(c[3]));
      }
      if(q.aditiva)Aditiva(dl,false);
      fx_dibujados.push_back(q.cuerpo_rayo ? q.id|(1ull<<61) : q.id);
      if(q.cuerpo_rayo) {
        static std::vector<int> dichos;
        const int llave=q.bsp*2+int((q.id&(1ull<<60))!=0);
        if(std::find(dichos.begin(),dichos.end(),llave)==dichos.end()) {
          dichos.push_back(llave);
          REXLOG_INFO("dbz3 survivors: FX cuerpo dibujado CST={} bsp={} textura={} lado={}",it->cst_codigo,q.bsp,q.textura,llave%2);
        }
        continue;
      }
      // Evidencia de dibujo, distinta de crear un ACE: exige atlas y geometria.
      static std::vector<uint64_t> fuentes_dichas;
      const uint64_t fuente=(q.id&(1ull<<60)) | (uint64_t(q.bsp)<<32) | uint32_t(q.banco*512+q.ace);
      if(std::find(fuentes_dichas.begin(),fuentes_dichas.end(),fuente)==fuentes_dichas.end()) {
        fuentes_dichas.push_back(fuente);
        REXLOG_INFO("dbz3 survivors: FX dibujado banco={} indice={} bsp={} textura={} lado={}",q.banco,q.ace,q.bsp,q.textura,(fuente>>60)&1);
      }
    }
  }
  g_fx_reales=fx_dibujados;
  for (int i = 0; i < j.nEfectos; ++i)
    if ((!j.efectos[i].hostil || j.efectos[i].forma == Forma::Rayo) &&
        std::find(fx_dibujados.begin(),fx_dibujados.end(),j.efectos[i].id_visual)==fx_dibujados.end()) EfectoAire(dl, v, j.efectos[i]);
  pase_rayo_ = 0;
  fx_ms += Ms(tf, Reloj::now());
  esc_.Pintar(dl);
  // ponytail: un solo corte de profundidad (el del jugador); el jefe puede quedar mal ordenado con algun enemigo pegado a el
  {   // El decorado y los enemigos que tapan al luchador son translucidos tambien sin Jugo.
    const float hw = v.S * 0.55f * g_escj, alto = v.S * 1.9f * g_escj;
    const ImVec2 pa(pj.x - hw, pj.y - alto), pb(pj.x + hw, pj.y);
    for (int q = corte; q < m; ++q) {
      const int i = int(claves_[q] & 0xFFFFFFFFu);
      const Sprite& s = spr_[i];
      if (s.a.x < pb.x && s.b.x > pa.x && s.a.y < pb.y && s.b.y > pa.y) alfa_[i] = std::min(alfa_[i], 0.18f);
    }
  }
  Enemigos(dl, corte, m);

  // efectos en el aire (bolas, discos, rayos, explosiones) y chispas de los golpes
  tf = Reloj::now();
  for (int i = 0; i < j.nEfectos; ++i)
    if (j.efectos[i].hostil) EfectoAire(dl, v, j.efectos[i]);
  if (!fx_.impacto && !T3(kHoja0 + kImpacto)) Chispas(dl, v);   // sin estallido del juego ni hoja: las chispas sueltas
  fx_ms += Ms(tf, Reloj::now());
  fx_suma_ += fx_ms;
  td = Reloj::now();
  Hojas(dl, v, false, W, H);   // impacto, humo, destello de gema, subida de nivel, explosion grande, cofre abierto
  Cifras(dl, v);
  if (Formas()) {   // A05: destello de la transformacion: halo aditivo que se abre sobre el luchador y rayas verticales (0,6 s)
    const float u = (t_ - g_forma_t0) / 0.6f;
    if (u >= 0 && u < 1 && fx_.halo) {
      const ImVec2 c(pj.x, pj.y - v.S * 0.9f * g_escj);
      Aditiva(dl, true);
      Spr(dl, fx_.halo, c, v.S * (2.0f + 7.0f * u), v.S * (2.0f + 7.0f * u), IM_COL32(255, 255, 255, int(235 * (1 - u))));
      if (fx_.nucleo) Spr(dl, fx_.nucleo, c, v.S * 3.0f * (1 - u), v.S * 9.0f * (1 - u * 0.5f), Rgb(0xFFE060, 0.7f * (1 - u)));
      Aditiva(dl, false);
    }
  }
  hojas_ms += Ms(td, Reloj::now());
  hojas_suma_ += hojas_ms;
  if (p.destello > 0) dl->AddCircleFilled(ImVec2(pj.x, pj.y - v.S * 0.9f * g_escj), v.S * 0.9f * g_escj, IM_COL32(255, 60, 40, int(90 * p.destello)), 20);
  // tanda 4: ambiente del escenario encima del mundo (y debajo del marcador, que va en la lista de delante); sigue en la pausa
  const auto tamb = Reloj::now();
  for (size_t k = 0; k < es.ambiente.size(); ++k)
    DibujarAmbiente(dl, es.ambiente[k].c_str(), es.ambiente_alfa[k], W, H, ImVec2(v.S * v.jx, -v.S * v.sp * v.jy), t_, k_ * zoom_ / 1.5f);
  amb_suma_ += Ms(tamb, Reloj::now());
  if (j.cegado > 0) {   // SUR-04: Taiyoken: el destello blanquea la pantalla y se va apagando (el jugador sigue a medio paso hasta que acaba)
    const float u = std::clamp(j.cegado / 2.5f, 0.0f, 1.0f), a = u > 0.8f ? 0.95f : 0.55f * u / 0.8f;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(255, 255, 240, int(255 * a)));
    if (const ps2::Tex* an = Arte("encargo4/taiyoken_anillo.png")) {   // encargo 4: el anillo de luz del Taiyoken se abre y se apaga
      const float r = H * (0.35f + 0.9f * (1.0f - u));
      DibujarImagen(dl, an, ImVec2(W * 0.5f - r, H * 0.5f - r), ImVec2(W * 0.5f + r, H * 0.5f + r), IM_COL32(255, 255, 255, int(255 * std::min(1.0f, u * 1.2f))));
    }
  }
  // B10: vineta de peligro con la vida por debajo del 30 % (mas marcada cuanto menos queda; late). Sin la pieza de arte no se pinta nada.
  if (Jugo() && !fin_ && p.vida_max > 0 && p.vida < 0.3f * p.vida_max)
    if (const ps2::Tex* vi = Arte("tanda5/pantalla_vineta_peligro.png")) {
      const float falta = 1.0f - p.vida / (0.3f * p.vida_max), a = (0.45f + 0.45f * falta) * (0.85f + 0.15f * std::sin(t_ * 6.0f));
      DibujarImagen(dl, vi, ImVec2(0, 0), ImVec2(W, H), IM_COL32(255, 255, 255, int(255 * a)));
      static bool dicho = false;
      if (!dicho) {
        dicho = true;
        REXLOG_INFO("dbz3 survivors: jugo: vineta de peligro vida={:.0f}/{:.0f}", p.vida, p.vida_max);
      }
    }
  // retrato 3D del jefe en su presentacion: aqui (lista de fondo) para quedar debajo del marco y del rotulo (lista de delante)
  Pres pr;
  if (PresGeom(W, H, &pr) && pr.a > 0.5f) {
    const float z = 96.0f * pr.ps;   // zona segura del retrato: 32,32 + 192x192 del marco de 256
    const ImVec2 za(pr.pc.x - z, pr.pc.y - z), zb(pr.pc.x + z, pr.pc.y + z);
    dl->AddRectFilledMultiColor(za, zb, IM_COL32(110, 20, 30, 255), IM_COL32(110, 20, 30, 255), IM_COL32(20, 8, 30, 255), IM_COL32(20, 8, 30, 255));
    // R23 SURV-7: sin busto 3D (salia de su ventana y se salia del marco); el retrato de B3 va en Marcador
  }
}

bool Modo::PresGeom(float W, float H, Pres* o) const {
  constexpr float kDura = 3.4f;
  if (pres_t_ < 0 || pres_t_ > kDura) return false;
  o->a = std::clamp(std::min(pres_t_ / 0.25f, (kDura - pres_t_) / 0.4f), 0.0f, 1.0f);
  const float rw = std::min(W * 0.62f, 960.0f * k_) * 0.75f * (0.94f + 0.06f * Suave(pres_t_ / 0.3f)), pw = rw * 0.22f, solapa = pw * 0.15f;
  const float gx = (W - (pw + rw - solapa)) * 0.5f, cy = H * 0.22f;   // R23 SURV-7: mas arriba (0,29 tapaba la accion junto al jugador)
  o->rs = rw / 1280.0f, o->ps = pw / 256.0f;
  o->pc = ImVec2(gx + pw * 0.5f, cy), o->rc = ImVec2(gx + pw - solapa + rw * 0.5f, cy);
  return true;
}

const ps2::Tex* Modo::RompTex(int tipo, bool roto) {
  if (tipo < 0 || tipo >= int(datos_.rompibles.size()) || tipo >= kMaxRompDefs) return nullptr;
  const ps2::Tex*& t = romp_tx_[tipo][roto];
  if (!t && romp_rt_[tipo][roto] > t_) return nullptr;   // auditoria #10: freno de reintento
  if (!t && !roto && datos_.rompibles[size_t(tipo)].id == "roca_cristales") t = Arte("encargo4/roca_rompible.png");   // encargo 4
  if (!t) t = Arte(("tanda4/02_rompibles/rompible_" + datos_.rompibles[size_t(tipo)].id + (roto ? "_roto.png" : "_entero.png")).c_str());
  if (!t) romp_rt_[tipo][roto] = t_ + 0.5f;
  return t;
}

void Modo::Enemigos(ImDrawList* dl, int a, int b) {
  ImTextureID tex = {};
  bool abierta = false;
  for (int q = a; q < b; ++q) {
    const int i = int(claves_[q] & 0xFFFFFFFFu);
    const Sprite& s = spr_[i];
    if (!abierta || s.tex != tex) {
      if (abierta) dl->PopTextureID();
      dl->PushTextureID(s.tex);
      tex = s.tex, abierta = true;
    }
    const bool jugo = Jugo();
    const int g = jugo ? 0 : int(170.0f * std::clamp(dest_[i], 0.0f, 1.0f));   // destello al recibir golpe: tinte rojizo (A02: blanco aditivo, 2 cuadros)
    dl->PrimReserve(6, 4);
    dl->PrimRectUV(s.a, s.b, s.uv0, s.uv1, IM_COL32(255, 255 - g, 255 - g, int(255.0f * alfa_[i])));
    if (jugo && dest_[i] > 0.8f && alfa_[i] > 0.5f) {
      Aditiva(dl, true);
      dl->PrimReserve(6, 4);
      dl->PrimRectUV(s.a, s.b, s.uv0, s.uv1, IM_COL32(255, 255, 255, 230));
      Aditiva(dl, false);
    }
  }
  if (abierta) dl->PopTextureID();
}

void Modo::PrecargaFormas(int& listos, int& total, bool& falla) {
  const auto pedir = [&](const Modelo& m, bool opcional) {
    const int e = pasarela::nave::EstadoFiguraB3(m.b3, m.forma, m.fid);
    listos += e != 1, ++total; // forma opcional ausente: sigue el respaldo de la figura base
    if (!opcional && e == 0) falla = true;
  };
  if (Formas()) for (const auto& p : datos_.personajes) for (int f = 1; f <= 2; ++f) {
    bool hay = false;
    const Modelo m = FormaDe(p.fid, f, &hay);
    if (hay) pedir(m, true);
  }
  // Tambien los mapas elegidos a mano: ninguna fase del jefe debe leer su modelo en combate.
  for (const auto& jefe : datos_.jefes) {
    pedir(ModeloDe(jefe.fid), false);
    pedir(ModeloJefe(jefe, 1, true, REXCVAR_GET(dbz3_survivors_jefes_dos_fases)), false);
    pedir(ModeloJefe(jefe, 2, true, REXCVAR_GET(dbz3_survivors_jefes_dos_fases)), false);
  }
}

bool Modo::PrecargaFx() {
  // Las piezas nativas tienen su propia cache (game_style): calentarlas de una en una,
  // antes del primer Mundo, que antes las pedia todas en el mismo cuadro.
  static std::vector<std::string> nombres;
  static size_t siguiente = 0;
  if (nombres.empty()) {
    for (const char* n : {"fx_impacto", "fx_anillo", "fx_nucleo", "fx_halo", "fx_brillo", "fx_chispas", "fx_golpe", "fx_kibola", "fx_kame_cabeza", "fx_kame_brillo", "fx_kame_raya"}) nombres.emplace_back(n);
    for (const auto& p : datos_.personajes) for (const char* fx : {"/rayo", "/bola"}) nombres.push_back("tkfx/" + p.id + fx);
    for (const auto& j : datos_.jefes) nombres.push_back("tkfx/" + j.id + "/bola");
  }
  if (siguiente < nombres.size()) S::Img(nombres[siguiente++].c_str());
  return siguiente == nombres.size();
}

void Modo::CargarFx() {
  char jb[48];
  std::snprintf(jb, sizeof jb, "tkfx/%s/bola", j_->DJ().id.c_str());
  // 12/11: el rayo y la bola son los DEL LUCHADOR elegido (Galick Ho de Vegeta...); sin textura propia en la tabla (Piccolo), los de Goku
  const std::string yo = j_->personaje >= 0 && j_->personaje < int(datos_.personajes.size()) ? datos_.personajes[size_t(j_->personaje)].id : "goku";
  const S::Pic rayo = S::Img(("tkfx/" + yo + "/rayo").c_str()), bola = S::Img(("tkfx/" + yo + "/bola").c_str());
  fx_ = {S::Img("fx_impacto"), S::Img("fx_anillo"), S::Img("fx_nucleo"), S::Img("fx_halo"), S::Img("fx_brillo"), S::Img("fx_chispas"),
         rayo ? rayo : S::Img("tkfx/goku/rayo"), bola ? bola : S::Img("tkfx/goku/bola"), S::Img("tkfx/vegeta/bola"), S::Img(jb), bola,
         S::Img("fx_golpe"), S::Img("fx_kibola"), S::Img("fx_kame_cabeza"), S::Img("fx_kame_brillo"), S::Img("fx_kame_raya")};
}

ImU32 Modo::ColB3(const PartB3& p, float k, float mult) {
  k = std::clamp(k, 0.0f, 1.0f);
  auto c = [&](int i) { return int(255.0f * std::clamp(p.c0[i] + (p.c1[i] - p.c0[i]) * k, 0.0f, 1.0f)); };
  return IM_COL32(c(0), c(1), c(2), int(float(c(3)) * std::clamp(mult, 0.0f, 1.0f)));
}

float Modo::MedioB3(const PartB3& p, float k, float px) {   // medio lado en px a la edad k (0..1 de su vida)
  return 0.5f * (p.tam + p.azar * 0.5f) * (p.e0 + (p.e1 - p.e0) * std::clamp(k, 0.0f, 1.0f)) * px;
}

void Modo::RayaB3(ImDrawList* dl, const S::Pic& t, const PartB3& p, ImVec2 a, ImVec2 b, float hw, ImU32 col) {
  float nx = -(b.y - a.y), ny = b.x - a.x;
  const float l = std::sqrt(nx * nx + ny * ny);
  if (l < 1e-3f) return;
  nx *= hw / l, ny *= hw / l;
  dl->AddImageQuad(t.tex, ImVec2(a.x + nx, a.y + ny), ImVec2(b.x + nx, b.y + ny), ImVec2(b.x - nx, b.y - ny), ImVec2(a.x - nx, a.y - ny),
                   ImVec2(p.uv[0], p.uv[1]), ImVec2(p.uv[2], p.uv[1]), ImVec2(p.uv[2], p.uv[3]), ImVec2(p.uv[0], p.uv[3]), col);
}

void Modo::GolpeB3(ImDrawList* dl, ImVec2 c, float px, float cuadros, float giro) {
  Aditiva(dl, true);
  for (int k = 0; k < 8; ++k) {
    const PartB3& p = kGolpeB3[k & 3];
    const float u = cuadros / p.vida;
    if (u >= 1.0f) continue;
    const float an = giro + float(k) * 0.785f + 0.21f * float(k >> 2), m = MedioB3(p, u, px);
    const float d0 = m * 0.25f + 11.5f * px * cuadros / 60.0f;   // velocidad del emisor 10 + azar 3 por segundo
    const ImVec2 a(c.x + std::cos(an) * d0, c.y + std::sin(an) * d0), b(c.x + std::cos(an) * (d0 + 2 * m), c.y + std::sin(an) * (d0 + 2 * m));
    RayaB3(dl, fx_.golpe, p, a, b, m * 0.5f, ColB3(p, u));
  }
  Aditiva(dl, false);
}

const S::Pic& Modo::Usa(const S::Pic& p, int bit, const char* nombre) {   // registro: la primera vez que se ve cada textura del juego
  if (p && !(fx_dicho_ & (1u << bit))) {
    fx_dicho_ |= 1u << bit;
    REXLOG_INFO("dbz3 survivors: fx nativo={}", nombre);
  }
  return p;
}

void Modo::Spr(ImDrawList* dl, const S::Pic& p, ImVec2 c, float w, float h, ImU32 col) {
  dl->AddImage(p.tex, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec2(0, 0), ImVec2(1, 1), col);
}

void Modo::Banda(ImDrawList* dl, const S::Pic& p, ImVec2 a, ImVec2 b, float hw, ImU32 col, bool tira, float u0, float u1) {
  float nx = -(b.y - a.y), ny = b.x - a.x;
  const float l = std::sqrt(nx * nx + ny * ny);
  if (l < 1e-3f) return;
  nx *= hw / l, ny *= hw / l;
  const ImVec2 q0(a.x + nx, a.y + ny), q1(b.x + nx, b.y + ny), q2(b.x - nx, b.y - ny), q3(a.x - nx, a.y - ny);
  if (tira) dl->AddImageQuad(p.tex, q0, q1, q2, q3, ImVec2(u0, 0), ImVec2(u1, 0), ImVec2(u1, 1), ImVec2(u0, 1), col);
  else dl->AddImageQuad(p.tex, q0, q1, q2, q3, ImVec2(0.5f, 0), ImVec2(0.5f, 0), ImVec2(0.5f, 1), ImVec2(0.5f, 1), col);
}

bool Modo::SueloNativo(ImDrawList* dl, const Vista& v, const Efecto2D& e) {
  if (!fx_.halo || !fx_.anillo) return false;
  const ImVec2 p = v.P(e.x, e.y);
  const float r = e.radio * v.S, a = std::min(1.0f, e.Vida01() * 4.0f);
  switch (e.forma) {
    case Forma::Aura: {   // resplandor en el suelo + llama ovalada alrededor del cuerpo + chispas que suben
      Usa(fx_.halo, 0, "halo");
      Usa(fx_.anillo, 1, "anillo");
      const float pul = 0.92f + 0.08f * std::sin(ta_ * 9.0f);
      Spr(dl, fx_.halo, p, r * 2.2f, r * 2.2f * v.sp, Rgb(e.color, 0.30f * a));
      Spr(dl, fx_.anillo, p, r * 2.0f * pul, r * 2.0f * pul * v.sp, Rgb(e.color, 0.55f * a));
      const ImVec2 cuerpo(p.x, p.y - v.S * 0.95f);
      const float lat = 0.9f + 0.1f * std::sin(ta_ * 23.0f);
      Spr(dl, fx_.halo, cuerpo, v.S * 1.7f * lat, v.S * 2.7f, Rgb(e.color, 0.42f * a));
      Spr(dl, fx_.halo, cuerpo, v.S * 1.1f, v.S * 2.0f * lat, IM_COL32(255, 255, 255, int(70 * a)));
      if (fx_.nucleo)
        for (int s = 0; s < 6; ++s) {
          const float f = std::fmod(ta_ * 1.6f + float(s) * 0.37f, 1.0f), an = float(s) * 1.047f + ta_ * 0.7f;
          Spr(dl, fx_.nucleo, ImVec2(p.x + std::cos(an) * v.S * 0.55f, p.y - v.S * (0.2f + 1.8f * f)), v.S * 0.22f, v.S * 0.22f,
              Rgb(e.color, (1.0f - f) * 0.9f * a));
        }
      return true;
    }
    case Forma::Anillo: {   // barrera y onda: la onda de choque del juego
      Usa(fx_.anillo, 1, "anillo");
      const float g = std::max(2.0f, e.ancho * v.S);
      {   // POST2 (antes: halo + aro oscuro + aro de color + aro blanco, a ojo): el anillo del #ACE 8 del banco 0, su color y alfa a lo largo de la vida, aditivo; radio = el del juego
        const float u = 1.0f - e.Vida01();
        Aditiva(dl, true);
        Spr(dl, fx_.anillo, p, r * 2.0f + g, (r * 2.0f + g) * v.sp, ColB3(kOndaB3, u, a));
        Aditiva(dl, false);
        return true;
      }
    }
    default: return false;   // cono y avisos del jefe: los de siempre
  }
}

ImVec2 Modo::OrigenRayo(const Vista& v, const Efecto2D& e, float alto) {
  float wx = e.x2 - e.x, wy = e.y2 - e.y;
  const float wl = std::sqrt(wx * wx + wy * wy);
  if (wl > 1e-3f) wx /= wl, wy /= wl;
  const float m = std::min(kManos, wl * 0.5f);
  ImVec2 p = v.P(e.x + wx * m, e.y + wy * m);
  p.y -= alto;
  return p;
}

bool Modo::AireNativo(ImDrawList* dl, const Vista& v, const Efecto2D& e) {
  if (e.forma == Forma::Disco && !e.hostil && e.def < datos_.tecnicas.size() &&
      kienzan::Elegible(REXCVAR_GET(dbz3_survivors_kienzan_malla), REXCVAR_GET(dbz3_survivors_kienzan_texturas),
                        true, false, datos_.tecnicas[e.def].id) && std::isfinite(e.radio) && e.radio > 0) {
    auto p = v.P(e.x, e.y);
    p.y -= v.S * 0.9f;
    if (DibujarKienzan(dl, p, std::max(2.0f, e.radio * v.S), v.sp, v.cp, e.ang)) return true;
  }
  if (!fx_.halo || !fx_.nucleo) return false;
  const float alto = v.S * 0.9f;
  ImVec2 p = v.P(e.x, e.y);
  p.y -= alto;
  const float r = std::max(2.0f, e.radio * v.S), a = std::min(1.0f, e.Vida01() * 4.0f);
  const float W = v.cx * 2, H = v.cy * 2.2f;
  switch (e.forma) {
    case Forma::Bola: {
      if (p.x < -r * 4 || p.y < -r * 4 || p.x > W + r * 4 || p.y > H + r * 4) return true;
      const Arquetipo q = ArqDe(e);
      S::Pic propia;   // bola del jefe (la de su personaje) y Rafaga (la de Vegeta); las demas, con el color de su tecnica
      if (e.hostil) propia = Usa(fx_.jefe_bola, 2, "bola_jefe");
      else if (q == Arquetipo::Rafaga) propia = Usa(fx_.vegeta_bola, 3, "bola_vegeta");
      // 12/11 v4: la bola de ki de B3 es amarilla con estela (la tkfx del luchador es la bola azul del Kamehameha: solo en el rayo)
      constexpr uint32_t kKi = 0xFFE040;
      // 13/11 (A04): la bola recien lanzada, pegada al luchador, se atenua (3-4 bolas a la vez ensuciaban su zona de blanco); a 2 de mundo, entera
      const float cerca = Jugo() && !e.hostil ? std::clamp((std::hypot(e.x - j_->jugador.x, e.y - j_->jugador.y) - 0.5f) / 1.5f, 0.3f, 1.0f) : 1.0f;
      // estela: la bola de unas centesimas antes, mas pequena y transparente
      const float vl = std::sqrt(e.vx * e.vx + e.vy * e.vy);
      if (!propia && fx_.kibola && vl > 0.1f) {   // POST2: la cabeza de la rafaga de ki de B3 (#ACE 9 del banco 0): 4 rayas a lo largo de su velocidad,
        Usa(fx_.kibola, 10, "kibola_b3");   // cada una a 3 edades (las que va soltando el emisor: la estela)
        const float px = r / 7.0f, ux = e.vx / vl, uy = e.vy / vl;   // radio de la rafaga en B3 = 7 u (#CST +0xCC del codigo 350)
        Aditiva(dl, true);
        for (const PartB3& pb : kBolaB3)
          for (int t = 2; t >= 0; --t) {
            const float k = float(t) * 0.5f, m = MedioB3(pb, k, px), atras = k * pb.vida / 60.0f * vl * v.S;
            const ImVec2 cb(p.x - ux * atras, p.y - uy * atras * v.sp);
            RayaB3(dl, fx_.kibola, pb, ImVec2(cb.x - ux * m, cb.y - uy * m * v.sp), ImVec2(cb.x + ux * m, cb.y + uy * m * v.sp), m * 0.5f, ColB3(pb, k, cerca));
          }
        Aditiva(dl, false);
        return true;
      }
      for (int t = 6; t >= 1 && vl > 0.1f; --t) {
        ImVec2 o = v.P(e.x - e.vx * 0.02f * float(t), e.y - e.vy * 0.02f * float(t));
        o.y -= alto;
        const float k = 1.0f - float(t) / 7.0f;
        Spr(dl, fx_.halo, o, r * 2.4f * (0.4f + 0.6f * k), r * 2.4f * (0.4f + 0.6f * k), Rgb(propia ? e.color : kKi, 0.55f * k * cerca));
      }
      if (propia) {   // la bola de energia del juego (ya coloreada, con su nucleo)
        if (e.hostil && e.radio < 0.31f) {   // A06: disparo de un enemigo: la bola del jefe tenida de rojo con aro oscuro (no se confunde con las tuyas)
          dl->AddCircle(p, r * 1.45f, IM_COL32(40, 0, 0, 200), 16, std::max(2.0f, r * 0.35f));
          Spr(dl, propia, p, r * 3.0f, r * 3.0f, IM_COL32(255, 90, 70, 255));
          return true;
        }
        Spr(dl, propia, p, r * 3.6f, r * 3.6f, IM_COL32_WHITE);
        return true;
      }
      if (FxB3()) Aditiva(dl, true);   // A04: resplandor exterior aditivo como B3 (deja ver lo que hay debajo); el cuerpo, normal
      Spr(dl, Usa(fx_.halo, 0, "halo"), p, r * 3.0f, r * 3.0f, Rgb(kKi, 0.55f * cerca));
      if (FxB3()) Aditiva(dl, false);
      Spr(dl, fx_.halo, p, r * 2.0f, r * 2.0f, Rgb(kKi, cerca));
      Spr(dl, Usa(fx_.nucleo, 5, "nucleo"), p, r * 1.3f, r * 1.3f, IM_COL32(255, 255, 220, int(255 * cerca)));
      return true;
    }
    case Forma::Disco: {   // Kienzan: disco plano de energia que gira (ovalo del juego + onda en el filo + destello que da vueltas)
      // Forma::Disco tambien dibuja otros ataques; solo Kienzan y su evolucion usan este interruptor.
      if (!REXCVAR_GET(dbz3_survivors_kienzan_texturas) && !e.hostil && e.def < datos_.tecnicas.size()) {
        const auto& id = datos_.tecnicas[e.def].id;
        if (id == "kienzan" || id == "kienzan_eterno") return false;   // EfectoAire: disco simple
      }
      Usa(fx_.halo, 0, "halo");
      const ImVec2 rr(r * 2.2f, r * 2.2f * v.sp);
      if (FxB3()) Aditiva(dl, true);   // A04: solo el resplandor exterior es aditivo (todo aditivo, el disco se quedaba blanco)
      Spr(dl, fx_.halo, p, rr.x * 1.5f, rr.y * 1.5f, Rgb(e.color, 0.25f));
      if (FxB3()) Aditiva(dl, false);
      Spr(dl, fx_.halo, p, rr.x, rr.y, Rgb(e.color, 0.95f));
      if (fx_.anillo) Spr(dl, Usa(fx_.anillo, 1, "anillo"), p, rr.x * 1.05f, rr.y * 1.05f, IM_COL32(255, 255, 255, 230));
      Spr(dl, fx_.halo, p, rr.x * 0.45f, rr.y * 0.45f, IM_COL32(255, 255, 255, 200));
      for (int s = 0; s < 2; ++s) {
        const float an = e.ang * 3.0f + 3.1416f * float(s);
        Spr(dl, fx_.nucleo, ImVec2(p.x + std::cos(an) * rr.x * 0.42f, p.y + std::sin(an) * rr.y * 0.42f), r * 0.9f, r * 0.9f, IM_COL32(255, 255, 255, 230));
      }
      return true;
    }
    case Forma::Rayo: {
      // nace de las manos (kManos delante del luchador, hacia donde sale) y no de su centro; el origen se pinta ANTES del luchador (pase 1)
      // para que el luchador quede encima, y el cuerpo del rayo antes si se aleja de la camara (sube en pantalla) o despues si se acerca
      p = OrigenRayo(v, e, alto);
      ImVec2 q = v.P(e.x2, e.y2);
      q.y -= alto;
      const auto id_punta=e.id_visual|(1ull<<(e.hostil ? 60 : 62));
      const auto id_cuerpo=id_punta|(1ull<<61);
      const bool cuerpo_real=fxreal::Activo() &&
          std::find(g_fx_reales.begin(),g_fx_reales.end(),id_cuerpo)!=g_fx_reales.end();
      const bool origen = pase_rayo_ == 1, pase_segmento = origen == (!e.hostil || RayoDetras(e));
      const bool cuerpo = pase_segmento && !cuerpo_real;
      const float hw = std::max(3.0f, e.ancho * v.S * 0.5f);
      const bool fxb3 = FxB3();
      const float ak = fxb3 && !e.hostil ? a * g_fxk : a;   // A04: brillo de los resplandores con el tope de saturacion
      const float bola_o = fxb3 ? 0.62f : 1.0f;             // A04: la bola de las manos no tapa al luchador
      const bool kame = e.hostil ? e.color == 0x3D76FF : ArqDe(e) == Arquetipo::Rayo;   // A07: el rayo de Cell es el Kamehameha del juego
      const bool punta = pase_segmento && !(fxreal::Activo() &&
          std::find(g_fx_reales.begin(),g_fx_reales.end(),id_punta)!=g_fx_reales.end());
      constexpr int kSeg = 10;
      if (kame && fx_.kame_rayo) {   // Kamehameha: la tira del rayo del juego, con su resplandor y la bola en las manos y en la punta
        Usa(fx_.kame_rayo, 6, "rayo_luchador");
        const S::Pic& bola = fx_.kame_bola ? Usa(fx_.kame_bola, 7, "bola_luchador") : fx_.halo;
        if (fx_.kame_cabeza && fx_.kame_brillo && fx_.kame_raya) {   // POST2: tira del juego + cabeza del #ACE 2 del BSP 533, todo aditivo
          Usa(fx_.kame_cabeza, 11, "kame_cabeza_b3");
          const float px = hw / 5.0f, cu = std::fmod(ta_ * 60.0f, 12.0f);   // radio del rayo en B3 = 5 u (#CST +0xCC): escala al rayo de aqui; edad ciclica
          auto cabeza = [&](ImVec2 c, float esc) {   // esc: la de las manos, menor con fx_b3 (no tapa al luchador)
            for (int k = 0; k < 2; ++k) {
              const PartB3& pb = kKameCabezaB3[k];
              const float u = std::fmod(cu, pb.vida) / pb.vida, m = MedioB3(pb, u, px) * esc, an = ta_ * (k ? 3.49f : 0.0f) - 1.31f + 0.7f * k;
              const float ca = std::cos(an) * m * 1.41f, sa = std::sin(an) * m * 1.41f;
              dl->AddImageQuad((k ? fx_.kame_brillo : fx_.kame_cabeza).tex, ImVec2(c.x + ca, c.y + sa), ImVec2(c.x - sa, c.y + ca),
                               ImVec2(c.x - ca, c.y - sa), ImVec2(c.x + sa, c.y - ca), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                               ColB3(pb, u, ak));
            }
            const PartB3& pr = kKameCabezaB3[2];
            const float u = std::fmod(cu, pr.vida) / pr.vida, m = MedioB3(pr, u, px) * esc;
            for (int k = 0; k < 4; ++k) {
              const float an = float(k) * 1.571f + ta_ * 2.0f;
              RayaB3(dl, fx_.kame_raya, pr, c, ImVec2(c.x + std::cos(an) * m, c.y + std::sin(an) * m), m * 0.25f, ColB3(pr, u, ak));
            }
          };
          Aditiva(dl, true);
          if (cuerpo) {
            for (int s = 0; s < kSeg; ++s) {
              const float f0 = float(s) / kSeg, f1 = float(s + 1) / kSeg;
              const ImVec2 a0(p.x + (q.x - p.x) * f0, p.y + (q.y - p.y) * f0), a1(p.x + (q.x - p.x) * f1, p.y + (q.y - p.y) * f1);
              Banda(dl, fx_.kame_rayo, a0, a1, hw * 1.7f, IM_COL32(255, 255, 255, int(255 * ak)), true, f0, f1);
            }
          }
          if (punta) cabeza(q, 1.0f);
          if (origen) cabeza(p, fxb3 ? 0.45f : 1.0f);
          Aditiva(dl, false);
          return true;
        }
        if (cuerpo) {
          for (int s = 0; s < kSeg; ++s) {
            const float f0 = float(s) / kSeg, f1 = float(s + 1) / kSeg;
            const float w0 = hw * (1.0f + 0.12f * std::sin(14.0f * f0 - ta_ * 30.0f)), w1 = hw * (1.0f + 0.12f * std::sin(14.0f * f1 - ta_ * 30.0f));
            const ImVec2 a0(p.x + (q.x - p.x) * f0, p.y + (q.y - p.y) * f0), a1(p.x + (q.x - p.x) * f1, p.y + (q.y - p.y) * f1);
            if (fxb3) Aditiva(dl, true);   // A04: resplandor aditivo (con tope), como B3
            Banda(dl, fx_.halo, a0, a1, (w0 + w1) * 2.2f, Rgb(e.color, 0.28f * ak));   // 12/11 v4: resplandor ancho y suave, como B3
            Banda(dl, fx_.halo, a0, a1, (w0 + w1) * 1.4f, Rgb(e.color, 0.45f * ak));
            if (fxb3) Aditiva(dl, false);
            Banda(dl, fx_.kame_rayo, a0, a1, (w0 + w1) * 0.85f, IM_COL32(255, 255, 255, int(235 * a)), true, f0, f1);
          }
        }
        if (punta) {
          Spr(dl, fx_.halo, q, hw * 4.4f, hw * 4.4f, Rgb(e.color, 0.7f * ak));
          Spr(dl, bola, q, hw * 3.2f, hw * 3.2f, IM_COL32(255, 255, 255, int(255 * a)));
        }
        if (origen) {   // la bola de las manos, algo menor que antes (tapaba al luchador con el zoom alto)
          Spr(dl, fx_.halo, p, hw * 4.0f * bola_o, hw * 4.0f * bola_o, Rgb(e.color, 0.6f * ak));
          Spr(dl, bola, p, hw * 2.8f * bola_o, hw * 2.8f * bola_o, IM_COL32(255, 255, 255, int((fxb3 ? 200 : 255) * a)));
        }
        return true;
      }
      // Makankosappo (y cualquier otro rayo): nucleo fino con resplandor (perfil del ovalo del juego) y la espiral que lo envuelve
      Usa(fx_.halo, 0, "halo");
      if (origen) {
        Spr(dl, fx_.halo, p, hw * 4.5f * bola_o, hw * 4.5f * bola_o, Rgb(e.color, 0.8f * ak));
        Spr(dl, fx_.nucleo, p, hw * 2.0f * bola_o, hw * 2.0f * bola_o, IM_COL32(255, 255, 255, int(255 * a)));
      }
      if (!cuerpo) return true;
      if (fxb3) Aditiva(dl, true);   // A04: resplandor aditivo con tope
      const float hz = e.hostil ? 0.5f : 1.0f;   // A07: el resplandor de los rayos del jefe no cubre media pantalla
      Banda(dl, fx_.halo, p, q, hw * 4.0f * hz, Rgb(e.color, 0.25f * ak));
      Banda(dl, fx_.halo, p, q, hw * 2.2f * hz, Rgb(e.color, 0.60f * ak));
      if (fxb3) Aditiva(dl, false);
      Banda(dl, fx_.halo, p, q, hw * 0.9f, IM_COL32(255, 255, 255, int(230 * a)));
      if (ArqDe(e) == Arquetipo::RayoFino) {
        Usa(fx_.nucleo, 5, "nucleo");
        const float dx = q.x - p.x, dy = q.y - p.y, l = std::max(1.0f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l, ny = dx / l;
        const int n = std::clamp(int(l / (hw * 1.6f)), 6, 48);
        for (int s = 0; s <= n; ++s) {   // helice vista de lado: dos hebras desfasadas
          const float f = float(s) / float(n), fase = f * l / (hw * 5.0f) * 6.2832f - ta_ * 20.0f;
          for (int h = 0; h < 2; ++h) {
            const float o = std::sin(fase + 3.1416f * float(h)) * hw * 2.2f, delante = std::cos(fase + 3.1416f * float(h));
            Spr(dl, fx_.nucleo, ImVec2(p.x + dx * f + nx * o, p.y + dy * f + ny * o), hw * 1.3f, hw * 1.3f,
                delante > 0 ? IM_COL32(255, 255, 255, int(230 * a)) : Rgb(e.color, 0.6f * a));
          }
        }
      }
      return true;
    }
    case Forma::Cono: {   // golpe fisico: el cono del suelo (primitivas) y, en el aire, el estallido de golpe del juego delante del puno
      if (!fx_.impacto) return false;
      const bool mio = Jugo() && !e.hostil;   // 13/11 (A05): el estallido de golpe del luchador sale mas lejos de sus manos y algo mas pequeno (el aro dorado junto a ellas)
      const float dd = mio ? 0.85f : 0.55f;
      ImVec2 c = v.P(e.x + std::cos(e.ang) * e.radio * dd, e.y + std::sin(e.ang) * e.radio * dd);
      c.y -= alto;
      if (fx_.golpe) {   // POST2: el #ACE 1 del banco 0 tal cual (rayas, color, escala, cuadros y mezcla aditiva)
        Usa(fx_.golpe, 9, "golpe_b3");
        GolpeB3(dl, c, v.S * kUnidadB3 * (mio ? 0.7f : 1.0f), (e.vida0 - e.vida) * 60.0f, e.ang);
        return true;
      }
      const float f = 1.0f - e.Vida01(), s = v.S * e.radio * (0.7f + 0.5f * f);
      // 12/11 v4: en B3 el golpe son rayas radiales amarillas con un destello: estallido tenido de amarillo, chispas amarillas y un
      // destello blanco solo en el primer tercio (antes: estrella blanca + esfera blanca)
      const int al = int(255 * e.Vida01());
      Spr(dl, Usa(fx_.impacto, 8, "impacto"), c, s * 0.8f, s * 0.8f, IM_COL32(255, 200, 30, al));
      for (int k = 0; k < 8; ++k) {   // rayas radiales: el ovalo suave del juego estirado desde el centro hacia fuera
        const float an = e.ang + float(k) * 0.785f + 0.2f * float(k & 1), l0 = s * (0.15f + 0.25f * f), l1 = s * (0.55f + 0.35f * f) * (k & 1 ? 0.7f : 1.0f);
        Banda(dl, fx_.halo, ImVec2(c.x + std::cos(an) * l0, c.y + std::sin(an) * l0), ImVec2(c.x + std::cos(an) * l1, c.y + std::sin(an) * l1),
              s * 0.09f, IM_COL32(255, 215, 40, al));
      }
      if (f < 0.35f) Spr(dl, fx_.halo, c, s * 0.7f * (1.0f - f), s * 0.7f * (1.0f - f), IM_COL32(255, 255, 230, int(255 * (1.0f - f / 0.35f))));
      return true;
    }
    case Forma::Explosion: {   // el estallido del juego creciendo, el destello y la onda en el suelo (como el impacto de Tactics)
      if (!fx_.impacto) return false;
      Usa(fx_.impacto, 8, "impacto");
      const float f = 1.0f - e.Vida01(), rr = r * (0.35f + 0.65f * std::sqrt(f)), vida = e.Vida01();
      const ImVec2 c = v.P(e.x, e.y);
      if (fx_.anillo) {
        Spr(dl, fx_.anillo, c, rr * 2.4f, rr * 2.4f * v.sp, Rgb(e.color, 0.9f * vida));
        Spr(dl, fx_.anillo, c, rr * 2.1f, rr * 2.1f * v.sp, IM_COL32(255, 225, 110, int(150 * vida)));   // 12/11 v4: sin aro blanco (pompa)
      }
      const ImVec2 cc(c.x, c.y - rr * 0.45f);
      Spr(dl, fx_.impacto, cc, rr * 2.6f, rr * 2.6f, IM_COL32(255, 205, 50, int(240 * vida)));   // 12/11 v4: estallido amarillo como B3
      Spr(dl, fx_.nucleo, cc, rr * 1.2f * (0.4f + vida), rr * 1.2f * (0.4f + vida), IM_COL32(255, 255, 230, int(240 * vida)));
      return true;
    }
    default: return false;
  }
}

void Modo::Chispas(ImDrawList* dl, const Vista& v) {
  const S::Pic& c = fx_.impacto ? fx_.impacto : fx_.nucleo;   // 12/11 v4: fx_chispas se ve como un disco blanco (pompa); el estallido no
  if (!c) return;
  const int n = std::min(n_chispas_, kChispas);
  for (int i = 0; i < n; ++i) {
    const Chispa& s = chispas_[i];
    const float e = (ta_ - s.t0) / 0.18f;
    if (e < 0 || e >= 1) continue;
    ImVec2 p = v.P(s.x, s.y);
    p.y -= v.S * 0.9f;
    if (fx_.golpe) {
      Usa(fx_.golpe, 9, "golpe_b3");
      GolpeB3(dl, p, v.S * kUnidadB3 * 0.55f, (ta_ - s.t0) * 60.0f, float(i % 8) * 0.785f);
      continue;
    }
    Spr(dl, Usa(c, 8, fx_.impacto ? "impacto" : "nucleo"), p, v.S * 1.1f * (0.7f + 0.5f * e), v.S * 1.1f * (0.7f + 0.5f * e), IM_COL32(255, 230, 90, int(230 * (1 - e))));
  }
}

void Modo::EfectoSuelo(ImDrawList* dl, const Vista& v, const Efecto2D& e) {
  if (SueloNativo(dl, v, e)) return;
  const ImVec2 p = v.P(e.x, e.y);
  const float r = e.radio * v.S, a = std::min(1.0f, e.Vida01() * 4.0f);
  switch (e.forma) {
    case Forma::Aura: {
      const float pul = 0.92f + 0.08f * std::sin(t_ * 8.0f);
      dl->AddEllipseFilled(p, ImVec2(r, r * v.sp), Rgb(e.color, 0.16f * a), 0, 40);
      dl->AddEllipseFilled(p, ImVec2(r * 0.6f, r * 0.6f * v.sp), Rgb(e.color, 0.12f * a), 0, 32);
      dl->AddEllipse(p, ImVec2(r * pul, r * pul * v.sp), Rgb(e.color, 0.6f * a), 0, 40, 3.0f * k_);
      break;
    }
    case Forma::Anillo: {
      const float g = std::max(2.0f, e.ancho * v.S);
      dl->AddEllipseFilled(p, ImVec2(r, r * v.sp), Rgb(e.color, 0.08f * a), 0, 48);
      dl->AddEllipse(p, ImVec2(r, r * v.sp), Rgb(e.color, 0.45f * a), 0, 48, g * 1.6f);
      dl->AddEllipse(p, ImVec2(r, r * v.sp), Rgb(e.color, 0.9f * a), 0, 48, g * 0.7f);
      dl->AddEllipse(p, ImVec2(r, r * v.sp), IM_COL32(255, 255, 255, int(200 * a)), 0, 48, std::max(1.5f, g * 0.25f));
      break;
    }
    case Forma::Cono: {
      if (fx_.impacto) break;   // con el juego: solo su estallido delante del puno (AireNativo), sin abanico dibujado
      constexpr int kSeg = 12;
      for (int capa = 0; capa < 2; ++capa) {
        const float rr = e.radio * (capa ? 0.6f : 1.0f);
        dl->PathLineTo(p);
        for (int s = 0; s <= kSeg; ++s) {
          const float an = e.ang - e.apertura + 2.0f * e.apertura * float(s) / kSeg;
          dl->PathLineTo(v.P(e.x + std::cos(an) * rr, e.y + std::sin(an) * rr));
        }
        dl->PathFillConvex(capa ? IM_COL32(255, 255, 255, int(70 * a)) : Rgb(e.color, 0.4f * a));
      }
      break;
    }
    case Forma::Explosion: {   // tanda 3: grieta en el suelo bajo la explosion (la hoja avanza con la vida del efecto)
      if (const ps2::Tex* g = fx_.anillo ? nullptr : T3(kHoja0 + kGrieta)) {   // con el juego, su onda (AireNativo)
        const DefHoja& h = kHojas[kGrieta];
        const int c = std::min(h.cuadros - 1, int((1.0f - e.Vida01()) * float(h.cuadros)));
        DibujarCuadro(dl, g, c, h.cuadros, ImVec2(h.ax, h.ay), p, e.radio * 2.6f * v.S / float(g->h), IM_COL32(255, 255, 255, int(255 * std::min(1.0f, e.Vida01() * 3.0f))));
      }
      break;
    }
    case Forma::AvisoLinea: {   // franja de carga (tanda 3) repetida a lo largo de la linea; se enciende hasta el ataque; diana al final
      if (const ps2::Tex* fr = T3(kFranja)) {
        const float f = 1.0f - e.Vida01(), pul = 0.5f + 0.5f * std::sin(t_ * 18.0f);
        FranjaSuelo(dl, v, fr, e.x, e.y, e.x2, e.y2, e.ancho, 1.0f, IM_COL32(255, 255, 255, int(60 + 40 * pul)));   // R23 SURV-7: menos opaca
        FranjaSuelo(dl, v, fr, e.x, e.y, e.x2, e.y2, e.ancho, f, IM_COL32(255, 255, 255, 150));
        if (T3(kDiana)) Pieza3(dl, kDiana, ImVec2(128, 128), v.P(e.x2, e.y2), e.ancho * 1.8f * v.S / 248.0f * (0.92f + 0.08f * pul));   // ya pintada a 55 grados
        break;
      }
      const ImVec2 q = v.P(e.x2, e.y2);
      const float hw = e.ancho * v.S * 0.5f, f = 1.0f - e.Vida01(), pul = 0.5f + 0.5f * std::sin(t_ * 18.0f);
      Franja(dl, p, q, hw, IM_COL32(255, 30, 20, int(50 + 40 * pul)));
      Franja(dl, p, ImVec2(p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f), hw, IM_COL32(255, 40, 20, 90));
      Franja(dl, p, q, std::max(1.5f, 2.0f * k_), IM_COL32(255, 220, 200, int(120 + 100 * pul)));
      break;
    }
    case Forma::AvisoAnillo: {
      const float f = 1.0f - e.Vida01(), pul = 0.5f + 0.5f * std::sin(t_ * 18.0f);
      if (fx_.halo) {   // SUR-04: anillo fino: cuentas del brillo del juego (fx_halo, aditivo) a lo largo del borde + linea fina; otro se cierra desde dentro
        Usa(fx_.halo, 0, "halo");
        const int n = std::clamp(int(r / std::max(1.0f, 4.0f * k_)), 24, 96);
        const float g = std::max(6.0f, 0.55f * v.S);
        Aditiva(dl, true);
        for (int k = 0; k < n; ++k) {
          const float an = 6.2831853f * float(k) / float(n) + t_ * 0.6f;
          Spr(dl, fx_.halo, ImVec2(p.x + std::cos(an) * r, p.y + std::sin(an) * r * v.sp), g, g, Rgb(e.color, 0.45f + 0.25f * pul));
          if (f > 0.05f) Spr(dl, fx_.halo, ImVec2(p.x + std::cos(an) * r * f, p.y + std::sin(an) * r * f * v.sp), g * 0.7f, g * 0.7f, Rgb(e.color, 0.35f * f));
        }
        Aditiva(dl, false);
        dl->AddEllipse(p, ImVec2(r, r * v.sp), Rgb(e.color, 0.8f), 0, 64, std::max(1.5f, 2.0f * k_));
        break;
      }
      if (const ps2::Tex* an = T3(kAnillo)) {   // tanda 3: el octavo (lienzo entero, pivote 128,128) girado 8 veces en el suelo y proyectado
        const float s = r * 128.0f / 123.0f;    // borde exterior del anillo (123 px al 50 % de alfa) = radio del aviso
        for (int k = 0; k < 8; ++k) DibujarGirada(dl, an, p, ImVec2(s, s), float(k) * 0.7853982f, v.sp, IM_COL32(255, 255, 255, int(170 + 85 * pul)));
        if (f > 0.05f)   // se llena hasta el ataque: el mismo anillo cerrandose desde dentro
          for (int k = 0; k < 8; ++k) DibujarGirada(dl, an, p, ImVec2(s * f, s * f), float(k) * 0.7853982f, v.sp, IM_COL32(255, 255, 255, int(120 * f)));
        break;
      }
      dl->AddEllipseFilled(p, ImVec2(r * f, r * f * v.sp), IM_COL32(255, 40, 20, 70), 0, 40);
      dl->AddEllipse(p, ImVec2(r, r * v.sp), IM_COL32(255, 40, 20, int(140 + 100 * pul)), 0, 48, 4.0f * k_);
      break;
    }
    // tanda 4
    case Forma::AvisoZona: {   // zona de la lluvia del jefe: la pieza repetida en el plano (tesela de 2,5 de mundo) y proyectada
      const float pul = 0.5f + 0.5f * std::sin(t_ * 10.0f), ent = std::min(1.0f, (e.vida0 - e.vida) / 0.2f);
      if (const ps2::Tex* z = T3(kZona))
        TeselaSuelo(dl, v, z, e.x - e.x2, e.y - e.y2, e.x + e.x2, e.y + e.y2, 2.5f, IM_COL32(255, 255, 255, int((150 + 70 * pul) * ent)));
      else
        dl->AddQuadFilled(v.P(e.x - e.x2, e.y - e.y2), v.P(e.x + e.x2, e.y - e.y2), v.P(e.x + e.x2, e.y + e.y2), v.P(e.x - e.x2, e.y + e.y2), IM_COL32(255, 40, 20, int(60 * ent)));
      break;
    }
    case Forma::Sombra: {   // sombra del proyectil: crece de un cuadro a otro hasta el impacto (6 cuadros, tinta final de ~194 px = el radio)
      const float f = 1.0f - e.Vida01();
      if (e.radio > 3.0f) {   // A07: esfera grande (Spirit Bomb, Supernova): el radio exacto del impacto, un aro que se llena
        dl->AddEllipseFilled(p, ImVec2(r * f, r * f * v.sp), Rgb(e.color, 0.10f + 0.14f * f), 0, 48);
        dl->AddEllipse(p, ImVec2(r, r * v.sp), IM_COL32(255, 70, 40, int(120 + 120 * f)), 0, 48, std::max(2.0f, 3.0f * k_));
        break;
      }
      if (const ps2::Tex* sh = T3(kSombra))
        DibujarCuadro(dl, sh, std::min(5, int(f * 6.0f)), 6, ImVec2(128, 128), p, e.radio * 2.0f * v.S / 194.0f, IM_COL32(255, 255, 255, 235));
      else
        dl->AddEllipseFilled(p, ImVec2(r * f, r * f * v.sp), IM_COL32(0, 0, 0, 120), 0, 24);
      dl->AddEllipse(p, ImVec2(r, r * v.sp), IM_COL32(255, 60, 40, int(60 + 80 * f)), 0, 32, std::max(1.5f, 2.0f * k_));   // donde dara
      break;
    }
    case Forma::AvisoRayo: {   // marca del rayo (4 cuadros a 8 fps en bucle, ancla 128,154) y el circulo que se llena hasta que cae
      const float f = 1.0f - e.Vida01();
      dl->AddEllipseFilled(p, ImVec2(r * f, r * f * v.sp), IM_COL32(150, 210, 255, int(50 + 50 * f)), 0, 32);
      if (const ps2::Tex* mk = T3(kMarcaRayo))
        DibujarCuadro(dl, mk, int(e.t * 8.0f) % 4, 4, ImVec2(128, 154), p, e.radio * 2.0f * v.S / 200.0f);
      else
        dl->AddEllipse(p, ImVec2(r, r * v.sp), IM_COL32(150, 210, 255, 220), 0, 32, 3.0f * k_);
      break;
    }
    default: break;
  }
}

void Modo::TeselaSuelo(ImDrawList* dl, const Vista& v, const ps2::Tex* t, float x0, float y0, float x1, float y1, float tesela, ImU32 col) {
  if (!t || tesela <= 0) return;
  for (float y = y0; y < y1; y += tesela)
    for (float x = x0; x < x1; x += tesela) {
      const float xb = std::min(x1, x + tesela), yb = std::min(y1, y + tesela), u = (xb - x) / tesela, w = (yb - y) / tesela;
      dl->AddImageQuad(t->id(), v.P(x, yb), v.P(xb, yb), v.P(xb, y), v.P(x, y), ImVec2(0, 1 - w), ImVec2(u, 1 - w), ImVec2(u, 1), ImVec2(0, 1), col);
    }
}

void Modo::EfectoAire(ImDrawList* dl, const Vista& v, const Efecto2D& e) {
  if (e.forma == Forma::Sombra && e.radio > 3.0f && fx_.halo) {   // A07: Spirit Bomb / Supernova: esfera de energia que crece sobre el jugador y cae al final
    const float f = e.Vida01(), caida = f > 0.35f ? 0.0f : 1.0f - f / 0.35f, pul = 0.94f + 0.06f * std::sin(t_ * 12.0f);
    ImVec2 q = v.P(e.x, e.y);
    q.y -= v.S * (1.0f + 3.0f * (1.0f - caida * caida));
    const float R = v.S * std::sqrt(e.radio / 5.5f) * (1.0f + 0.9f * (1.0f - f)) * pul;   // crece mientras carga
    Aditiva(dl, true);   // resplandor exterior aditivo; el cuerpo en alfa normal (en aditivo se pierde sobre el suelo claro, nieve o arena)
    Spr(dl, fx_.halo, q, R * 3.4f, R * 3.4f, Rgb(e.color, 0.30f));
    Aditiva(dl, false);
    dl->AddCircleFilled(q, R * 1.05f, Rgb(e.color, 0.96f), 40);   // el cuerpo de la esfera, solido (el halo suelto se veia gris sobre el suelo claro)
    dl->AddCircleFilled(q, R * 0.7f, Rgb(0xFFD070, 0.95f), 32);
    Spr(dl, fx_.halo, q, R * 2.2f, R * 2.2f, Rgb(e.color, 0.55f));
    if (fx_.anillo) Spr(dl, fx_.anillo, q, R * 2.5f, R * 2.5f, Rgb(0xFFF0C0, 0.8f));
    if (fx_.nucleo) Spr(dl, fx_.nucleo, q, R * 1.4f, R * 1.4f, IM_COL32(255, 255, 235, 255));
    return;
  }
  if (e.forma == Forma::Sombra) {   // tanda 4: el proyectil de la lluvia cae sobre su sombra en la segunda mitad del aviso
    const float f = e.Vida01();
    if (f > 0.55f) return;
    ImVec2 q = v.P(e.x, e.y);
    q.y -= v.S * 9.0f * f / 0.55f;
    const float r = v.S * e.radio * 0.55f;
    if (fx_.jefe_bola) Spr(dl, fx_.jefe_bola, q, r * 2.2f, r * 2.2f, IM_COL32_WHITE);
    else dl->AddCircleFilled(q, r, IM_COL32(255, 90, 40, 230), 16), dl->AddCircleFilled(q, r * 0.5f, IM_COL32(255, 230, 200, 255), 12);
    return;
  }
  if (e.forma == Forma::RayoCae) {   // tanda 4: el rayo (6 cuadros, ancla 128,450 en el suelo) avanza con la vida del efecto
    if (const ps2::Tex* ry = T3(kRayo))
      DibujarCuadro(dl, ry, std::min(5, int((1.0f - e.Vida01()) * 6.0f)), 6, ImVec2(128, 450), v.P(e.x, e.y), e.radio * 2.4f * v.S / 200.0f);
    return;
  }
  if (AireNativo(dl, v, e)) return;
  const float alto = v.S * 0.9f;   // a media altura de una figura
  ImVec2 p = v.P(e.x, e.y);
  p.y -= alto;
  const float r = std::max(2.0f, e.radio * v.S), a = std::min(1.0f, e.Vida01() * 4.0f);
  switch (e.forma) {
    case Forma::Bola: {
      if (p.x < -r * 2 || p.y < -r * 2 || p.x > v.cx * 2 + r * 2 || p.y > v.cy * 2.2f + r * 2) return;
      dl->AddCircleFilled(p, r * 2.0f, Rgb(e.color, 0.13f), 16);
      dl->AddCircleFilled(p, r * 1.4f, Rgb(e.color, 0.3f), 16);
      dl->AddCircleFilled(p, r, Rgb(e.color, 0.95f), 16);
      dl->AddCircleFilled(ImVec2(p.x - r * 0.15f, p.y - r * 0.15f), r * 0.55f, IM_COL32(255, 255, 255, 235), 12);
      break;
    }
    case Forma::Disco: {
      const ImVec2 rr(r, r * v.sp);
      dl->AddEllipseFilled(p, ImVec2(rr.x * 1.5f, rr.y * 1.5f), Rgb(e.color, 0.15f), 0, 24);
      dl->AddEllipseFilled(p, rr, Rgb(e.color, 0.75f), 0, 24);
      dl->AddEllipse(p, rr, IM_COL32(255, 255, 255, 230), 0, 24, std::max(1.5f, r * 0.18f));
      for (int s = 0; s < 3; ++s) {   // dientes que giran (el disco rueda)
        const float an = e.ang + 2.094f * float(s);
        dl->AddLine(ImVec2(p.x + std::cos(an) * r * 0.25f, p.y + std::sin(an) * r * 0.25f * v.sp), ImVec2(p.x + std::cos(an) * r * 0.9f, p.y + std::sin(an) * r * 0.9f * v.sp),
                    IM_COL32(255, 255, 255, 160), std::max(1.0f, r * 0.1f));
      }
      break;
    }
    case Forma::Rayo: {   // sin texturas del juego: mismo reparto que AireNativo (origen en las manos, antes del luchador)
      p = OrigenRayo(v, e, alto);
      ImVec2 q = v.P(e.x2, e.y2);
      q.y -= alto;
      const float hw = std::max(2.0f, e.ancho * v.S * 0.5f);
      if ((pase_rayo_ == 1) == (!e.hostil || RayoDetras(e))) {
        Franja(dl, p, q, hw * 1.6f, Rgb(e.color, 0.22f * a));
        Franja(dl, p, q, hw, Rgb(e.color, 0.85f * a));
        Franja(dl, p, q, hw * 0.38f, IM_COL32(255, 255, 255, int(240 * a)));
        dl->AddCircleFilled(q, hw, Rgb(e.color, 0.6f * a), 16);
      }
      if (pase_rayo_ == 1) {
        dl->AddCircleFilled(p, hw * 1.5f, Rgb(e.color, 0.8f * a), 16);
        dl->AddCircleFilled(p, hw * 0.8f, IM_COL32(255, 255, 255, int(240 * a)), 16);
      }
      break;
    }
    case Forma::Explosion: {
      const float f = 1.0f - e.Vida01(), rr = r * (0.35f + 0.65f * std::sqrt(f)), vida = e.Vida01();
      const ImVec2 c = v.P(e.x, e.y);
      dl->AddEllipseFilled(c, ImVec2(rr, rr * v.sp), Rgb(e.color, 0.25f * vida), 0, 32);
      dl->AddCircleFilled(ImVec2(c.x, c.y - rr * 0.35f), rr * 0.8f, Rgb(e.color, 0.55f * vida), 24);
      dl->AddCircleFilled(ImVec2(c.x, c.y - rr * 0.35f), rr * 0.45f * (0.4f + vida), IM_COL32(255, 255, 230, int(230 * vida)), 20);
      dl->AddEllipse(c, ImVec2(rr * 1.1f, rr * 1.1f * v.sp), IM_COL32(255, 255, 255, int(160 * vida)), 0, 32, std::max(1.5f, 3.0f * k_));
      break;
    }
    default: break;
  }
}

void Modo::FranjaSuelo(ImDrawList* dl, const Vista& v, const ps2::Tex* t, float x0, float y0, float x1, float y1, float ancho, float hasta, ImU32 col) {
  const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy) * hasta;
  if (len < 1e-3f || !t) return;
  const float ux = dx / (len / hasta), uy = dy / (len / hasta), hw = ancho * 0.5f * 64.0f / 56.0f, tramo = hw * 2.0f * 256.0f / 64.0f;
  const float nx = -uy * hw, ny = ux * hw;
  for (float s = 0; s < len; s += tramo) {
    const float e = std::min(len, s + tramo), u1 = (e - s) / tramo;
    const float ax = x0 + ux * s, ay = y0 + uy * s, bx = x0 + ux * e, by = y0 + uy * e;
    dl->AddImageQuad(t->id(), v.P(ax + nx, ay + ny), v.P(bx + nx, by + ny), v.P(bx - nx, by - ny), v.P(ax - nx, ay - ny), ImVec2(0, 0), ImVec2(u1, 0),
                     ImVec2(u1, 1), ImVec2(0, 1), col);
  }
}

void Modo::Lanzar(uint8_t hoja, float x, float y, float alto, float mult, bool sigue, bool real) {
  vis_[n_vis_++ % kVis] = {x, y, alto, real ? t_ : ta_, mult, hoja, sigue, real};
  if (n_vis_ >= 2 * kVis) n_vis_ -= kVis;   // sigue dando la vuelta sin desbordar
}

void Modo::Hojas(ImDrawList* dl, const Vista& v, bool suelo, float W, float H) {
  const int n = std::min(n_vis_, kVis);
  for (int i = 0; i < n; ++i) {
    const Vis& f = vis_[i];
    if(fxreal::Activo() && std::find(g_fx_reales.begin(),g_fx_reales.end(),IdHojaReal(i,f.t0))!=g_fx_reales.end())continue;
    const DefHoja& h = kHojas[f.hoja];
    if (h.suelo != suelo) continue;
    const float t = (f.real ? t_ : ta_) - f.t0;
    const int c = int(t * h.fps);
    if (t < 0 || c >= h.cuadros) continue;
    if (HojaNativa(dl, v, f, t / (float(h.cuadros) / h.fps), h.ancho * f.mult * v.S, W, H)) continue;
    const bool abierto = f.hoja == kCofreAbiertoFx;
    const ps2::Tex* tx = T3(abierto ? kCofreAbierto : kHoja0 + f.hoja);
    if (!tx || tx->h <= 0) continue;
    ImVec2 p = f.sigue ? v.P(j_->jugador.x, j_->jugador.y) : v.P(f.x, f.y);
    p.y -= f.alto * v.S;
    const float esc = h.ancho * f.mult * v.S / float(tx->h), r = float(tx->h) * esc;
    if (p.x < -r || p.x > W + r || p.y < -r || p.y > H + r) continue;
    const float a = abierto ? 1.0f - std::clamp((t - 0.8f) / 0.45f, 0.0f, 1.0f) : 1.0f;
    DibujarCuadro(dl, tx, c, h.cuadros, ImVec2(h.ax, h.ay), p, esc, IM_COL32(255, 255, 255, int(255 * a)));
  }
}

bool Modo::HojaNativa(ImDrawList* dl, const Vista& v, const auto& f, float u, float lado, float W, float H) {   // f: Vis (declarado despues)
  if (!fx_.impacto || !fx_.anillo || !fx_.nucleo) return false;
  const float a = 1.0f - u;
  ImVec2 p = f.sigue ? v.P(j_->jugador.x, j_->jugador.y) : v.P(f.x, f.y);
  if (p.x < -lado || p.x > W + lado || p.y < -lado || p.y > H + lado) return true;
  switch (f.hoja) {
    case kImpacto: {   // el estallido de golpe y sus chispas (como Cono en AireNativo)
      p.y -= f.alto * v.S;
      if (fx_.golpe) {   // POST2: el golpe exacto de B3 (#ACE 1 del banco 0); u = 0..1 de la vida de la hoja -> 8 cuadros
        Usa(fx_.golpe, 9, "golpe_b3");
        GolpeB3(dl, p, v.S * kUnidadB3, u * 8.0f, float(int(f.x * 7.0f + f.y * 3.0f) % 8) * 0.1f);
        return true;
      }
      const float s = lado * (0.55f + 0.45f * u);
      // 12/11 v4: B3 = rayas radiales amarillas + destello (antes estallido blanco + fx_chispas, que se ve como una pompa blanca)
      const int al = int(255 * a);
      Spr(dl, Usa(fx_.impacto, 8, "impacto"), p, s * 0.8f, s * 0.8f, IM_COL32(255, 200, 30, al));
      if (fx_.halo) {
        for (int k = 0; k < 8; ++k) {
          const float an = float(k) * 0.785f + 0.2f * float(k & 1), l0 = s * (0.15f + 0.25f * u), l1 = s * (0.55f + 0.35f * u) * (k & 1 ? 0.7f : 1.0f);
          Banda(dl, fx_.halo, ImVec2(p.x + std::cos(an) * l0, p.y + std::sin(an) * l0), ImVec2(p.x + std::cos(an) * l1, p.y + std::sin(an) * l1),
                s * 0.09f, IM_COL32(255, 215, 40, al));
        }
        if (u < 0.35f) Spr(dl, fx_.halo, p, s * 0.6f * (1.0f - u), s * 0.6f * (1.0f - u), IM_COL32(255, 255, 230, int(255 * (1.0f - u / 0.35f))));
      }
      return true;
    }
    case kExplosion: {   // onda en el suelo, estallido y nucleo (como Forma::Explosion)
      const float rr = lado * 0.4f * (0.35f + 0.65f * std::sqrt(u));
      Spr(dl, Usa(fx_.anillo, 1, "anillo"), p, rr * 2.4f, rr * 2.4f * v.sp, IM_COL32(255, 220, 160, int(230 * a)));
      const ImVec2 c(p.x, p.y - rr * 0.45f);
      Spr(dl, fx_.impacto, c, rr * 2.6f, rr * 2.6f, IM_COL32(255, 230, 190, int(240 * a)));
      Spr(dl, fx_.nucleo, c, rr * 1.2f * (0.4f + a), rr * 1.2f * (0.4f + a), IM_COL32(255, 255, 230, int(240 * a)));
      return true;
    }
    case kGrieta: {   // la onda de choque del juego en el suelo
      const float rr = lado * 0.45f * (0.4f + 0.6f * u);
      Spr(dl, Usa(fx_.anillo, 1, "anillo"), p, rr * 2.0f, rr * 2.0f * v.sp, IM_COL32(255, 255, 255, int(200 * a)));
      return true;
    }
    case kHumo:
    case kPolvo: return true;
    default: return false;   // gema, subida de nivel y cofre: piezas de interfaz, no efectos del combate
  }
}

void Modo::Cifras(ImDrawList* dl, const Vista& v) {
  const bool jugo = Jugo();
  for (const Cifra& c : cifras_) {
    const float t = ta_ - c.t0;
    if (c.valor <= 0 || t < 0 || t >= 0.75f) continue;
    if (jugo) {   // A03: tamano segun el dano, critico (golpe fuerte) mas grande y naranja con rebote, sufijo K/M, sombra
      ImVec2 q = v.P(c.x, c.y);
      q.y -= v.S * (1.6f + 0.9f * Suave(t / 0.75f));
      const float a = t < 0.45f ? 1.0f : 1.0f - (t - 0.45f) / 0.3f;
      const float escala = std::min(0.46f, (0.28f + 0.07f * std::log10(1.0f + float(c.valor) / 5.0f)) * (c.fuerte ? 1.18f : 1.0f));   // 13/11: con tope
      const float pop = t < 0.12f ? 1.0f + 0.35f * (1.0f - t / 0.12f) : 1.0f;
      const float alto = v.S * escala * pop;
      const ImU32 col = c.fuerte ? IM_COL32(255, 150, 30, int(255 * a)) : IM_COL32(255, 255, 255, int(255 * a));
      const ImU32 sombra = IM_COL32(20, 10, 0, int(200 * a));
      if (c.valor < 1000) {
        const float d = std::max(1.5f, alto * 0.06f);
        DibujarCifras(dl, c.fuerte, c.valor, ImVec2(q.x + d, q.y + d), alto, sombra);
        DibujarCifras(dl, c.fuerte, c.valor, q, alto, col);
      } else {   // 1,2K / 3,4M: el tipo del menu no trae K ni M; texto de la fuente del juego con borde
        char buf[16];
        if (c.valor < 1000000) std::snprintf(buf, sizeof buf, "%.1fK", float(c.valor) / 1000.0f);
        else std::snprintf(buf, sizeof buf, "%.1fM", float(c.valor) / 1000000.0f);
        ImFont* f = ImGui::GetFont();
        const float fs = alto * 1.15f;
        const ImVec2 tam = f->CalcTextSizeA(fs, 1e9f, 0.0f, buf), o(q.x - tam.x * 0.5f, q.y - alto * 1.0f);
        const float d = std::max(1.5f, fs * 0.06f);
        for (const ImVec2 off : {ImVec2(-d, 0), ImVec2(d, 0), ImVec2(0, -d), ImVec2(0, d), ImVec2(d, d)}) dl->AddText(f, fs, ImVec2(o.x + off.x, o.y + off.y), sombra, buf);
        dl->AddText(f, fs, o, col, buf);
      }
      continue;
    }
    ImVec2 p = v.P(c.x, c.y);
    p.y -= v.S * (1.6f + 0.9f * Suave(t / 0.75f));
    const float a = t < 0.45f ? 1.0f : 1.0f - (t - 0.45f) / 0.3f;
    const float alto = v.S * (c.fuerte ? 0.55f : 0.40f) * (t < 0.08f ? 0.7f + 0.3f * t / 0.08f : 1.0f);
    DibujarCifras(dl, c.fuerte, c.valor, p, alto, IM_COL32(255, 255, 255, int(255 * a)));
  }
}

void Modo::Cifra_(float x, float y, float dano) {
  if (dano < 1.0f || cifras_paso_ >= 4) return;   // como mucho 4 nuevas por paso y kCifras a la vez
  if (Jugo()) {   // A03: los golpes seguidos sobre el mismo sitio se apilan en una sola cifra (la suma, con el rebote otra vez)
    for (Cifra& c : cifras_)
      if (c.valor > 0 && ta_ - c.t0 < 0.45f && std::fabs(c.x - x) < 1.8f && std::fabs(c.y - y) < 1.8f) {   // 13/11: radio y ventana mayores (no repite "89 89")
        c.valor += int(dano + 0.5f), c.fuerte = c.fuerte || c.valor >= int(Juego::kGolpeFuerte), c.t0 = ta_ - 0.02f;
        return;
      }
  }
  for (Cifra& c : cifras_)
    if (c.valor <= 0 || ta_ - c.t0 >= 0.75f) {
      c = {x + 0.25f * float((n_vis_ + cifras_paso_) % 3 - 1), y, ta_, int(dano + 0.5f), dano >= 30.0f};
      ++cifras_paso_;
      return;
    }
}

const ps2::Tex* Modo::DecoTex(int k) {   // al primer uso; la que falte se reintenta cada 2 s
  const DefDecorado& g = Deco();
  const std::string* ruta = nullptr;
  if (k < kDecoMancha) ruta = k < int(g.suelos.size()) ? &g.suelos[size_t(k)] : nullptr;
  else if (k < kDecoAdorno) ruta = k - kDecoMancha < int(g.manchas.size()) ? &g.manchas[size_t(k - kDecoMancha)] : nullptr;
  else if (k < kDecoPiezas) ruta = k - kDecoAdorno < int(g.adornos.size()) ? &g.adornos[size_t(k - kDecoAdorno)] : nullptr;
  if (!ruta) return nullptr;
  if (!deco_tx_[k] && deco_rt_[k] <= t_) {
    deco_tx_[k] = Arte(ruta->c_str());
    if (!deco_tx_[k]) deco_rt_[k] = t_ + 2.0f;   // auditoria #10: reintenta cada 2 s, no cada cuadro
  }
  return deco_tx_[k];
}

void Modo::SueloZona(ImDrawList* dl, const Vista& v, int zi, int zj, const ps2::Tex* t) {
  const int var = Variante(zi, zj);
  const bool l = Variante(zi - 1, zj) != var, r = Variante(zi + 1, zj) != var, b = Variante(zi, zj - 1) != var, a = Variante(zi, zj + 1) != var;
  static constexpr float kF[5] = {0, 0.25f, 0.5f, 0.75f, 1};
  const auto alfa = [&](int ix, int iy) {   // 0 en el borde con una zona distinta, 1 a un cuarto de zona
    const float ax = (ix == 0 && l) || (ix == 4 && r) ? 0.0f : 1.0f, ay = (iy == 0 && b) || (iy == 4 && a) ? 0.0f : 1.0f;
    return IM_COL32(255, 255, 255, int(255 * std::min(ax, ay)));
  };
  dl->PushTextureID(t->id());
  dl->PrimReserve(16 * 6, 16 * 4);
  for (int iy = 0; iy < 4; ++iy)
    for (int ix = 0; ix < 4; ++ix) {   // la textura entera cubre 2 x 2 subcuadros (12 x 12 de mundo); v = 0 arriba (+y)
      const float u0 = (ix & 1) ? 0.5f : 0.0f, vb = (iy & 1) ? 0.5f : 1.0f;
      const float x0 = (float(zi) + kF[ix]) * kZona, x1 = (float(zi) + kF[ix + 1]) * kZona;
      const float y0 = (float(zj) + kF[iy]) * kZona, y1 = (float(zj) + kF[iy + 1]) * kZona;
      const ImDrawIdx k = ImDrawIdx(dl->_VtxCurrentIdx);
      dl->PrimWriteIdx(k), dl->PrimWriteIdx(ImDrawIdx(k + 1)), dl->PrimWriteIdx(ImDrawIdx(k + 2));
      dl->PrimWriteIdx(k), dl->PrimWriteIdx(ImDrawIdx(k + 2)), dl->PrimWriteIdx(ImDrawIdx(k + 3));
      dl->PrimWriteVtx(v.P(x0, y0), ImVec2(u0, vb), alfa(ix, iy));
      dl->PrimWriteVtx(v.P(x1, y0), ImVec2(u0 + 0.5f, vb), alfa(ix + 1, iy));
      dl->PrimWriteVtx(v.P(x1, y1), ImVec2(u0 + 0.5f, vb - 0.5f), alfa(ix + 1, iy + 1));
      dl->PrimWriteVtx(v.P(x0, y1), ImVec2(u0, vb - 0.5f), alfa(ix, iy + 1));
    }
  dl->PopTextureID();
}

void Modo::Mancha(ImDrawList* dl, const Vista& v, float x, float y, uint32_t h, float ancho) {
  // reparto, opacidad y tamano de cada mancha del toml (llanura: 4 tierra : 3 hierba seca : 2 charco : 1 ceniza; la ceniza, muy oscura,
  // al 65 % del tamano y al 55 % de opacidad: salia como un agujero negro)
  const DefDecorado& g = Deco();
  const int n = std::min(int(g.manchas.size()), kDecoAdorno - kDecoMancha);
  if (n <= 0) return;
  float tot = 0;
  for (int k = 0; k < n; ++k) tot += g.reparto[size_t(k)];
  float r = float(h % 1000u) / 1000.0f * tot;
  int cual = 0;
  while (cual < n - 1 && r >= g.reparto[size_t(cual)]) r -= g.reparto[size_t(cual++)];
  const ps2::Tex* t = DecoTex(kDecoMancha + cual);
  if (!t) return;
  const float s = ancho * v.S * 0.5f * g.manchas_tam[size_t(cual)], ang = float((h >> 8) % 360u) * 0.0174533f;
  DibujarGirada(dl, t, v.P(x, y), ImVec2(s, s), ang, v.sp, IM_COL32(255, 255, 255, int(255.0f * std::clamp(g.manchas_alfa[size_t(cual)], 0.0f, 1.0f))));   // en el plano del suelo
}

int Modo::Decorado(ImDrawList* dl, const Vista& v, float W, float H, int m) {
  const float hx = W * 0.5f / v.S, y_arriba = v.jy + v.cy / (v.S * v.sp), y_abajo = v.jy - (H - v.cy) / (v.S * v.sp);
  const float x0 = v.jx - hx, x1 = v.jx + hx;
  // 1) zonas de suelo
  const int zi0 = int(std::floor(x0 / kZona)), zi1 = int(std::floor(x1 / kZona)), zj0 = int(std::floor(y_abajo / kZona)), zj1 = int(std::floor(y_arriba / kZona));
  for (int zj = zj0; zj <= zj1; ++zj)
    for (int zi = zi0; zi <= zi1; ++zi)
      if (const int var = Variante(zi, zj))
        if (const ps2::Tex* t = DecoTex(var - 1)) SueloZona(dl, v, zi, zj, t);
  // 2) manchas: en el centro de cada borde entre zonas distintas (y corridas por el borde) y sueltas por celdas
  for (int zj = zj0 - 1; zj <= zj1 + 1; ++zj)
    for (int zi = zi0 - 1; zi <= zi1 + 1; ++zi) {
      const int var = Variante(zi, zj);
      const uint32_t h = Mezcla(zi, zj, 23);
      if (Variante(zi + 1, zj) != var)   // borde derecho (vertical)
        Mancha(dl, v, float(zi + 1) * kZona, (float(zj) + 0.3f + 0.4f * float(h & 255u) / 255.0f) * kZona, h, 10.0f + float((h >> 16) & 3u));
      if (Variante(zi, zj + 1) != var)   // borde de arriba (horizontal)
        Mancha(dl, v, (float(zi) + 0.3f + 0.4f * float((h >> 4) & 255u) / 255.0f) * kZona, float(zj + 1) * kZona, h >> 3, 10.0f + float((h >> 18) & 3u));
    }
  for (int cj = int(std::floor(y_abajo / kCeldaMancha)) - 1; cj <= int(std::floor(y_arriba / kCeldaMancha)) + 1; ++cj)
    for (int ci = int(std::floor(x0 / kCeldaMancha)) - 1; ci <= int(std::floor(x1 / kCeldaMancha)) + 1; ++ci) {
      const uint32_t h = Mezcla(ci, cj, 37);
      if (h % 1000u < uint32_t(Deco().densidad_manchas * 1000.0f))
        Mancha(dl, v, (float(ci) + float((h >> 10) & 255u) / 255.0f) * kCeldaMancha, (float(cj) + float((h >> 18) & 255u) / 255.0f) * kCeldaMancha, h >> 5,
               6.0f + 3.0f * float((h >> 26) & 15u) / 15.0f);
    }
  // 3) adornos por celda (el picado los alarga hacia arriba: se miran 4 de mundo mas por abajo)
  const DefDecorado& g = Deco();
  const int nad = std::min(int(g.adornos.size()), kDecoPiezas - kDecoAdorno);
  const uint32_t umbral = uint32_t(g.densidad_adornos * 1000.0f);
  int puestos = 0;
  if (nad <= 0) return 0;
  for (int cj = int(std::floor((y_abajo - 4.0f) / kCeldaAdorno)); cj <= int(std::floor(y_arriba / kCeldaAdorno)); ++cj)
    for (int ci = int(std::floor((x0 - 2.0f) / kCeldaAdorno)); ci <= int(std::floor((x1 + 2.0f) / kCeldaAdorno)); ++ci) {
      const uint32_t h = Mezcla(ci, cj, 53);
      if (h % 1000u >= umbral) continue;
      const int tipo = int((h >> 10) % uint32_t(nad));
      const ps2::Tex* t = DecoTex(kDecoAdorno + tipo);
      if (!t || t->w <= 0) continue;
      const float x = (float(ci) + 0.15f + 0.7f * float((h >> 14) & 255u) / 255.0f) * kCeldaAdorno;
      const float y = (float(cj) + 0.15f + 0.7f * float((h >> 22) & 255u) / 255.0f) * kCeldaAdorno;
      const float esc = 3.0f * (0.85f + 0.3f * float((h >> 4) & 63u) / 63.0f) * v.S / float(t->w);
      const bool espejo = (h >> 30) & 1u;
      const ImVec2 pies = v.P(x, y), ancla(128.0f, g.anclas[size_t(tipo)]);
      if (pies.x + 128 * esc < 0 || pies.x - 128 * esc > W || pies.y - ancla.y * esc > H || pies.y + (256 - ancla.y) * esc < 0) continue;
      if (std::find(g.planos.begin(), g.planos.end(), tipo) != g.planos.end()) {   // plano (crater, grieta, losa, duna): con el suelo, debajo de todo
        DibujarCuadro(dl, t, 0, 1, ancla, pies, esc, IM_COL32_WHITE, espejo);
        continue;
      }
      if (puestos >= 128 || m + puestos >= kSpr) continue;   // el hueco que kSpr les guarda
      Sprite& s = spr_[m + puestos];
      s.tex = t->id(), s.a = ImVec2(pies.x - 128 * esc, pies.y - ancla.y * esc), s.b = ImVec2(s.a.x + 256 * esc, s.a.y + 256 * esc);
      s.uv0 = ImVec2(espejo ? 1.0f : 0.0f, 0), s.uv1 = ImVec2(espejo ? 0.0f : 1.0f, 1);
      dest_[m + puestos] = 0, alfa_[m + puestos] = 1;
      claves_[m + puestos] = (uint64_t(Bits(pies.y)) << 32) | uint32_t(m + puestos);
      ++puestos;
    }
  return puestos;
}

bool Modo::Flecha(ImDrawList* dl, int pieza, ImVec2 p, float W, float H, float semi, float alto, ImVec2* donde) {   // true = dibujada
  if (p.x + semi >= 0 && p.x - semi <= W && p.y >= 0 && p.y - alto <= H) return false;   // en pantalla, aunque sea en parte
  const bool barra_jefe = j_->jefe.estado != EstadoJefe::Ausente && j_->jefe.estado != EstadoJefe::Muerto;
  const float mx = 70 * k_, arriba = (barra_jefe ? 275 : 190) * k_, abajo = 150 * k_;   // la flecha mide 88 px a 1080 (semi 44)
  const ps2::Tex* t = T3(pieza);
  if (!t) return false;
  const ImVec2 c(W * 0.5f, H * 0.55f);
  const float dx = p.x - c.x, dy = p.y - c.y;
  float f = 1.0f;
  if (std::fabs(dx) > 1e-3f) f = std::min(f, ((dx > 0 ? W - mx : mx) - c.x) / dx);
  if (std::fabs(dy) > 1e-3f) f = std::min(f, ((dy > 0 ? H - abajo : arriba) - c.y) / dy);
  const float s = 44 * k_ * (1.0f + 0.08f * std::sin(t_ * 6.0f));
  ImVec2 q(c.x + dx * f, c.y + dy * f);
  if (q.x > W - 300 * k_ && q.y < 430 * k_) q.y = 430 * k_;   // tanda 4: no encima del minimapa ni de los contadores de debajo
  DibujarGirada(dl, t, q, ImVec2(s, s), std::atan2(dx, -dy), 1.0f);
  if (donde) *donde = q;
  return true;
}

}  // namespace dbz3::survivors::interno
