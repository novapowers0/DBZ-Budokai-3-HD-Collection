// Dragon Budokai Survivors: pantallas del modo (carga, eleccion, mapa, tienda, marcador, subida, pausa, resultados, avisos, minimapa). Parte de modo.cpp (P0).
#include "modo_interno.h"
#include "medidas.h"
#include "pantallas_cierre.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

#include "../modos.h"   // M::L: texto en es / en / fr / de / it (pt y ja: i18n_modos.toml)

// P2: ajustes del menu propio y de la meta-progresion (apagados de serie; ver pantallas_p2.h)
REXCVAR_DEFINE_BOOL(dbz3_survivors_menu_v2, true, "DBZ3/Mods", "Dragon Budokai Survivors: portada y menu propio, seleccion de luchador v2 y clasificacion mundial")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_meta_v2, true, "DBZ3/Mods", "Dragon Budokai Survivors: subida de nivel con rareza, cofre, resultados, tienda, coleccion y logros v2")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(dbz3_survivors_cierre_b3, true, "DBZ3/Mods", "Dragon Budokai Survivors: derrota y resultados con la interfaz de B3")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace dbz3::survivors::interno {

namespace M = dbz3::modos;

// ======================================================================================================================================
// P2 (pantallas, menu y meta-progresion). Todo lo de abajo va detras de dbz3_survivors_menu_v2 / dbz3_survivors_meta_v2 (apagados de serie).
// ======================================================================================================================================
namespace {
constexpr uint16_t kX = 0x4000, kLB = 0x0100, kRB = 0x0200;   // XInput: X, LB, RB (kA, kB, kY y la cruceta vienen de modo_interno.h)
constexpr uint8_t kPantPortada = 7;
const char* const kEstaciones[7] = {"jugar", "tienda", "coleccion", "logros", "clasificacion", "comojugar", "opciones"};

// DBZ3_TEST_SURVIVORS_MENU: "1" = recorrido automatico de la portada; coleccion | logros | ranking | comojugar | opciones = abre esa estacion
const char* PruebaMenu() {
  static const std::string v = [] {
    const char* e = std::getenv("DBZ3_TEST_SURVIVORS_MENU");
    return std::string(e ? e : "");
  }();
  return v.empty() ? nullptr : v.c_str();
}
bool Recorrido() { return PruebaMenu() && !std::strcmp(PruebaMenu(), "1"); }
bool RankingLocal() {
  const char* e = std::getenv("DBZ3_TEST_SURVIVORS_RANKING");
  return e && !std::strcmp(e, "local");
}

// Arte de la tanda 5 (GPT): res/survivors/tanda5/<archivo>; si no esta, nullptr y la pantalla usa el recurso provisional
const ps2::Tex* Arte5(const char* nombre) {
  static std::vector<std::pair<std::string, bool>> hay;
  for (const auto& h : hay)
    if (h.first == nombre) return h.second ? Arte((std::string("tanda5/") + nombre).c_str()) : nullptr;
  std::error_code ec;
  const bool si = std::filesystem::exists(RutaRecursos() / "tanda5" / nombre, ec);
  hay.push_back({nombre, si});
  return si ? Arte((std::string("tanda5/") + nombre).c_str()) : nullptr;
}

// Arte opcional de SUR-13: no cambia las rutas compartidas con Rogue.
const ps2::Tex* ArteComercial(const char* grupo, const char* id) {
  const std::string clave(id);
  if (clave.empty() || clave.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) return nullptr;
  return Arte5((std::string("comercial/") + grupo + "/" + clave + ".png").c_str());
}

void IconoComercial(ImDrawList* dl, const char* id, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE) {
  const ps2::Tex* t = ArteComercial("iconos", id);
  if (!t) t = Icono(id);
  DibujarAjustada(dl, t, a, b, col);
}

std::vector<Logro>& ListaLogros() {
  static std::vector<Logro> v;
  static bool leido = false;
  if (!leido) {
    leido = true;
    std::string av;
    LeerLogros(RutaRecursos() / "logros.toml", &v, &av);
    if (!av.empty()) REXLOG_WARN("dbz3 survivors: logros.toml: {}", av);
    REXLOG_INFO("dbz3 survivors: logros leidos {}", v.size());
  }
  return v;
}
const char* LogroTxt(const Logro& l, bool desc) { return TextoLogro(l, desc, T::LangCode()).c_str(); }
// R23 SURV-7: la condicion de un candado, sin el punto final (los jueces lo marcaron en dos rondas)
std::string CondTxt(const Logro& l) {
  std::string t = LogroTxt(l, true);
  while (!t.empty() && (t.back() == '.' || t.back() == ' ')) t.pop_back();
  return t;
}

ImU32 Lerp32(ImU32 a, ImU32 b, float t) {
  const auto c = [&](int s) { return int(float((a >> s) & 255) * (1 - t) + float((b >> s) & 255) * t); };
  return IM_COL32(c(IM_COL32_R_SHIFT), c(IM_COL32_G_SHIFT), c(IM_COL32_B_SHIFT), c(IM_COL32_A_SHIFT));
}
const char* RarezaTxt(Rareza r) {
  switch (r) {
    case Rareza::PocoComun: return M::L("Poco común", "Uncommon", "Peu commun", "Ungewöhnlich", "Non comune");
    case Rareza::Rara: return M::L("Rara", "Rare", "Rare", "Selten", "Rara");
    case Rareza::Epica: return M::L("Épica", "Epic", "Épique", "Episch", "Epica");
    case Rareza::Legendaria: return M::L("Legendaria", "Legendary", "Légendaire", "Legendär", "Leggendaria");
    default: return M::L("Común", "Common", "Commun", "Gewöhnlich", "Comune");
  }
}
const char* RarezaArchivo(Rareza r) {
  static const char* const k[kRarezas] = {"tarjeta_comun.png", "tarjeta_poco_comun.png", "tarjeta_rara.png", "tarjeta_epica.png", "tarjeta_legendaria.png"};
  return k[int(r)];
}

// R23 SUR-5: cara del luchador de B3 (marcador del juego, `caras/<b3>`; si no, el retrato de dialogo recortado) cuadrada en a..b.
// col = IM_COL32_WHITE normal; con el negro de alfa 255 sale la silueta (candado). Sin arte devuelve false.
bool CaraB3(ImDrawList* dl, int b3, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE, bool grande = false) {
  // grande (R23 SURV-6): el retrato de dialogo (320 px) antes que la cara del marcador (baja resolucion; borrosa a mas de ~150 px)
  // caras_rec = la cara del marcador sin su margen transparente (native_ui): sin el, salia estrecha y a la izquierda
  const std::string n = std::to_string(b3);
  S::Pic p = S::Img(((grande ? "retratos/" : "caras_rec/") + n).c_str());
  if (!p) p = S::Img(((grande ? "caras_rec/" : "retratos/") + n).c_str());
  if (!p) p = S::Img(("caras/" + n).c_str());
  if (!p || p.w <= 0 || p.h <= 0) return false;
  // cubre el cuadro sin deformar: recorta por UV lo que sobra del lado largo (centrado en x, arriba en y: la cara manda)
  const float bw = b.x - a.x, bh = b.y - a.y, s = std::max(bw / p.w, bh / p.h);
  const float uw = bw / (p.w * s), vh = bh / (p.h * s), u0 = (1 - uw) * 0.5f, v0 = (1 - vh) * 0.25f;
  dl->AddImage(p.tex, a, b, ImVec2(u0, v0), ImVec2(u0 + uw, v0 + vh), col);
  return true;
}
void SiluetaBloqueada(ImDrawList* dl, const DefPersonaje& p, int b3, ImVec2 a, ImVec2 b, bool grande) {
  if (const ps2::Tex* t = ArteComercial("siluetas", p.id.c_str()))
    DibujarAjustada(dl, t, a, b);
  else
    CaraB3(dl, b3, a, b, IM_COL32(0, 0, 0, 255), grande);
}
// Marco dorado fino de una cara (jefes, finales)
void MarcoCara(ImDrawList* dl, ImVec2 a, ImVec2 b, float k) {
  dl->AddRect(ImVec2(a.x - 2 * k, a.y - 2 * k), ImVec2(b.x + 2 * k, b.y + 2 * k), IM_COL32(20, 12, 6, 255), 6 * k, 0, 5 * k);
  dl->AddRect(a, b, IM_COL32(255, 205, 70, 255), 4 * k, 0, 3 * k);
}

// Cinta de rareza de w x h centrada en c: la pieza de la tanda 5 teñida, o una cinta con cola de golondrina dibujada
void Cinta(ImDrawList* dl, ImVec2 c, float w, float h, ImU32 col, const char* txt, float fs) {
  if (const ps2::Tex* t = Arte5("cinta_rareza.png")) {
    DibujarImagen(dl, t, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), col);
  } else {
    const float x0 = c.x - w * 0.5f, x1 = c.x + w * 0.5f, y0 = c.y - h * 0.5f, y1 = c.y + h * 0.5f, n = h * 0.35f;
    const ImVec2 p[6] = {{x0, y0}, {x1, y0}, {x1 - n, c.y}, {x1, y1}, {x0, y1}, {x0 + n, c.y}};
    dl->AddConvexPolyFilled(p, 6, col);
    dl->AddPolyline(p, 6, IM_COL32(0, 0, 0, 120), ImDrawFlags_Closed, 1.5f);
  }
  TxtC(dl, fs, c, IM_COL32(255, 255, 255, 255), txt);
}

// "antes -> despues" de una opcion de la subida (tecnica: dano; pasiva: su bonus)
void LineaCambio(const Datos& d, const Juego& j, const Opcion& o, char* out, size_t n) {
  out[0] = 0;
  if (o.tipo == TipoOpcion::Curar || o.def < 0) return;
  const int nv = std::clamp(o.nivel, 1, kNivelMax);
  if (o.tipo == TipoOpcion::Pasiva) {
    if (size_t(o.def) >= d.pasivas.size()) return;
    const DefPasiva& p = d.pasivas[size_t(o.def)];
    const int antes = j.NivelPasiva(o.def);
    const float a = p.por_nivel * float(antes), b = p.por_nivel * float(nv);
    if (antes == 0) {   // nueva: solo el valor que da
      if (p.efecto == Efecto::VidaMax) std::snprintf(out, n, "+%.0f", double(b));
      else if (p.efecto == Efecto::Regen) std::snprintf(out, n, "+%.1f/s", double(b));
      else std::snprintf(out, n, "+%.0f%%", double(b * 100));
    } else if (p.efecto == Efecto::VidaMax) std::snprintf(out, n, "+%.0f -> +%.0f", double(a), double(b));
    else if (p.efecto == Efecto::Regen) std::snprintf(out, n, "+%.1f/s -> +%.1f/s", double(a), double(b));
    else std::snprintf(out, n, "+%.0f%% -> +%.0f%%", double(a * 100), double(b * 100));
  } else {
    if (size_t(o.def) >= d.tecnicas.size()) return;
    const DefTecnica& t = d.tecnicas[size_t(o.def)];
    const int antes = j.NivelTecnica(o.def);
    if (antes > 0) std::snprintf(out, n, "%s %.0f -> %.0f", M::L("Daño", "Damage", "Dégâts", "Schaden", "Danno"), double(t.dano[antes - 1]), double(t.dano[nv - 1]));
    else std::snprintf(out, n, "%s %.0f", M::L("Daño", "Damage", "Dégâts", "Schaden", "Danno"), double(t.dano[nv - 1]));
  }
}
int NivelAntes(const Juego& j, const Opcion& o) {
  return o.tipo == TipoOpcion::Pasiva ? j.NivelPasiva(o.def) : o.tipo == TipoOpcion::Curar ? 0 : j.NivelTecnica(o.def);
}

// Circulo con la letra de un boton del mando (X, Y, LB) a la izquierda de un rotulo
void Boton(ImDrawList* dl, ImVec2 c, float r, const char* letra, ImU32 col) {
  dl->AddCircleFilled(c, r, col, 24);
  dl->AddCircle(c, r, IM_COL32(0, 0, 0, 160), 24, 2.0f);
  TxtC(dl, r * (std::strlen(letra) > 1 ? 0.9f : 1.2f), c, IM_COL32_WHITE, letra);
}

// A14 "compartir": la ventana del juego (zona cliente, tal como se ve) a un BMP de 24 bits en <datos de usuario>/survivors_capturas. Solo Windows.
bool CapturarVentana(std::filesystem::path* ruta) {
#if defined(_WIN32)
  struct Busca {
    DWORD pid;
    HWND h;
    long area;
  } busca{GetCurrentProcessId(), nullptr, 0};
  EnumWindows(
      [](HWND h, LPARAM l) -> BOOL {   // la ventana visible mas grande de este proceso = la del juego
        auto* b = reinterpret_cast<Busca*>(l);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        RECT r{};
        if (pid == b->pid && IsWindowVisible(h) && GetClientRect(h, &r) && long(r.right) * r.bottom > b->area) b->h = h, b->area = long(r.right) * r.bottom;
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&busca));
  HWND h = busca.h;
  RECT rc{};
  if (!h || !GetClientRect(h, &rc) || rc.right <= 8 || rc.bottom <= 8) return false;
  const int w = rc.right, hh = rc.bottom;
  HDC pantalla = GetDC(nullptr), mem = CreateCompatibleDC(pantalla);
  HBITMAP bmp = CreateCompatibleBitmap(pantalla, w, hh);
  HGDIOBJ viejo = SelectObject(mem, bmp);
  const bool ok_blt = PrintWindow(h, mem, 0x2 /*PW_RENDERFULLCONTENT*/) != 0;   // el contenido de la ventana aunque otra este delante
  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof bi, bi.biWidth = w, bi.biHeight = -hh, bi.biPlanes = 1, bi.biBitCount = 24, bi.biCompression = BI_RGB;
  const int fila = (w * 3 + 3) & ~3;
  std::vector<uint8_t> px(size_t(fila) * size_t(hh));
  const bool ok_bits = ok_blt && GetDIBits(mem, bmp, 0, UINT(hh), px.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS) == hh;
  SelectObject(mem, viejo), DeleteObject(bmp), DeleteDC(mem), ReleaseDC(nullptr, pantalla);
  if (!ok_bits) return false;
  std::error_code ec;
  const std::filesystem::path dir = dbz3::settings::UserDataRoot() / "survivors_capturas";
  std::filesystem::create_directories(dir, ec);
  std::time_t t = std::time(nullptr);
  std::tm g{};
  localtime_s(&g, &t);
  char nombre[64];
  std::snprintf(nombre, sizeof nombre, "survivors_%04d%02d%02d_%02d%02d%02d.bmp", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
  *ruta = dir / nombre;
  std::ofstream f(*ruta, std::ios::binary | std::ios::trunc);
  BITMAPFILEHEADER bf{};
  bf.bfType = 0x4D42, bf.bfOffBits = sizeof bf + sizeof bi, bf.bfSize = DWORD(bf.bfOffBits + px.size());
  f.write(reinterpret_cast<const char*>(&bf), sizeof bf), f.write(reinterpret_cast<const char*>(&bi), sizeof bi);
  f.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
  return bool(f);
#else
  (void)ruta;
  return false;
#endif
}

// Contexto de las funciones de dibujo que no son miembros de Modo (no pueden tocar su parte privada)
struct Ctx {
  ImDrawList *fondo, *dl;
  float W, H, k, t, t_opc;
  const Datos& d;
  const Juego& j;
  EstadoP2& p;
  const Progreso& prog;
  int op_sel;
};

// ---------------------------------------------------------------- A10: subida de nivel v2
void SubidaV2(const Ctx& c) {
  const Juego& j = c.j;
  ImDrawList* dl = c.dl;
  const float k = c.k, W = c.W, H = c.H, en = Suave(c.t_opc / 0.25f);
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 10, int(220 * en)));
  if (const ps2::Tex* r = Arte5("subida_fondo_rayos.png")) DibujarImagen(dl, r, ImVec2(0, 0), ImVec2(W, H), IM_COL32(255, 255, 255, int(255 * en)));
  char buf[96];   // R23 SURV-8: titulo y nivel en una linea, mas abajo: antes tapaba el reloj de arriba
  std::snprintf(buf, sizeof buf, "%s  %s %d", M::L("¡Subes de nivel!", "Level up!", "Niveau supérieur !", "Stufenaufstieg!", "Livello superiore!"), M::L("Nivel", "Level", "Niveau", "Stufe", "Livello"), j.jugador.nivel);
  S::MenuTextB3(dl, ImVec2(W * 0.5f, 186 * k - (1 - en) * 40 * k), 54 * k, buf, true, false, 1100 * k);
  // zeni de la partida (con ellos se re-sortea)
  {
    std::snprintf(buf, sizeof buf, "%d", j.zeni);
    const float fs = 36 * k, w = Medir(fs, buf).x;
    Txt(dl, fs, ImVec2(W - 40 * k - w, 50 * k), IM_COL32(255, 230, 120, 255), buf);
    if (const ps2::Tex* z = Arte(kArte3[kObjeto0 + int(TipoObjeto::Zeni)])) DibujarCuadro(dl, z, 0, 1, ImVec2(64, 64), ImVec2(W - 40 * k - w - fs * 0.75f, 50 * k + fs * 0.5f), fs * 1.3f / 128.0f);
  }
  const int n = j.nOpciones;
  const float h = 540 * k, w = h * 400.0f / 560.0f, hueco = 40 * k, x0 = (W - (float(n) * w + float(n - 1) * hueco)) * 0.5f, y0 = 215 * k;
  for (int i = 0; i < n; ++i) {
    const Opcion& o = j.opciones[i];
    const float a = Suave((c.t_opc - 0.08f * float(i)) / 0.3f);
    if (a <= 0) continue;
    const bool el = i == c.op_sel, evo = o.tipo == TipoOpcion::Evolucion;
    const float g = el ? 1.04f + 0.01f * std::sin(c.t * 4.0f) : 1.0f;
    const ImVec2 ce(x0 + float(i) * (w + hueco) + w * 0.5f, y0 + h * 0.5f + (1 - a) * 140 * k);
    const ImVec2 p0(ce.x - w * 0.5f * g, ce.y - h * 0.5f * g), p1(ce.x + w * 0.5f * g, ce.y + h * 0.5f * g);
    const float cw = p1.x - p0.x, ch = p1.y - p0.y;
    const int al = int(255 * a);   // la entrada aparece de golpe y se queda al 100 %: ninguna carta queda semitransparente
    const uint32_t rgb = evo ? 0xFF6A2A : ColorRareza(o.rareza);
    const ImU32 col = Rgb(rgb, 1.0f);
    if (el) dl->AddRectFilled(ImVec2(p0.x - 10 * k, p0.y - 10 * k), ImVec2(p1.x + 10 * k, p1.y + 10 * k), Rgb(rgb, 0.45f * a), 22 * k);
    dl->AddRectFilled(ImVec2(p0.x + 8 * k, p0.y + 8 * k), ImVec2(p1.x - 8 * k, p1.y - 8 * k), IM_COL32(16, 38, 50, al), 14 * k);   // base opaca: la carta se lee siempre, sea cual sea el arte
    const ps2::Tex* propia = Arte5(evo ? "tarjeta_evolucion.png" : RarezaArchivo(o.rareza));
    const ImU32 tinte = el ? IM_COL32(255, 255, 255, al) : IM_COL32(205, 210, 225, al);   // la no elegida, algo mas apagada (opaca)
    if (propia) {
      DibujarImagen(dl, propia, p0, p1, tinte);
    } else {
      const ImU32 base = Lerp32(IM_COL32(255, 255, 255, 255), IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255), 0.6f);
      DibujarImagen(dl, Ui(el ? "ui_tarjeta_resaltada" : "ui_tarjeta_normal"), p0, p1, (Lerp32(tinte, base, 0.7f) & 0x00FFFFFFu) | (uint32_t(al) << IM_COL32_A_SHIFT));
    }
    // borde: color de rareza (vivo si es la elegida, apagado si no)
    dl->AddRect(ImVec2(p0.x + 6 * k, p0.y + 6 * k), ImVec2(p1.x - 6 * k, p1.y - 6 * k), Rgb(rgb, el ? 0.95f * a : 0.45f * a), 16 * k, 0, (el ? 5.0f : 3.0f) * k);
    // icono (zona cuadrada de la tarjeta)
    const float ic = ch * 0.27f;
    const ImVec2 ci(ce.x, p0.y + ch * 0.215f);
    IconoComercial(dl, j.Icono(o), ImVec2(ci.x - ic * 0.5f, ci.y - ic * 0.5f), ImVec2(ci.x + ic * 0.5f, ci.y + ic * 0.5f), IM_COL32(255, 255, 255, al));
    // cinta de rareza
    Cinta(dl, ImVec2(ce.x, p0.y + ch * 0.415f), cw * 0.78f, 44 * k, Rgb(rgb, 0.95f * a), evo ? M::L("¡Evolución!", "Evolution!", "Évolution !", "Entwicklung!", "Evoluzione!") : RarezaTxt(o.rareza), 30 * k);
    const Textos& tx = j.Texto(o);
    S::MenuTextB3(dl, ImVec2(ce.x, p0.y + ch * 0.505f), 34 * k, Es(tx), el, false, cw * 0.82f);
    const int antes = NivelAntes(j, o);
    if (o.tipo != TipoOpcion::Curar) {
      if (antes == 0) std::snprintf(buf, sizeof buf, "%s  %s %d", M::L("¡Nueva!", "New!", "Nouveau !", "Neu!", "Nuova!"), M::L("Nv.", "Lv.", "Niv.", "St.", "Liv."), o.nivel);
      else std::snprintf(buf, sizeof buf, "%s %d -> %d", M::L("Nv.", "Lv.", "Niv.", "St.", "Liv."), antes, o.nivel);
      TxtC(dl, 28 * k, ImVec2(ce.x, p0.y + ch * 0.575f), antes == 0 ? IM_COL32(120, 255, 160, al) : IM_COL32(255, 230, 140, al), buf);
      char ln[64];
      LineaCambio(c.d, j, o, ln, sizeof ln);
      if (ln[0]) TxtC(dl, 27 * k, ImVec2(ce.x, p0.y + ch * 0.63f), IM_COL32(150, 215, 255, al), ln);
    }
    Txt(dl, 27 * k, ImVec2(p0.x + cw * 0.09f, p0.y + ch * 0.685f), IM_COL32(235, 238, 250, al), Desc(tx), cw * 0.82f);
  }
  // botones: re-sortear, saltar, desterrar (con su coste); gris si no se puede
  const float by = y0 + h + 40 * k, bw = 430 * k, bh = 78 * k, bh2 = 14 * k;
  const bool ok_re = j.PuedeResortear(), ok_ds = j.DesterrosRestantes() > 0 && c.op_sel < j.nOpciones && j.opciones[c.op_sel].tipo != TipoOpcion::Curar && j.opciones[c.op_sel].tipo != TipoOpcion::Evolucion;
  struct B { const char* l; char t1[64]; bool ok; ImU32 col; } bs[3];
  const DefSubida& sb = c.d.subida;
  bs[0] = {"X", "", ok_re, IM_COL32(60, 120, 230, 255)}, bs[1] = {"Y", "", true, IM_COL32(220, 170, 40, 255)}, bs[2] = {"LB", "", ok_ds, IM_COL32(200, 60, 70, 255)};
  std::snprintf(bs[0].t1, 64, "%s  %d", M::L("Re-sortear", "Reroll", "Retirer", "Neu würfeln", "Ritira"), j.CosteResortear());
  std::snprintf(bs[1].t1, 64, "%s  +%d / +%.0f%%", M::L("Saltar", "Skip", "Passer", "Überspringen", "Salta"), sb.saltar_zeni, double(sb.saltar_vida * 100));
  std::snprintf(bs[2].t1, 64, "%s  (%d)", M::L("Desterrar", "Banish", "Bannir", "Verbannen", "Bandisci"), j.DesterrosRestantes());
  for (int i = 0; i < 3; ++i) {
    const float bx = W * 0.5f + (float(i) - 1.0f) * (bw + bh2);
    const ImVec2 a(bx - bw * 0.5f, by), b(bx + bw * 0.5f, by + bh);
    const int al = int(255 * en);
    DibujarNueve(dl, "ui_boton_normal", a, b, 0.00783f * bh, bs[i].ok ? IM_COL32(255, 255, 255, al) : IM_COL32(120, 120, 130, al));
    Boton(dl, ImVec2(a.x + bh * 0.5f, a.y + bh * 0.5f), bh * 0.3f, bs[i].l, bs[i].ok ? bs[i].col : IM_COL32(90, 90, 100, 255));
    TxtMax(dl, 32 * k, ImVec2(a.x + bh * 0.95f, a.y + bh * 0.5f - 32 * k * 0.55f), bs[i].ok ? IM_COL32(245, 245, 255, al) : IM_COL32(185, 185, 200, al), bs[i].t1, bw - bh * 1.1f);
  }
  if (c.t - c.p.sub_msg_t < 1.6f) TxtC(dl, 30 * k, ImVec2(W * 0.5f, by + bh + 36 * k), IM_COL32(255, 170, 120, 255), c.p.sub_msg);
  S::HelpLine(dl, ImVec2(W * 0.5f, H - 52 * k), 34 * k, {{"A", M::L("Elegir", "Choose", "Choisir", "Wählen", "Scegli")}});
}

// ---------------------------------------------------------------- A11: cofre como espectaculo (tier = 1, 3 o 5 premios)
void CofreV2(const Ctx& c) {
  const Juego& j = c.j;
  ImDrawList* dl = c.dl;
  const float k = c.k, W = c.W, H = c.H, ct = c.p.cofre_t;
  static const bool prueba = std::getenv("DBZ3_TEST_SURVIVORS_COFRE") != nullptr;
  const float apertura = prueba ? 1.8f : 1.25f;   // s con el cofre cerrado (temblando); en pruebas mas largo (la captura llega con retraso)
  const int tier = std::max(1, j.nPremios);
  const float en = Suave(ct / 0.3f);
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 10, int(200 * en)));
  const ImVec2 cc(W * 0.5f, H * 0.90f);   // base del cofre (13/11: mas abajo y mas grande, los premios flotan sobre la tapa)
  const bool abierto = ct >= apertura;
  const float oro = tier >= 5 ? 1.0f : tier >= 3 ? 0.5f : 0.0f;
  const ImU32 tinte = tier >= 5 ? IM_COL32(255, 215, 90, 255) : tier >= 3 ? IM_COL32(190, 220, 255, 255) : IM_COL32(230, 170, 110, 255);
  // rayos detras del cofre abierto
  if (abierto) {
    const float s = Suave((ct - apertura) / 0.5f);
    if (const ps2::Tex* r = Arte5("cofre_rayos.png")) {
      const float rs = (1500.0f + 200.0f * std::sin(c.t * 2.0f)) * k * s;
      DibujarImagen(dl, r, ImVec2(cc.x - rs * 0.5f, cc.y - rs * 0.85f), ImVec2(cc.x + rs * 0.5f, cc.y + rs * 0.15f), Lerp32(IM_COL32(255, 255, 255, 0), tinte, 1.0f));
    } else {
      dl->AddCallback(rex::ui::kImDrawBlendAdditive, nullptr);   // 13/11: rayos mas largos, anchos y brillantes (antes 0,18 de alfa y casi no se veian)
      for (int i = 0; i < 18; ++i) {   // abanico de rayos dibujado
        const float ang = -3.14159f * (0.06f + 0.88f * float(i) / 17.0f) + 0.05f * std::sin(c.t * 1.5f + float(i));
        const float l = ((i & 1 ? 760.0f : 980.0f) + 120.0f * std::sin(c.t * 3.0f + float(i) * 1.7f)) * k * s;
        const ImVec2 a(cc.x, cc.y - 150 * k), b(cc.x + std::cos(ang) * l, cc.y - 150 * k + std::sin(ang) * l);
        Franja(dl, a, b, (i & 1 ? 22.0f : 34.0f) * k * s, Rgb(tier >= 5 ? 0xFFD060 : tier >= 3 ? 0xA0D0FF : 0xFFB080, 0.26f));
      }
      dl->AddCallback(rex::ui::kImDrawBlendAlpha, nullptr);
    }
  }
  // el cofre (tanda 5 si esta; si no, el de la tanda 3 ampliado), temblando antes de abrirse
  const char* arch = abierto ? (tier >= 5 ? "cofre_oro_abierto.png" : tier >= 3 ? "cofre_plata_abierto.png" : "cofre_bronce_abierto.png")
                             : (tier >= 5 ? "cofre_oro_cerrado.png" : tier >= 3 ? "cofre_plata_cerrado.png" : "cofre_bronce_cerrado.png");
  const float temblor = abierto ? 0.0f : std::sin(ct * 55.0f) * (3.0f + 10.0f * Suave(ct / apertura)) * k;
  if (const ps2::Tex* t = Arte5(arch)) {
    const float s = 480 * k * (abierto ? 1.0f : 0.96f + 0.04f * std::sin(ct * 20.0f));
    DibujarImagen(dl, t, ImVec2(cc.x - s * 0.5f + temblor, cc.y - s * 0.9f), ImVec2(cc.x + s * 0.5f + temblor, cc.y + s * 0.1f));
  } else if (const ps2::Tex* t = Arte(kArte3[abierto ? kCofreAbierto : kObjeto0 + int(TipoObjeto::Cofre)])) {
    DibujarCuadro(dl, t, 0, 1, ImVec2(64, 124), ImVec2(cc.x + temblor, cc.y), 330 * k / 128.0f, tinte);
  }
  if (abierto) {   // destello al abrirse
    const float f = 1.0f - Suave((ct - apertura) / 0.35f);
    if (f > 0) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(255, 245, 210, int(190 * f)));
  }
  // premios en abanico sobre el cofre: orbe + icono + nombre + antes -> despues
  if (abierto) {
    const int n = j.nPremios;
    const float radio = 560 * k;
    for (int i = 0; i < n; ++i) {
      const float u = n > 1 ? float(i) / float(n - 1) : 0.5f, ang = 3.14159f * (0.88f - 0.76f * u);   // de izquierda a derecha por encima del cofre
      const float a = Suave((ct - apertura - 0.18f * float(i) - 0.1f) / 0.4f);
      if (a <= 0) continue;
      const Opcion& o = j.premios[i];
      const ImVec2 base(cc.x + std::cos(ang) * radio * 1.45f * (n > 1 ? 1.0f : 0.0f), cc.y - 365 * k - std::sin(ang) * radio * (n > 1 ? 0.40f : 0.0f));
      const ImVec2 pc(base.x, base.y - (n == 1 ? 255 * k : 0) - (1 - a) * 90 * k);
      const uint32_t rgb = o.tipo == TipoOpcion::Evolucion ? 0xFF6A2A : ColorRareza(o.rareza);
      const float os = (n == 1 ? 230.0f : 170.0f) * k;
      if (const ps2::Tex* orb = Arte5("cofre_orbe_premio.png")) DibujarImagen(dl, orb, ImVec2(pc.x - os * 0.5f, pc.y - os * 0.5f), ImVec2(pc.x + os * 0.5f, pc.y + os * 0.5f), Rgb(rgb, a));
      else {
        dl->AddCircleFilled(pc, os * 0.5f, Rgb(rgb, 0.30f * a), 40);
        dl->AddCircle(pc, os * 0.5f, Rgb(rgb, 0.95f * a), 40, 5.0f * k);
      }
      const float ic = os * 0.62f;
      IconoComercial(dl, j.Icono(o), ImVec2(pc.x - ic * 0.5f, pc.y - ic * 0.5f), ImVec2(pc.x + ic * 0.5f, pc.y + ic * 0.5f), IM_COL32(255, 255, 255, int(255 * a)));
      S::MenuTextB3(dl, ImVec2(pc.x, pc.y + os * 0.5f + 26 * k), 32 * k, Es(j.Texto(o)), true, false, 320 * k);
      char buf[64];
      const int antes = j.nivel_previo_premio[i];
      if (o.tipo == TipoOpcion::Curar) std::snprintf(buf, sizeof buf, "%s", M::L("Curación", "Heal", "Soin", "Heilung", "Cura"));
      else if (antes == 0) std::snprintf(buf, sizeof buf, "%s %s %d", M::L("¡Nueva!", "New!", "Nouveau !", "Neu!", "Nuova!"), M::L("Nv.", "Lv.", "Niv.", "St.", "Liv."), o.nivel);
      else std::snprintf(buf, sizeof buf, "%s %d", M::L("Nv.", "Lv.", "Niv.", "St.", "Liv."), o.nivel);
      TxtC(dl, 28 * k, ImVec2(pc.x, pc.y + os * 0.5f + 56 * k), IM_COL32(255, 230, 140, int(255 * a)), buf);
      Cinta(dl, ImVec2(pc.x, pc.y - os * 0.5f - 8 * k), 300 * k, 52 * k, Rgb(rgb, 0.9f * a), o.tipo == TipoOpcion::Evolucion ? M::L("¡Evolución!", "Evolution!", "Évolution !", "Entwicklung!", "Evoluzione!") : RarezaTxt(o.rareza), 34 * k);
    }
    // texto de evolucion abajo: "Evoluciona X: requiere Y al maximo + Z" (el de la primera tecnica base premiada que tiene evolucion)
    for (int i = 0; i < n; ++i) {
      const Opcion& o = j.premios[i];
      if (o.tipo != TipoOpcion::Tecnica || o.def < 0) continue;
      for (const DefTecnica& e : c.d.tecnicas) {
        if (e.evoluciona_de != o.def || e.pasiva < 0 || size_t(e.pasiva) >= c.d.pasivas.size()) continue;
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s %s: %s %s %s %s", M::L("Evoluciona", "Evolves into", "Évolue en", "Entwickelt sich zu", "Evolve in"), Es(e.txt),
                      M::L("requiere", "needs", "demande", "braucht", "richiede"), Es(c.d.tecnicas[size_t(o.def)].txt),
                      M::L("al máximo +", "maxed +", "au max +", "auf Max +", "al massimo +"), Es(c.d.pasivas[size_t(e.pasiva)].txt));
        const float bw = std::min(W - 120 * k, 1000 * k), bh = bw / 6.0f, by = H - 190 * k;
        const ps2::Tex* cin = Arte5("evolucion_cinta.png");
        if (cin) DibujarNueveTex(dl, cin, 60, 60, 60, 60, ImVec2(W * 0.5f - bw * 0.5f, by), ImVec2(W * 0.5f + bw * 0.5f, by + bh), bw / 1200.0f);
        else {
          dl->AddRectFilled(ImVec2(W * 0.5f - bw * 0.5f, by), ImVec2(W * 0.5f + bw * 0.5f, by + bh * 0.62f), IM_COL32(20, 36, 64, 245), 12 * k);
          dl->AddRect(ImVec2(W * 0.5f - bw * 0.5f, by), ImVec2(W * 0.5f + bw * 0.5f, by + bh * 0.62f), IM_COL32(255, 150, 50, 255), 12 * k, 0, 4 * k);
        }
        TxtMax(dl, 38 * k, ImVec2(W * 0.5f - bw * 0.46f, by + bh * (cin ? 0.40f : 0.31f) - 20 * k), IM_COL32(255, 235, 170, 255), buf, bw * 0.92f);
        i = n;
        break;
      }
    }
  }
  if (!abierto) {
    // 20/11: el titulo baja a 204 (la placa del reloj acaba en 96*1,15 alrededor de y1+ni/2): antes se pisaban
    S::MenuTextB3(dl, ImVec2(W * 0.5f, 204 * k), 70 * k, M::L("¡Cofre!", "Chest!", "Coffre !", "Truhe!", "Forziere!"), true, false);
  } else {
    const float a = Suave((ct - apertura) / 0.4f);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %s", j.nPremios, j.nPremios == 1 ? M::L("premio", "reward", "récompense", "Belohnung", "premio") : M::L("premios", "rewards", "récompenses", "Belohnungen", "premi"));
    S::MenuTextB3(dl, ImVec2(W * 0.5f, 204 * k), 56 * k, M::L("¡Cofre abierto!", "Chest opened!", "Coffre ouvert !", "Truhe geöffnet!", "Forziere aperto!"), true, false);
    if (j.nPremios > 1) TxtC(dl, 30 * k, ImVec2(W * 0.5f, 244 * k), IM_COL32(255, 230, 140, int(255 * a)), buf);   // con 1 premio el recuento sobra (y el premio sube)
    (void)oro;
    if (ct > 2.2f) S::HelpLine(dl, ImVec2(W * 0.5f, H - 52 * k), 34 * k, {{"A", M::L("Recoger", "Collect", "Récupérer", "Einsammeln", "Raccogli")}});
  }
}
}  // namespace
namespace {
// aviso de un logro desbloqueado en partida: arriba al centro, uno cada vez (cola en p.logro_cola); `activo` = el tiempo corre
void AvisoLogro(ImDrawList* dl, float W, float k, float dt, bool activo, EstadoP2& p) {
  if (!activo || (p.logro_cola.empty() && p.logro_mostrado < 0)) return;   // en la subida, el cofre o la pausa espera su turno (cola)
  if (p.logro_mostrado < 0) p.logro_mostrado = p.logro_cola.front(), p.logro_cola.erase(p.logro_cola.begin()), p.logro_t = 0;
  if (activo) p.logro_t += dt;
  const std::vector<Logro>& ls = ListaLogros();
  if (p.logro_mostrado < int(ls.size())) {
    const Logro& l = ls[size_t(p.logro_mostrado)];
    static const bool prueba = std::getenv("DBZ3_TEST_SURVIVORS_LOGRO") != nullptr;
    const float dur = prueba ? 7.0f : 3.4f;
    const float a = std::min(1.0f, std::min(p.logro_t * 4.0f, (dur - p.logro_t) * 3.0f));
    if (a > 0) {
      const float w = 440 * k, h = 100 * k, x = (W - w) * 0.5f, y = ImGui::GetIO().DisplaySize.y - 16 * k - h + (1 - Suave(a)) * 60 * k;   // R23 SURV-7: abajo al centro, entre tecnicas y pasivas (arriba o a la derecha tapaba vida o accion)
      dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(8, 24, 46, int(235 * a)), 14 * k);
      dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 190, 50, int(255 * a)), 14 * k, 0, 4 * k);
      Pieza(dl, "ui_casilla_mejora", ImVec2(x + 62 * k, y + h * 0.5f), 84 * k, IM_COL32(255, 255, 255, int(255 * a)));
      IconoEn(dl, l.icono.c_str(), ImVec2(x + 28 * k, y + h * 0.5f - 34 * k), ImVec2(x + 96 * k, y + h * 0.5f + 34 * k), IM_COL32(255, 255, 255, int(255 * a)));
      Txt(dl, 22 * k, ImVec2(x + 120 * k, y + 12 * k), IM_COL32(255, 200, 70, int(255 * a)), M::L("Logro desbloqueado", "Achievement unlocked", "Succès débloqué", "Erfolg freigeschaltet", "Traguardo sbloccato"));
      TxtMax(dl, 32 * k, ImVec2(x + 120 * k, y + 40 * k), IM_COL32(255, 255, 255, int(255 * a)), LogroTxt(l, false), w - 140 * k);
      if (l.zeni > 0) {
        char b[32];
        std::snprintf(b, sizeof b, "+%d zeni", l.zeni);
        Txt(dl, 24 * k, ImVec2(x + 120 * k, y + 78 * k), IM_COL32(255, 230, 120, int(255 * a)), b);
      }
    }
  }
  if (p.logro_t > (std::getenv("DBZ3_TEST_SURVIVORS_LOGRO") ? 7.0f : 3.4f)) p.logro_mostrado = -1;
}
}  // namespace


void Modo::FondoMenu(ImDrawList* dl, float W, float H, float oscuro, const char* esc) {
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(12, 16, 34, 255));
  DibujarSuelo(dl, esc, ImVec2(0, 0), ImVec2(W, H), ImVec2(t_ * 18.0f * k_, t_ * 6.0f * k_), 1100.0f * k_);
  DibujarCapa(dl, esc, "lejano", ImVec2(0, 0), ImVec2(W, H));
  DibujarCapa(dl, esc, "medio", ImVec2(0, 0), ImVec2(W, H));
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 8, 24, int(255 * oscuro)));
}

void Modo::Rotulo(ImDrawList* dl, ImVec2 c, float w, const char* t) {
  const float s = w / 1024.0f;
  if (const ps2::Tex* r = T3(kRotuloVictoria)) DibujarCuadro(dl, r, 0, 1, ImVec2(512, 128), c, s);
  S::MenuTextB3(dl, c, 80 * s, t, true, false, 736 * s);
}

void Modo::PanelMenu(ImDrawList* dl, ImVec2 a, ImVec2 b) {
  if (!DibujarNueve(dl, "ui_panel_resultados", a, b, 0.55f * k_)) dl->AddRectFilled(a, b, IM_COL32(10, 34, 48, 235), 10 * k_);
}

void Modo::Ayuda(ImDrawList* dl, float W, float H, const char* a, const char* b, const char* y) {
  std::vector<std::pair<const char*, const char*>> v;   // solo en menus
  if (a) v.push_back({"A", a});
  if (b) v.push_back({"B", b});
  if (y) v.push_back({"Y", y});
  dl->AddRectFilledMultiColor(ImVec2(0, H - 110 * k_), ImVec2(W, H), IM_COL32(4, 8, 20, 0), IM_COL32(4, 8, 20, 0), IM_COL32(4, 8, 20, 200), IM_COL32(4, 8, 20, 200));   // R23 SURV-7
  S::HelpLine(dl, ImVec2(W * 0.5f, H - 50 * k_), 40 * k_, v);
}

void Modo::Carga(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  if (pad & kB) return Salir();
  // 21/11: antes de definir los atlas se espera al modelo de cada traje alternativo (malla del traje, clips del base;
  // TkEstado). ponytail: tope fijo de 30 s; si no llega o falla, ese enemigo usa el tono suave del toml.
  bool trajes_pendientes = false;
  if (!imp_definidos_ && t_pant_ < 30.0f)
    for (const DefEnemigo& e : datos_.enemigos)
      if (e.traje > 0) {
        const Modelo m = ModeloDe(e.fid);
        const int alt = pasarela::nave::FidTrajeB3(m.b3, e.traje, m.forma), base = pasarela::nave::FidTrajeB3(m.b3, 0, m.forma);
        if (alt >= 0 && alt != base && pasarela::nave::EstadoFiguraB3(m.b3, m.forma, alt) == 1) trajes_pendientes = true;
      }
  if (!imp_definidos_ && !trajes_pendientes) {
    // Cuatro atlas reales por tipo: andar, derrota, ataque y reaccion al golpe.
    std::vector<TipoImpostor> v;
    for (const int clip : {kClipAndar, kClipDerrota, kClipAtaque, kClipDano})
      for (const DefEnemigo& e : datos_.enemigos) {
        const Modelo m = ModeloDe(e.fid);
        // 21/11: variedad con los trajes alternativos de B3 (recolores oficiales); si el luchador no lo trae, el tono suave del toml
        int fid = e.fid;
        float tono = e.tono;
        if (e.traje > 0) {
          const int alt = pasarela::nave::FidTrajeB3(m.b3, e.traje, m.forma), base = pasarela::nave::FidTrajeB3(m.b3, 0, m.forma);
          if (alt >= 0 && alt != base && pasarela::nave::EstadoFiguraB3(m.b3, m.forma, alt) == 2) fid = alt, tono = 0;
          if (clip == kClipAndar) REXLOG_INFO("dbz3 survivors: enemigo {} traje {}: fid {} (base {}){}", e.id, e.traje, alt, base, fid == alt ? "" : " -> tono");
        }
        v.push_back({m.b3, m.forma, fid, tono, e.escala > 1.3f ? 128 : 96, clip});
      }
    imp_.Definir(v);
    imp_definidos_ = true;
  }
  imp_.Avanzar();
  static float peor_carga = 0; static int lentos = 0, cuadros_carga = 0;   // SUR-08: cuadros reales de la carga (ms) y cuantos pasan de 100 ms
  if (t_pant_ < 0.3f) peor_carga = 0, lentos = 0, cuadros_carga = 0;
  { const float dtr = ImGui::GetIO().DeltaTime * 1000.0f; ++cuadros_carga; peor_carga = std::max(peor_carga, dtr); lentos += dtr > 100.0f; }
  int listos = 0, total = 0;
  bool falla = false;
  // No empezar con enemigos invisibles ni con hornos trabajando al aparecer la primera oleada.
  const auto figura = [&](int fid) {
    const Modelo m = ModeloDe(fid);
    const int e = pasarela::nave::EstadoFiguraB3(m.b3, m.forma, fid);
    listos += e == 2, falla |= e == 0, ++total;
  };
  for (const DefPersonaje& p : datos_.personajes) figura(p.fid);
  PrecargaFormas(listos, total, falla);
  listos += PrecargaFx(), ++total;
  // SUR-08: solo el clip de andar de cada enemigo bloquea la carga; derrota, ataque y golpe se hornean ya en la pantalla de eleccion (Avanzar sigue)
  if (!imp_definidos_) total += int(datos_.enemigos.size());   // aun esperando trajes
  else for (int i = 0; i < int(datos_.enemigos.size()); ++i) {
    const int e = imp_.Estado(i);
    listos += e == 2, falla |= e == 0, ++total;
  }
  // Los jefes y sus fases tambien quedan preparados por PrecargaFormas.
  if (falla)
    return Fallo(T::T("No se pudieron preparar los modelos. Faltan datos de Budokai 3 HD: comprueba que data_cmn.afs está en la carpeta del juego.",
                      "Could not prepare the models. Budokai 3 HD data is missing: check that data_cmn.afs is in the game folder."));
  // R18: las copias frias del laboratorio hornean todos los atlas. Ampliacion
  // exclusiva de prueba; el limite del jugador sigue siendo kTopeCarga.
  static const float tope_carga=[] {
    const char* e=std::getenv("DBZ3_TEST_SURVIVORS_CARGA_S");
    return e ? std::clamp(float(std::atof(e)),kTopeCarga,900.f) : kTopeCarga;
  }();
  if (t_pant_ > tope_carga) return Fallo(T::T("La preparación de los modelos tarda demasiado. Vuelve a intentarlo más tarde.", "Preparing the models is taking too long. Try again later."));
  if (listos == total) {
    cargado_ = true;
    REXLOG_INFO("dbz3 survivors: cargado ({} modelos en {:.1f} s; {} cuadros, peor {:.0f} ms, {} de mas de 100 ms)", total, t_pant_, cuadros_carga, peor_carga, lentos);
    return Ir(Inicial());
  }
  FondoMenu(fondo, W, H, 0.55f);
  if (T3(kLogo)) Pieza3(dl, kLogo, ImVec2(512, 192), ImVec2(W * 0.5f, 300 * k_), 900 * k_ / 1024.0f);   // tanda 3: el logotipo
  else Titulo(dl, W, 300, "Dragon Budokai Survivors", 84);
  Precarga(2);
  char buf[96];
  std::snprintf(buf, sizeof buf, "%s  %d / %d", T::T("Preparando luchadores y enemigos", "Preparing fighters and enemies"), listos, total);
  TxtC(dl, 34 * k_, ImVec2(W * 0.5f, 520 * k_), IM_COL32_WHITE, buf);
  const float pct = (float(listos) + 0.5f * (0.5f + 0.5f * std::sin(t_ * 4.0f)) * (listos < total)) / float(std::max(1, total));
  const ps2::Tex *marco = T3(kCargaMarco), *relleno = T3(kCargaRelleno);
  if (marco && relleno) {   // tanda 4: relleno recortado por porcentaje en fill_rect (13,14 + 999x22 de 1024x48) y el marco encima, mismo lienzo
    const float bw = 1000 * k_, s = bw / 1024.0f, bx = (W - bw) * 0.5f, by = 570 * k_;
    const float u = (13.0f + 999.0f * std::clamp(pct, 0.0f, 1.0f)) / 1024.0f;
    dl->AddImage(relleno->id(), ImVec2(bx, by), ImVec2(bx + 1024 * s * u, by + 48 * s), ImVec2(0, 0), ImVec2(u, 1));
    dl->AddImage(marco->id(), ImVec2(bx, by), ImVec2(bx + 1024 * s, by + 48 * s));
  } else {
    const float bw = 760 * k_, bh = bw / 8.0f;   // ui_barra_ki: 512 x 64
    DibujarBarra(dl, "ki", ImVec2((W - bw) * 0.5f, 570 * k_), ImVec2((W + bw) * 0.5f, 570 * k_ + bh), pct);
  }
  TxtC(dl, 24 * k_, ImVec2(W * 0.5f, 660 * k_), IM_COL32(200, 210, 255, 220),
       T::T("La primera vez puede tardar un poco: se generan a partir de Budokai 3.", "The first time may take a while: they are generated from Budokai 3."));
  Consejo(dl, W, ImVec2(W * 0.5f, 830 * k_));
  Ayuda(dl, W, H, nullptr, T::T("Cancelar", "Cancel"));
}

void Modo::Consejo(ImDrawList* dl, float W, ImVec2 c) {
  const ps2::Tex* t = T3(kConsejo);
  const float cw = std::min(W - 80 * k_, 1100 * k_), ch = cw * 300.0f / 1200.0f, s = cw / 1200.0f;
  const ImVec2 a(c.x - cw * 0.5f, c.y - ch * 0.5f), b(c.x + cw * 0.5f, c.y + ch * 0.5f);
  if (!DibujarNueveTex(dl, t, 28, 28, 28, 28, a, b, s)) dl->AddRectFilled(a, b, IM_COL32(8, 30, 50, 220), 10 * k_);
  const int n = int(t_ / 7.0f) % kNumConsejos;
  const float zx = a.x + 31 * s, zy = a.y + 42 * s, zw = 1139 * s, zh = 216 * s;
  S::MenuTextB3(dl, ImVec2(zx + zw * 0.5f, zy + 30 * s), 46 * s, T::T("Consejo", "Tip"), true, false, zw);
  const char* txt = T::T(kConsejos[n][0], kConsejos[n][1]);
  const float fs = 40 * s, w = zw - 60 * s, h = Medir(fs, txt, w).y;
  Txt(dl, fs, ImVec2(zx + 30 * s, zy + 70 * s + std::max(0.0f, (zh - 70 * s - h) * 0.5f)), IM_COL32(230, 236, 250, 255), txt, w);
}

void Modo::Eleccion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  const int n = int(datos_.personajes.size());
  const bool v2 = MenuV2();
  const int cols = v2 && n > 4 ? 4 : n;
  const auto bloqueo = [&](int i) { return v2 && MetaV2() ? LogroQueAbre(ListaLogros(), datos_, prog_, i) : -1; };   // logro que lo abre (-1 = abierto)
  if (v2 && Recorrido() && t_pant_ > 3.0f) pad |= kB;
  if (pad & kB) {
    if (!v2) return Salir();
    Sonar("atras", 5, 0);
    return Ir(Pantalla(kPantPortada));
  }
  if (pad & kIzq) sel_ = (sel_ + n - 1) % n;
  if (pad & kDer) sel_ = (sel_ + 1) % n;
  if (v2 && n > cols) {
    if (pad & kAbajo) sel_ = (sel_ + cols) % n;
    if (pad & kArriba) sel_ = (sel_ + n - cols) % n;
  }
  if (pad & kY) return AbrirTienda();
  if (Pr().piloto && t_pant_ > 2.5f) sel_ = std::min(Pr().personaje, n - 1), pad |= kA;   // deja ver la pantalla (captura)
  if ((pad & kA) && bloqueo(sel_) >= 0) {   // luchador bloqueado: dice como se abre
    Sonar("bloqueado", 5, 0);
    p2_.sub_msg_t = t_;
  } else if (pad & kA) {   // tanda 4: despues, el mapa (las pruebas con ESCENARIO lo saltan)
    Sonar("aceptar", 3, 0);
    if (EscenarioPrueba() >= 0) return Empezar(sel_, EscenarioPrueba(), Pr().dificultad);
    return Ir(Pantalla::Mapa);
  }

  FondoMenu(fondo, W, H, 0.45f);
  Rotulo(dl, ImVec2(W * 0.5f, 92 * k_), 620 * k_, T::T("Elige luchador", "Choose your fighter"));
  Precarga(4);   // el resto del arte de la partida, unas pocas texturas por cuadro
  PrecargaEscenario(std::max(0, EscenarioPrueba()), 2);
  // R23 SURV-6: rejilla de tarjetas a la izquierda y, a la derecha, la ficha del elegido en el panel de resultados (nombre, retrato,
  // tecnica inicial y estadisticas en barras; si esta bloqueado, como se abre). Las tarjetas solo llevan retrato y nombre.
  const ps2::Tex *tj_n = ArteMejoras("gpt3/survivors_tarjeta_luchador_normal.png"), *tj_e = ArteMejoras("gpt3/survivors_tarjeta_luchador_elegida.png"),
                 *tj_b = ArteMejoras("gpt3/survivors_tarjeta_luchador_bloqueada.png"), *tj_c = ArteMejoras("gpt3/survivors_candado.png");
  if (const ps2::Tex* t = Arte5("comercial/tarjeta_bloqueada.png")) tj_b = t;
  const int filas = (n + cols - 1) / cols;
  const float cw = (filas > 1 ? 226 : 270) * k_, chh = cw * 500.0f / 360.0f, hueco = 22 * k_, s2 = cw / 360.0f;
  const float gx0 = 90 * k_ + float(4 - cols) * (cw + hueco) * 0.5f, gy0 = 570 * k_ - (float(filas) * chh + float(filas - 1) * hueco) * 0.5f;   // R23 SURV-7: centrada frente a la ficha (190..950)
  float fs_bloq = 28 * k_;   // la letra de las condiciones: la que cabe en la mas larga
  for (int i = 0; i < n; ++i)
    if (bloqueo(i) >= 0) fs_bloq = std::min(fs_bloq, 28 * k_ * cw * 0.8f / std::max(1.0f, Medir(28 * k_, CondTxt(ListaLogros()[size_t(bloqueo(i))]).c_str()).x));
  for (int i = 0; i < n; ++i) {
    const DefPersonaje& p = datos_.personajes[size_t(i)];
    const bool el = i == sel_, bloq = bloqueo(i) >= 0;
    const float g = el ? 1.04f + 0.01f * std::sin(t_ * 3.0f) : 1.0f;
    const ImVec2 cc(gx0 + float(i % cols) * (cw + hueco) + cw * 0.5f, gy0 + float(i / cols) * (chh + hueco) + chh * 0.5f);
    const ImVec2 A(cc.x - cw * 0.5f * g, cc.y - chh * 0.5f * g), B(cc.x + cw * 0.5f * g, cc.y + chh * 0.5f * g);
    const float sg = s2 * g;
    if (el) fondo->AddRectFilled(ImVec2(A.x - 8 * k_, A.y - 8 * k_), ImVec2(B.x + 8 * k_, B.y + 8 * k_), IM_COL32(255, 200, 40, 80), 18 * k_);
    if (tj_n && tj_e && tj_b) {
      DibujarImagen(fondo, bloq ? tj_b : el ? tj_e : tj_n, A, B);
    } else {
      fondo->AddRectFilled(A, B, IM_COL32(16, 38, 50, 255), 10 * k_);
      dl->AddRect(A, B, el ? IM_COL32(255, 205, 70, 255) : IM_COL32(150, 100, 40, 255), 10 * k_, 0, 3 * k_);
    }
    const ImVec2 a(A.x + 36 * sg, A.y + 36 * sg), b(a.x + 288 * sg, a.y + 288 * sg);
    // fondo del retrato y figura en la lista de FONDO (si la figura no cabe en su lista va a una ventana aparte, entre el fondo y el marco)
    fondo->AddRectFilledMultiColor(a, b, IM_COL32(100, 150, 235, 255), IM_COL32(100, 150, 235, 255), IM_COL32(32, 52, 125, 255), IM_COL32(32, 52, 125, 255));
    if (!bloq) fondo->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.55f), (b.x - a.x) * 0.31f, IM_COL32(255, 255, 255, 55), 48);
    const Modelo m = ModeloDe(p.fid);
    // de frente (giro 0); el elegido solo se mece; la espera desfasada por retrato para que no respiren a la vez
    if (!bloq) DibujarFiguraEnRect(fondo, ImVec2(a.x - (b.x - a.x) * 0.22f, a.y - (b.y - a.y) * 0.30f), ImVec2(b.x + (b.x - a.x) * 0.22f, b.y + (b.y - a.y) * 0.06f), m.b3, m.forma, p.fid, el ? 0.25f * std::sin(t_ * 1.2f) : 0.0f, kClipEspera, t_ + 0.37f * float(i));   // R23 SURV-7: figura mayor
    if (!el && !bloq) dl->AddRectFilled(a, b, IM_COL32(0, 0, 20, 70));
    if (bloq) {   // bloqueado: silueta negra de su cara de B3 sobre el fondo de retrato (R23 SURV-7: antes azul sobre azul, no se leia) y el candado
      dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.5f), (b.x - a.x) * 0.36f, IM_COL32(255, 255, 255, 50), 48);
      const float q = (b.x - a.x) * 0.1f;
      SiluetaBloqueada(dl, p, m.b3, ImVec2(a.x + q, a.y + q), ImVec2(b.x - q, b.y - q), false);
      const float r = (b.x - a.x) * 0.2f;
      if (tj_c) DibujarImagen(dl, tj_c, ImVec2(cc.x - r, b.y - r * 2.1f), ImVec2(cc.x + r, b.y - r * 0.1f));
    }
    if (bloq) {   // "???" y como se abre, con la misma letra en todas
      S::MenuTextB3(dl, ImVec2(cc.x, b.y + (B.y - b.y) * 0.30f), 32 * k_ * g, "???", el, true, cw * 0.86f);
      TxtC(dl, fs_bloq * g, ImVec2(cc.x, b.y + (B.y - b.y) * 0.64f), IM_COL32(255, 205, 150, 255), CondTxt(ListaLogros()[size_t(bloqueo(i))]).c_str());
    } else {
      S::MenuTextB3(dl, ImVec2(cc.x, b.y + (B.y - b.y) * 0.45f), 38 * k_ * g, Es(p.txt), el, false, cw * 0.86f);
    }
  }
  // ficha del elegido
  const ImVec2 pa(1110 * k_, 190 * k_), pb(W - 80 * k_, 950 * k_);
  PanelMenu(fondo, pa, pb);
  const float px = pa.x + 60 * k_, pw = pb.x - pa.x - 120 * k_, pcx = (pa.x + pb.x) * 0.5f;
  const DefPersonaje& ps = datos_.personajes[size_t(sel_)];
  const Modelo ms = ModeloDe(ps.fid);
  const ImU32 gris = IM_COL32(200, 210, 240, 255), oro = IM_COL32(255, 225, 90, 255);
  if (bloqueo(sel_) >= 0) {
    S::MenuTextB3(dl, ImVec2(pcx, pa.y + 90 * k_), 64 * k_, "???", true, false, pw);
    const float cs = 260 * k_;   // R23 SURV-7: 300 -> 260 (la cara ampliada se veia borrosa)
    const ImVec2 ca(pcx - cs * 0.5f, pa.y + 170 * k_), cb(ca.x + cs, ca.y + cs);
    dl->AddRectFilledMultiColor(ca, cb, IM_COL32(100, 150, 235, 255), IM_COL32(100, 150, 235, 255), IM_COL32(32, 52, 125, 255), IM_COL32(32, 52, 125, 255));
    SiluetaBloqueada(dl, ps, ms.b3, ca, cb, true);
    MarcoCara(dl, ca, cb, k_);
    if (tj_c) DibujarImagen(dl, tj_c, ImVec2(cb.x - 96 * k_, cb.y - 110 * k_), ImVec2(cb.x - 6 * k_, cb.y - 10 * k_));
    S::MenuTextB3(dl, ImVec2(pcx, cb.y + 80 * k_), 44 * k_, M::L("Cómo se abre", "How to unlock", "Comment le débloquer", "So schaltest du ihn frei", "Come si sblocca"), true, false, pw);
    const std::string cts = CondTxt(ListaLogros()[size_t(bloqueo(sel_))]);
    const char* ct = cts.c_str();
    const float fs = 42 * k_, w = std::min(pw, Medir(fs, ct).x);
    Txt(dl, fs, ImVec2(pcx - w * 0.5f, cb.y + 130 * k_), IM_COL32(255, 215, 170, 255), ct, pw);
    Txt(dl, 30 * k_, ImVec2(px, pb.y - 150 * k_), IM_COL32(200, 210, 240, 255),
        M::L("Cada logro abre un luchador nuevo con su técnica de Budokai 3.", "Each achievement unlocks a new fighter with their Budokai 3 technique.",
             "Chaque succès débloque un combattant avec sa technique de Budokai 3.", "Jeder Erfolg schaltet einen Kämpfer mit seiner Budokai-3-Technik frei.",
             "Ogni traguardo sblocca un lottatore con la sua tecnica di Budokai 3."), pw);
  } else {
    S::MenuTextB3(dl, ImVec2(pcx, pa.y + 90 * k_), 64 * k_, Es(ps.txt), true, false, pw);
    const float cs = 230 * k_;
    const ImVec2 ca(px, pa.y + 150 * k_), cb(ca.x + cs, ca.y + cs);
    if (CaraB3(dl, ms.b3, ca, cb, IM_COL32_WHITE, true)) MarcoCara(dl, ca, cb, k_);
    const float ix = cb.x + 30 * k_, iw = pb.x - 60 * k_ - ix;
    Txt(dl, 28 * k_, ImVec2(ix, ca.y + 10 * k_), gris, M::L("Técnica inicial", "Starting technique", "Technique de départ", "Starttechnik", "Tecnica iniziale"));
    if (ps.tecnica >= 0 && ps.tecnica < int(datos_.tecnicas.size())) {
      const DefTecnica& t = datos_.tecnicas[size_t(ps.tecnica)];
      const float ic = 72 * k_, y = ca.y + 60 * k_;
      Pieza(dl, "ui_casilla_mejora", ImVec2(ix + ic * 0.5f, y + ic * 0.5f), ic);
      IconoEn(dl, t.icono.c_str(), ImVec2(ix + ic * 0.12f, y + ic * 0.12f), ImVec2(ix + ic * 0.88f, y + ic * 0.88f));
      Txt(dl, 30 * k_, ImVec2(ix, y + ic + 14 * k_), oro, Es(t.txt), iw);
    }
    // estadisticas: barras frente al mejor de los luchadores
    const auto valor = [&](const DefPersonaje& q, int s) {
      if (s == 0) return q.vida;
      if (s == 1) return q.velocidad;
      if (s == 2) return q.tecnica >= 0 && q.tecnica < int(datos_.tecnicas.size()) ? datos_.tecnicas[size_t(q.tecnica)].dano[0] : 0.0f;
      return q.recogida;
    };
    float tope[4] = {1, 1, 1, 1};
    for (const DefPersonaje& q : datos_.personajes)
      for (int s = 0; s < 4; ++s) tope[s] = std::max(tope[s], valor(q, s));
    const char* et[4] = {T::T("Vida", "Health"), T::T("Velocidad", "Speed"), M::L("Daño", "Damage", "Dégâts", "Schaden", "Danno"),
                         M::L("Alcance", "Pickup", "Portée", "Reichweite", "Portata")};
    const int ics[4] = {kStatVida, kStatVelocidad, kStatDano, kStatAlcance};
    const float sy0 = cb.y + 60 * k_, paso = 74 * k_, is = 44 * k_, bx = px + is + 200 * k_, bw = pb.x - 60 * k_ - bx - 90 * k_;
    for (int s = 0; s < 4; ++s) {
      const float y = sy0 + paso * float(s), v = valor(ps, s);
      if (T3(ics[s])) Pieza3(dl, ics[s], ImVec2(32, 32), ImVec2(px + is * 0.5f, y), is / 56.0f);
      Txt(dl, 30 * k_, ImVec2(px + is + 14 * k_, y - 17 * k_), gris, et[s]);
      (void)tope, (void)bx, (void)bw;
      char vb[16];   // R23 SURV-7: la cifra, como en la portada (las casillas de rango no se entendian)
      std::snprintf(vb, sizeof vb, s == 1 || s == 3 ? "%.1f" : "%.0f", double(v));
      dl->AddLine(ImVec2(px, y + 30 * k_), ImVec2(pb.x - 60 * k_, y + 30 * k_), IM_COL32(200, 120, 40, 90), 1.5f * k_);
      Txt(dl, 34 * k_, ImVec2(pb.x - 60 * k_ - Medir(34 * k_, vb).x, y - 19 * k_), IM_COL32(255, 225, 90, 255), vb);
    }
  }
  ZeniTotal(dl, ImVec2(W - 40 * k_, 60 * k_), 40 * k_);
  if (v2 && t_ - p2_.sub_msg_t < 2.5f && bloqueo(sel_) >= 0)
    TxtC(dl, 30 * k_, ImVec2(W * 0.5f, H - 100 * k_), IM_COL32(255, 170, 120, 255), M::L("Bloqueado: cumple su logro para abrirlo.", "Locked: complete its achievement to open it.", "Verrouillé : accomplis son succès pour l'ouvrir.", "Gesperrt: erfülle seinen Erfolg, um ihn zu öffnen.", "Bloccato: completa il suo traguardo per aprirlo."));
  Ayuda(dl, W, H, T::T("Elegir", "Select"), v2 ? M::L("Volver", "Back", "Retour", "Zurück", "Indietro") : T::T("Volver al HUB", "Back to the HUB"), T::T("Tienda", "Shop"));
}

void Modo::ZeniTotal(ImDrawList* dl, ImVec2 p, float fs, ImU32 col) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d", prog_.zeni);
  const float w = Medir(fs, buf).x;
  Txt(dl, fs, ImVec2(p.x - w, p.y - fs * 0.55f), col, buf);
  if (const ps2::Tex* z = T3(kObjeto0 + int(TipoObjeto::Zeni))) DibujarCuadro(dl, z, 0, 1, ImVec2(64, 64), ImVec2(p.x - w - fs * 0.75f, p.y), fs * 1.3f / 128.0f);
  else Pieza(dl, "ui_moneda", ImVec2(p.x - w - fs * 0.75f, p.y), fs * 1.2f);
}

void Modo::Mapa(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  const int n = int(datos_.escenarios.size());
  map_sel_ = std::clamp(map_sel_, 0, n - 1);
  if (pad & kB) return Ir(Pantalla::Eleccion);
  if (pad & kIzq) map_sel_ = (map_sel_ + n - 1) % n, Sonar("mover", 2, 0);
  if (pad & kDer) map_sel_ = (map_sel_ + 1) % n, Sonar("mover", 2, 0);
  if (pad & (kArriba | kAbajo)) dif_sel_ ^= 1, Sonar("mover", 2, 0);
  if (Pr().piloto && t_pant_ > 2.0f) map_sel_ = 0, dif_sel_ = Pr().dificultad, pad |= kA;   // deja ver la pantalla (captura)
  if (pad & kA) {
    if (prog_.Desbloqueado(map_sel_)) {
      Sonar("aceptar", 3, 0);
      return Empezar(sel_, map_sel_, dif_sel_);
    }
    map_aviso_ = t_;
    Sonar("bloqueado", 5, 0);
  }
  FondoMenu(fondo, W, H, 0.5f, datos_.escenarios[size_t(map_sel_)].id.c_str());   // el fondo del escenario elegido
  Precarga(3), PrecargaEscenario(map_sel_, 3);
  for (int e = 0; e < n; ++e) PrecargaEscenario(e, 1), Fondo(datos_.escenarios[size_t(e)].id.c_str(), "suelo");   // R23 SURV-7: las miniaturas de todos (los bloqueados tambien se dibujan)
  // R23 SURV-6: el escenario elegido en grande dentro del panel de resultados (foto, nombre, jefe con su cara, descripcion y dificultad)
  // y debajo la tira de los 6 escenarios para moverse
  Rotulo(dl, ImVec2(W * 0.5f, 84 * k_), 560 * k_, T::T("Elige escenario", "Choose a stage"));
  ZeniTotal(dl, ImVec2(W - 40 * k_, 60 * k_), 40 * k_);
  const DefEscenario& d = datos_.escenarios[size_t(map_sel_)];
  const bool abierto = prog_.Desbloqueado(map_sel_);
  const ImVec2 pa(150 * k_, 168 * k_), pb(W - 150 * k_, 690 * k_);   // R23 SURV-8: 712 -> 690 (sobraba hueco bajo la ficha)
  PanelMenu(fondo, pa, pb);
  {   // foto: marco resaltado de la tanda 4 (image_rect 22,24 + 468x240 de 512x288)
    const float fw = 800 * k_, s = fw / 512.0f;
    const ImVec2 A(pa.x + 45 * k_, (pa.y + pb.y) * 0.5f - 144 * s), B(A.x + fw, A.y + 288 * s);
    const ImVec2 ia(A.x + 22 * s, A.y + 24 * s), ib(ia.x + 468 * s, ia.y + 240 * s);
    Miniatura(fondo, map_sel_, ia, ib);
    if (!abierto) fondo->AddRectFilled(ia, ib, IM_COL32(6, 10, 30, 60));   // R23 SURV-8: velo suave, se ve el dibujo
    if (!DibujarNueveTex(dl, T3(kTarjetaEscRes), 28, 28, 28, 28, A, B, s)) dl->AddRect(A, B, IM_COL32(255, 170, 40, 255), 8 * k_, 0, 3 * k_);
    if (!abierto) DibujarAjustada(dl, ArteMejoras("rogue/ui_candado.png"), ImVec2(B.x - 110 * k_, B.y - 120 * k_), ImVec2(B.x - 40 * k_, B.y - 50 * k_));
    if (const int md = prog_.medalla[map_sel_]) {
      const ImVec2 mc(B.x - 34 * k_, A.y + 34 * k_);   // R23 SURV-8: dentro del marco (se salia del panel por arriba)
      if (md == kPlatino) DibujarAjustada(dl, T3(kMedallaPlatino), ImVec2(mc.x - 40 * k_, mc.y - 40 * k_), ImVec2(mc.x + 40 * k_, mc.y + 40 * k_));
      else Pieza(dl, md == kOro ? "ui_medalla_oro" : md == kPlata ? "ui_medalla_plata" : "ui_medalla_bronce", mc, 80 * k_);
    }
  }
  const float ix = pa.x + 900 * k_, iw = pb.x - 50 * k_ - ix, icx = ix + iw * 0.5f;
  S::MenuTextB3(dl, ImVec2(icx, pa.y + 80 * k_), 64 * k_, Es(d.txt), true, !abierto, iw);
  {   // jefe: su cara de B3 y su nombre
    const DefJefe& jf = datos_.jefes[size_t(d.jefe)];
    const float cs = 104 * k_;
    const ImVec2 ca(ix, pa.y + 140 * k_), cb(ca.x + cs, ca.y + cs);
    if (CaraB3(dl, ModeloDe(jf.fid).b3, ca, cb, abierto ? IM_COL32_WHITE : IM_COL32(150, 150, 165, 255))) MarcoCara(dl, ca, cb, k_);
    Txt(dl, 28 * k_, ImVec2(cb.x + 24 * k_, ca.y + 14 * k_), IM_COL32(200, 210, 240, 255), T::T("Jefe", "Boss"));
    TxtMax(dl, 40 * k_, ImVec2(cb.x + 24 * k_, ca.y + 50 * k_), IM_COL32(255, 225, 90, 255), Es(jf.txt), iw - cs - 24 * k_);
  }
  {   // descripcion, o como se abre
    char buf[192];
    const float ty = pa.y + 280 * k_;
    if (!abierto) {
      const DefEscenario& prev = datos_.escenarios[size_t(std::max(0, map_sel_ - 1))];
      if (MetaV2()) std::snprintf(buf, sizeof buf, M::L("Vence a %s en %s (o llega al minuto 10) para abrirlo.", "Defeat %s in %s (or reach minute 10) to unlock it.", "Bats %s dans %s (ou atteins la minute 10) pour l'ouvrir.", "Besiege %s in %s (oder erreiche Minute 10), um ihn zu öffnen.", "Sconfiggi %s in %s (o arriva al minuto 10) per aprirlo."),
                                  Es(datos_.jefes[size_t(prev.jefe)].txt), Es(prev.txt));
      else std::snprintf(buf, sizeof buf, T::T("Gana, o llega al minuto 10, en %s para abrirlo.", "Win, or reach minute 10, in %s to unlock it."), Es(prev.txt));
      const float a = t_ - map_aviso_ < 2.0f ? 1.0f : 0.85f;
      Cinta(dl, ImVec2(ix + 90 * k_, ty + 2 * k_), 180 * k_, 40 * k_, IM_COL32(200, 70, 40, 255), M::L("Bloqueado", "Locked", "Verrouillé", "Gesperrt", "Bloccato"), 26 * k_);
      Txt(dl, 28 * k_, ImVec2(ix, ty + 32 * k_), IM_COL32(255, 220, 180, int(255 * a)), buf, iw);
    } else {
      Txt(dl, 30 * k_, ImVec2(ix, ty), IM_COL32(235, 240, 255, 255), Desc(d.txt), iw);
    }
  }
  {   // dificultad: dos botones nativos con su insignia; debajo, lo que cambia
    const char* nombres[2] = {T::T("Más fácil", "Easier"), T::T("Difícil", "Hard")};
    const float bw = (iw - 20 * k_) * 0.5f, bh = 74 * k_, by = pb.y - 168 * k_;
    for (int q = 0; q < 2; ++q) {
      const bool el = q == dif_sel_ && abierto;   // bloqueado: las dos apagadas
      const ImVec2 a(ix + float(q) * (bw + 20 * k_), by), b(a.x + bw, by + bh);
      DibujarNueve(dl, !abierto ? "ui_boton_deshabilitado" : el ? "ui_boton_encima" : "ui_boton_normal", a, b, 0.00783f * bh, el || !abierto ? IM_COL32_WHITE : IM_COL32(105, 110, 128, 255));   // R23 SURV-7: la no elegida, apagada
      DibujarAjustada(dl, T3(q ? kDif2 : kDif1), ImVec2(a.x + 10 * k_, a.y + 6 * k_), ImVec2(a.x + 10 * k_ + bh - 12 * k_, b.y - 6 * k_), IM_COL32(255, 255, 255, el ? 255 : 140));
      S::MenuTextB3(dl, ImVec2((a.x + bh + b.x) * 0.5f, (a.y + b.y) * 0.5f), 36 * k_, nombres[q], el, !el, bw - bh - 10 * k_);
    }
    char buf[192];
    if (dif_sel_)
      std::snprintf(buf, sizeof buf, T::T("Vida x%.1f, daño x%.1f, zeni x%.2f y medalla de platino.", "Health x%.1f, damage x%.1f, zeni x%.2f and a platinum medal."),
                    double(datos_.dificultad[1].vida), double(datos_.dificultad[1].dano), double(datos_.dificultad[1].zeni));
    else
      std::snprintf(buf, sizeof buf, T::T("Vida enemiga x%.2f, daño enemigo x%.2f.", "Enemy health x%.2f, enemy damage x%.2f."),
                    double(datos_.dificultad[0].vida), double(datos_.dificultad[0].dano));
    if (abierto) TxtMax(dl, 30 * k_, ImVec2(ix, by + bh + 16 * k_), IM_COL32(220, 228, 250, 255), buf, iw);   // R23 SURV-7: bloqueado, no aplica
  }
  // tira de escenarios
  const int n_ = n;
  const float cw = 210 * k_, ch = cw * 246.0f / 478.0f, hueco = 26 * k_, x0 = (W - (float(n_) * cw + float(n_ - 1) * hueco)) * 0.5f, y0 = 772 * k_;
  PanelMenu(fondo, ImVec2(x0 - 40 * k_, y0 - 36 * k_), ImVec2(W - x0 + 40 * k_, y0 + ch + 76 * k_));
  const float fl = 64 * k_ * (1.0f + 0.06f * std::sin(t_ * 5.0f));
  Pieza(dl, "ui_selector_izquierda", ImVec2(x0 - 90 * k_, y0 + ch * 0.5f), fl);
  Pieza(dl, "ui_selector_derecha", ImVec2(W - x0 + 90 * k_, y0 + ch * 0.5f), fl);
  for (int e = 0; e < n_; ++e) {
    const DefEscenario& de = datos_.escenarios[size_t(e)];
    const bool el = e == map_sel_, ab = prog_.Desbloqueado(e);
    const float sube = el ? 10 * k_ : 0;
    const ImVec2 a(x0 + float(e) * (cw + hueco), y0 - sube), b(a.x + cw, a.y + ch);
    if (el) fondo->AddRectFilled(ImVec2(a.x - 10 * k_, a.y - 10 * k_), ImVec2(b.x + 10 * k_, b.y + 10 * k_), IM_COL32(255, 200, 40, 90), 12 * k_);
    Miniatura(fondo, e, a, b);   // R23 SURV-7: los bloqueados tambien con su dibujo (apagado): dan pista y no son cajas iguales
    if (!ab) fondo->AddRectFilled(a, b, IM_COL32(6, 10, 30, el ? 50 : 85));
    else if (!el) fondo->AddRectFilled(a, b, IM_COL32(0, 0, 20, 60));
    dl->AddRect(ImVec2(a.x - 3 * k_, a.y - 3 * k_), ImVec2(b.x + 3 * k_, b.y + 3 * k_), IM_COL32(20, 12, 6, 255), 6 * k_, 0, 4 * k_);
    dl->AddRect(a, b, el ? IM_COL32(255, 205, 70, 255) : IM_COL32(190, 120, 40, 255), 4 * k_, 0, el ? 4 * k_ : 2 * k_);
    if (!ab) DibujarAjustada(dl, ArteMejoras("rogue/ui_candado.png"), ImVec2((a.x + b.x) * 0.5f - 26 * k_, (a.y + b.y) * 0.5f - 26 * k_), ImVec2((a.x + b.x) * 0.5f + 26 * k_, (a.y + b.y) * 0.5f + 26 * k_));
    if (const int md = prog_.medalla[e]) {
      const ImVec2 mc(b.x - 22 * k_, a.y + 24 * k_);
      if (md == kPlatino) DibujarAjustada(dl, T3(kMedallaPlatino), ImVec2(mc.x - 22 * k_, mc.y - 22 * k_), ImVec2(mc.x + 22 * k_, mc.y + 22 * k_));
      else Pieza(dl, md == kOro ? "ui_medalla_oro" : md == kPlata ? "ui_medalla_plata" : "ui_medalla_bronce", mc, 44 * k_);
    }
    if (nuevo_esc_ == e && ab) DibujarAjustada(dl, ArteMejoras("rogue/ui_nuevo.png"), ImVec2(a.x - 6 * k_, a.y - 14 * k_), ImVec2(a.x + 64 * k_, a.y + 42 * k_));
    S::MenuTextB3(dl, ImVec2((a.x + b.x) * 0.5f, b.y + 34 * k_ + sube), 36 * k_, Es(de.txt), el, false, cw + hueco - 6 * k_);
  }
  Ayuda(dl, W, H, T::T("Empezar", "Start"), T::T("Volver", "Back"));
}

void Modo::Miniatura(ImDrawList* dl, int e, ImVec2 a, ImVec2 b) {
  const DefEscenario& d = datos_.escenarios[size_t(e)];
  const float w = b.x - a.x, h = b.y - a.y;
  if (const ps2::Tex* m = Arte(("encargo4/mapa_" + d.id + ".png").c_str())) {   // encargo 4: miniatura pintada (glaciar, nave)
    DibujarImagen(dl, m, a, b);
    return;
  }
  dl->AddRectFilled(a, b, Rgb(d.deco.color, 1.0f));
  if (const ps2::Tex* t = Fondo(d.id.c_str(), "suelo"); t && t->w > 0 && t->h > 0) {   // 70 % del ancho del suelo, recortado al alto
    const float uw = 0.7f, vh = std::min(1.0f, uw * float(t->w) / float(t->h) * h / w);
    dl->AddImage(t->id(), a, b, ImVec2(0.15f, 0.5f - vh * 0.5f), ImVec2(0.15f + uw, 0.5f + vh * 0.5f));
  }
  const std::vector<std::string>& ad = d.deco.adornos;
  if (ad.empty()) {   // R23 SURV-7: sin adornos (nave, planeta): sus rompibles repartidos, para que no sea un rectangulo liso
    const int nr = int(d.rompibles.size());
    if (nr == 0) return;
    dl->AddRectFilledMultiColor(a, b, IM_COL32(255, 255, 255, 30), IM_COL32(255, 255, 255, 30), IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 60));
    static constexpr float kRx[4] = {0.18f, 0.50f, 0.82f, 0.34f}, kRy[4] = {0.40f, 0.62f, 0.44f, 0.86f};
    for (int q = 0; q < 4; ++q)
      if (const ps2::Tex* t = RompTex(d.rompibles[size_t((q + e) % nr)], false)) {
        const ImVec2 c(a.x + w * kRx[q], a.y + h * kRy[q]);
        const float r = h * 0.26f;
        DibujarAjustada(dl, t, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
      }
    return;
  }
  dl->PushClipRect(a, b, true);
  static constexpr float kX[6] = {0.10f, 0.30f, 0.52f, 0.72f, 0.90f, 0.40f}, kY[6] = {0.55f, 0.95f, 0.62f, 1.0f, 0.70f, 0.40f};
  static constexpr int kOrden[6] = {5, 0, 2, 4, 1, 3};   // de atras (arriba) adelante
  for (int q = 0; q < 6; ++q) {
    const int k = kOrden[q], i = (k * 5 + e * 3 + 1) % int(ad.size());
    const ps2::Tex* t = Arte(ad[size_t(i)].c_str());
    if (!t || t->w <= 0) continue;
    const float ay = i < int(d.deco.anclas.size()) ? d.deco.anclas[size_t(i)] : 250.0f, esc = h * 0.62f / 256.0f;
    DibujarCuadro(dl, t, 0, 1, ImVec2(128, ay), ImVec2(a.x + w * kX[k], a.y + h * kY[k]), esc, IM_COL32_WHITE, (k & 1) != 0);
  }
  dl->PopClipRect();
}

void Modo::AbrirTienda() {
  p2_.volver_a = uint8_t(pant_);   // de la portada (menu v2) o de la eleccion de luchador
  Ir(Pantalla::Tienda);
  tienda_sel_ = std::clamp(tienda_sel_, 0, std::max(0, int(datos_.mejoras.size()) - 1)), tienda_msg_ = -100;
  Sonar("aceptar", 3, 0);
  REXLOG_INFO("dbz3 survivors: tienda abierta zeni={}", prog_.zeni);
}

void Modo::Tienda(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  const int n = std::min(int(datos_.mejoras.size()), kMejorasPerm);
  if (MenuV2() && Recorrido() && t_pant_ > 3.0f) pad |= kB;
  if (pad & (kB | kY)) {
    Sonar("atras", 5, 0);
    return Ir(Pantalla(p2_.volver_a));
  }
  if (MetaV2() && (pad & kX)) {   // C06: devolver todo = las mejoras vuelven a 0 y los zeni vuelven exactos
    const int dev = DevolverTodo(datos_, &prog_);
    if (dev > 0) {
      GuardarProg();
      Sonar("compra", 3, 0);
      REXLOG_INFO("dbz3 survivors: tienda devuelto zeni={} total={}", dev, prog_.zeni);
      std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, M::L("Devueltos %d zeni.", "Refunded %d zeni.", "%d zeni rendus.", "%d Zeni erstattet.", "Restituiti %d zeni."), dev);
    } else if (dev < 0) {
      std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, "%s", T::T("No cabe el reembolso. Gasta zeni primero.", "Refund exceeds your Zeni limit."));
    } else {
      std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, "%s", M::L("No hay nada que devolver.", "Nothing to refund.", "Rien à rendre.", "Nichts zu erstatten.", "Niente da restituire."));
    }
    p2_.sub_msg_t = t_;
  }
  constexpr int kCols = 6;   // SUR-10: 18 mejoras = 6 x 3
  if (n > 0) {
    if (pad & kIzq) tienda_sel_ = (tienda_sel_ + n - 1) % n, Sonar("mover", 2, 0);
    if (pad & kDer) tienda_sel_ = (tienda_sel_ + 1) % n, Sonar("mover", 2, 0);
    if (pad & (kArriba | kAbajo)) tienda_sel_ = std::min((tienda_sel_ + kCols) % (((n + kCols - 1) / kCols) * kCols), n - 1), Sonar("mover", 2, 0);
    if (pad & kA) {
      const int precio = Precio(datos_, prog_, tienda_sel_);
      tienda_ok_ = Comprar(datos_, tienda_sel_, &prog_), tienda_msg_ = t_;
      if (tienda_ok_) {
        REXLOG_INFO("dbz3 survivors: tienda compra {} nivel={} zeni={} precio={}", datos_.mejoras[size_t(tienda_sel_)].id, prog_.mejoras[tienda_sel_], prog_.zeni, precio);
        GuardarProg();
        Sonar("compra", 3, 0);
      } else {
        REXLOG_INFO("dbz3 survivors: tienda sin comprar {} ({})", datos_.mejoras[size_t(tienda_sel_)].id, precio < 0 ? "al maximo" : "faltan zeni");
        Sonar("bloqueado", 5, 0);
      }
    }
  }
  FondoMenu(fondo, W, H, 0.6f);
  // panel 1400x900 (nueve secciones: 52 px desde el borde del PNG); zona de contenido 60,66 + 1280x768
  const float pw = std::min(W * 0.92f, 1400 * k_ * 0.95f), s = pw / 1400.0f, ph = 900 * s, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f - 12 * k_;
  if (!DibujarNueveTex(dl, T3(kTiendaPanel), 52, 52, 52, 52, ImVec2(px, py), ImVec2(px + pw, py + ph), s))
    dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(8, 30, 50, 224), 12 * k_);
  const float cx = px + 60 * s, cy = py + 66 * s, cw = 1280 * s;
  S::MenuTextB3(dl, ImVec2(cx + cw * 0.5f, cy + 56 * s), 52 * s, T::T("Tienda de mejoras", "Upgrade shop"), true, false, cw * 0.6f);
  ZeniTotal(dl, ImVec2(cx + cw - 10 * s, cy + 56 * s), 40 * s);
  const float paso = cw / float(kCols), lado = 108 * s;
  for (int i = 0; i < n; ++i) {
    const DefMejoraPerm& m = datos_.mejoras[size_t(i)];
    const int nivel = prog_.mejoras[i], precio = Precio(datos_, prog_, i);
    const bool el = i == tienda_sel_, puede = precio >= 0 && prog_.zeni >= precio;
    const int estado = nivel > 0 ? kCasillaComp : puede ? kCasillaDisp : kCasillaBloq;   // comprada = ya tiene nivel (marca)
    const ImVec2 c(cx + paso * (float(i % kCols) + 0.5f), cy + (136.0f + 176.0f * float(i / kCols)) * s);
    const float g = el ? 1.07f + 0.015f * std::sin(t_ * 4.0f) : 1.0f, l = lado * g, e = l / 192.0f;
    if (el) dl->AddRectFilled(ImVec2(c.x - l * 0.56f, c.y - l * 0.56f), ImVec2(c.x + l * 0.56f, c.y + l * 0.56f), IM_COL32(255, 200, 40, 90), 14 * k_);
    DibujarImagen(dl, T3(estado), ImVec2(c.x - l * 0.5f, c.y - l * 0.5f), ImVec2(c.x + l * 0.5f, c.y + l * 0.5f));
    // icono a 112x112 en su icon_rect (40,32) del lienzo de 192: no tapa la marca de comprada
    const ImVec2 ia(c.x - l * 0.5f + 40 * e, c.y - l * 0.5f + 32 * e);
    const ps2::Tex* ico = ArteComercial("tienda", m.id.c_str());
    if (!ico) ico = m.icono.find('/') == std::string::npos ? Icono(m.icono.c_str()) : Arte(m.icono.c_str());
    DibujarAjustada(dl, ico, ia, ImVec2(ia.x + 112 * e, ia.y + 112 * e),
                    IM_COL32(255, 255, 255, estado == kCasillaBloq ? 150 : 255));
    if (T3(kPuntoVacio) && T3(kPuntoLleno) && T3(kPuntoMax)) {   // nivel: 5 puntos (los de las casillas de la partida)
      const float d = 22 * s, y = c.y + lado * 0.5f + 16 * s, x0 = c.x - d * 2.0f;
      for (int k = 0; k < kNivelMax; ++k)
        Pieza3(dl, k >= nivel ? kPuntoVacio : nivel >= kNivelMax ? kPuntoMax : kPuntoLleno, ImVec2(16, 16), ImVec2(x0 + float(k) * d, y), d / 24.0f);
    }
    char buf[32];
    if (precio < 0) std::snprintf(buf, sizeof buf, "MAX");
    else std::snprintf(buf, sizeof buf, "%d", precio);
    const float fs = 28 * s, w = Medir(fs, buf).x, ty = c.y + lado * 0.5f + 36 * s;
    const ImU32 col = precio < 0 ? IM_COL32(255, 150, 255, 255) : puede ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 120, 110, 255);
    Txt(dl, fs, ImVec2(c.x - w * 0.5f + (precio < 0 ? 0 : fs * 0.45f), ty), col, buf);
    if (precio >= 0)
      if (const ps2::Tex* z = T3(kObjeto0 + int(TipoObjeto::Zeni)))
        DibujarCuadro(dl, z, 0, 1, ImVec2(64, 64), ImVec2(c.x - w * 0.5f - fs * 0.15f, ty + fs * 0.55f), fs * 1.1f / 128.0f);
  }
  // la elegida: nombre, efecto y nivel; el resultado de la ultima compra
  if (n > 0) {
    const DefMejoraPerm& m = datos_.mejoras[size_t(tienda_sel_)];
    const float dy = cy + 640 * s;
    const bool msg = t_ - tienda_msg_ < 1.6f;
    S::MenuTextB3(dl, ImVec2(cx + cw * 0.5f, dy), 40 * s, Es(m.txt), true, false, cw * 0.8f);
    TxtC(dl, 28 * s, ImVec2(cx + cw * 0.5f, dy + 42 * s), IM_COL32(230, 235, 250, 255), Desc(m.txt));
    char buf[128];
    if (msg)
      std::snprintf(buf, sizeof buf, "%s",
                    tienda_ok_ ? T::T("¡Comprado!", "Bought!") : Precio(datos_, prog_, tienda_sel_) < 0 ? T::T("Ya está al máximo.", "Already maxed.") : T::T("Te faltan zeni.", "Not enough zeni."));
    else
      std::snprintf(buf, sizeof buf, T::T("Nivel %d de %d", "Level %d of %d"), prog_.mejoras[tienda_sel_], kNivelMax);
    TxtC(dl, 28 * s, ImVec2(cx + cw * 0.5f, dy + 80 * s), msg ? (tienda_ok_ ? IM_COL32(120, 255, 160, 255) : IM_COL32(255, 140, 120, 255)) : IM_COL32(255, 230, 140, 255), buf);
  }
  if (MetaV2()) {
    char g[64];
    std::snprintf(g, sizeof g, "%s %lld", M::L("Invertido:", "Invested:", "Investi :", "Investiert:", "Investito:"), static_cast<long long>(TotalGastado(datos_, prog_)));
    Txt(dl, 24 * s, ImVec2(cx + 10 * s, cy + 18 * s), IM_COL32(200, 210, 240, 255), g);
    if (t_ - p2_.sub_msg_t < 2.2f) TxtC(dl, 30 * s, ImVec2(cx + cw * 0.5f, cy + 110 * s), IM_COL32(120, 255, 160, 255), p2_.sub_msg);
    return S::HelpLine(dl, ImVec2(W * 0.5f, H - 52 * k_), 34 * k_, {{"A", T::T("Comprar", "Buy")}, {"X", M::L("Devolver todo", "Refund all", "Tout rendre", "Alles erstatten", "Restituisci tutto")}, {"B", T::T("Volver", "Back")}});
  }
  Ayuda(dl, W, H, T::T("Comprar", "Buy"), T::T("Volver", "Back"));
}

void Modo::Ficha(ImDrawList* dl, const char* icono, ImVec2 a, float lado, int nivel, bool evo, bool max, float recarga) {
  if (const ps2::Tex* ra = evo ? nullptr : ArteMejoras("gpt3/survivors_hud_ranura_tecnica.png"))   // encargo 3: ranura de 96x96
    DibujarImagen(dl, ra, a, ImVec2(a.x + lado, a.y + lado));
  else
    Pieza(dl, evo ? "ui_casilla_mejora_resaltada" : "ui_casilla_mejora", ImVec2(a.x + lado * 0.5f, a.y + lado * 0.5f), lado);
  IconoEn(dl, icono, ImVec2(a.x + lado * 0.14f, a.y + lado * 0.14f), ImVec2(a.x + lado * 0.86f, a.y + lado * 0.86f));
  // tanda 3: velo de recarga (la parte que falta, de arriba abajo) y puntos de nivel (vacio / lleno / maximo) en vez de la cifra
  if (const ps2::Tex* velo = recarga > 0.01f ? T3(kVelo) : nullptr)
    dl->AddImage(velo->id(), a, ImVec2(a.x + lado, a.y + lado * recarga), ImVec2(0, 0), ImVec2(1, recarga), IM_COL32(255, 255, 255, 220));
  if (T3(kPuntoVacio) && T3(kPuntoLleno) && T3(kPuntoMax)) {
    const float d = lado * 0.17f, y = a.y + lado - d * 0.5f, x0 = a.x + lado * 0.5f - d * 2.0f;   // tinta de 24 px en un lienzo de 32
    for (int i = 0; i < kNivelMax; ++i)
      Pieza3(dl, i >= nivel ? kPuntoVacio : (max || evo) ? kPuntoMax : kPuntoLleno, ImVec2(16, 16), ImVec2(x0 + float(i) * d, y), d / 24.0f);
    return;
  }
  char n[8] = "EVO";
  if (!evo) std::snprintf(n, sizeof n, max ? "MAX" : "%d", nivel);
  const float fs = lado * (evo || max ? 0.26f : 0.34f);
  const ImVec2 m = Medir(fs, n);
  const ImVec2 c(a.x + lado - m.x * 0.5f - lado * 0.08f, a.y + lado - m.y * 0.5f - lado * 0.04f);
  dl->AddRectFilled(ImVec2(c.x - m.x * 0.5f - 4 * k_, c.y - m.y * 0.5f), ImVec2(c.x + m.x * 0.5f + 4 * k_, c.y + m.y * 0.5f), IM_COL32(8, 12, 36, 220), 6 * k_);
  TxtC(dl, fs, c, evo ? IM_COL32(255, 120, 255, 255) : IM_COL32(255, 215, 60, 255), n);
}

void Modo::Marcador(ImDrawList* dl, float W, float H, float dt) {
  medidas::Tramo tramo{medidas::Hud};
  const Juego& j = *j_;
  const Jugador& p = j.jugador;
  const float m = 16 * k_;
  char buf[64];
  if (j.estado == Estado::Jugando && p.vida > 0) {   // R23 SUR-5: marca amarilla sobre la cabeza del jugador: no se pierde entre la horda y los efectos
    const ImVec2 q = vista_.P(p.x, p.y);
    const float by = q.y - vista_.S * kAlto * 1.45f - (8 + 4 * std::sin(t_ * 6.0f)) * k_, r = 26 * k_;
    const ImVec2 tri[3] = {{q.x - r, by - r * 1.3f}, {q.x + r, by - r * 1.3f}, {q.x, by}};
    dl->AddConvexPolyFilled(tri, 3, IM_COL32(255, 215, 40, 235));
    dl->AddPolyline(tri, 3, IM_COL32(40, 20, 0, 230), ImDrawFlags_Closed, 2.5f * k_);
  }
  // experiencia arriba de lado a lado (ui_barra_exp 1920 x 32)
  const float ew = W - 2 * m, eh = std::min(ew * 32.0f / 1920.0f, 32 * k_);
  const float pct_exp = p.exp_sig > 0 ? std::clamp(p.exp / p.exp_sig, 0.0f, 1.0f) : 0.0f;
  const ps2::Tex *bm = MetaV2() ? Arte5("barra_partida_marco.png") : nullptr, *br = MetaV2() ? Arte5("barra_partida_relleno.png") : nullptr;
  if (bm && br && W >= 1156 * k_) {   // tanda 5: barra de experiencia de GPT (1280x64; relleno en x 40..1240)
    const float bw = 1000 * k_, sc = bw / 1280.0f, bx = (W - bw) * 0.5f, by = 2 * k_;
    const float u = (40.0f + 1200.0f * pct_exp) / 1280.0f;
    // R23 SURV-6: el marco va DEBAJO: su canal es un 78 % opaco y tapaba el relleno (la barra salia siempre vacia)
    dl->AddImage(bm->id(), ImVec2(bx, by), ImVec2(bx + 1280 * sc, by + 64 * sc));
    dl->AddImage(br->id(), ImVec2(bx, by), ImVec2(bx + 1280 * sc * u, by + 64 * sc), ImVec2(0, 0), ImVec2(u, 1));
    TxtC(dl, 26 * k_, ImVec2(bx - 44 * k_, by + 32 * sc), IM_COL32(255, 230, 150, 255), T::T("EXP", "XP"));   // R23 SURV-8: rotulo de la barra
  } else {
    DibujarBarra(dl, "exp", ImVec2(m, m * 0.5f), ImVec2(m + ew, m * 0.5f + eh), pct_exp);
  }
  // nivel (insignia) y vida
  const float ni = 96 * k_, y1 = m + eh + 8 * k_;
  Pieza(dl, "ui_insignia_nivel", ImVec2(m + ni * 0.5f, y1 + ni * 0.5f), ni);
  std::snprintf(buf, sizeof buf, "%d", p.nivel);
  TxtC(dl, 40 * k_, ImVec2(m + ni * 0.5f, y1 + ni * 0.52f), IM_COL32(255, 230, 120, 255), buf);
  const float vw = 400 * k_, vh = vw / 8.0f, vx = m + ni + 6 * k_, vy = y1 + (ni - vh) * 0.5f;
  DibujarBarra(dl, "vida", ImVec2(vx, vy), ImVec2(vx + vw, vy + vh), p.vida_max > 0 ? p.vida / p.vida_max : 0);
  std::snprintf(buf, sizeof buf, "%.0f / %.0f", std::max(0.0f, std::ceil(p.vida)), p.vida_max);
  TxtC(dl, 33 * k_, ImVec2(vx + vw * 0.5f, vy + vh * 0.5f), IM_COL32_WHITE, buf);
  TxtC(dl, 24 * k_, ImVec2(vx + 44 * k_, vy + vh * 0.5f), IM_COL32(255, 200, 190, 255), T::T("Vida", "HP"));   // R23 SURV-8: rotulo de la barra   // R23 SURV-7: mas letra
  // tiempo (centro arriba): reloj de la linea de 15 minutos
  Reloj15(buf, sizeof buf, j.Minuto());
  const float tf = 52 * k_, tw = Medir(tf, buf).x, ti = 64 * k_, tx = W * 0.5f - (ti + 8 * k_ + tw) * 0.5f, ty = y1 + ni * 0.5f;
  if (const ps2::Tex* placa = ArteMejoras("gpt3/survivors_hud_reloj_placa.png")) {   // encargo 3: placa 320x96, hueco del tiempo en (48,24)+224x48
    const float sp = 1.15f * k_, pw = 320 * sp, ph = 96 * sp, cxp = W * 0.5f;
    DibujarImagen(dl, placa, ImVec2(cxp - pw * 0.5f, ty - ph * 0.5f), ImVec2(cxp + pw * 0.5f, ty + ph * 0.5f));
    const float tf2 = tf * 0.9f;
    Txt(dl, tf2, ImVec2(cxp - Medir(tf2, buf).x * 0.5f, ty - tf2 * 0.55f), IM_COL32_WHITE, buf);
  } else {
    Pieza(dl, "ui_contador_tiempo", ImVec2(tx + ti * 0.5f, ty), ti);
    Txt(dl, tf, ImVec2(tx + ti + 8 * k_, ty - tf * 0.55f), IM_COL32_WHITE, buf);
  }
  // tanda 4: minimapa arriba a la derecha; debajo, derrotados (antes arriba), jefes derrotados y racha
  const auto tmm = Reloj::now();
  const float mm = 190 * k_, mmy = y1;
  Minimapa(dl, ImVec2(W - m - mm * 0.5f, mmy + mm * 0.5f), mm);
  mini_suma_ += Ms(tmm, Reloj::now());
  float cy = mmy + mm + 44 * k_;   // columna de la derecha (R23 SURV-6: aire bajo el minimapa)
  std::snprintf(buf, sizeof buf, "%d", j.derrotados);
  const float df = 40 * k_, dw = Medir(df, buf).x, di = 60 * k_;
  Pieza(dl, "ui_contador_muertes", ImVec2(W - m - dw - 8 * k_ - di * 0.5f, cy), di);
  Txt(dl, df, ImVec2(W - m - dw, cy - df * 0.55f), IM_COL32_WHITE, buf);
  {   // R23 SURV-7: etiqueta bajo el contador (sin ella no se sabia que contaba; dos rondas)
    const char* et = M::L("Derrotados", "Defeated", "Vaincus", "Besiegt", "Sconfitti");
    Txt(dl, 24 * k_, ImVec2(W - m - Medir(24 * k_, et).x, cy + 24 * k_), IM_COL32(215, 222, 245, 235), et);
  }
  if (j.jefes_derrotados > 0) {   // jefes derrotados en esta partida, con el craneo (R23 SURV-6: solo si hay alguno; el "0" suelto no se entendia)
    std::snprintf(buf, sizeof buf, "%d", j.jefes_derrotados);
    const float cw2 = Medir(df, buf).x, x = W - m - dw - 8 * k_ - di - 26 * k_;
    Txt(dl, df, ImVec2(x - cw2, cy - df * 0.55f), j.jefes_derrotados ? IM_COL32(255, 230, 120, 255) : IM_COL32_WHITE, buf);
    if (const ps2::Tex* cr = T3(kCraneo)) DibujarCuadro(dl, cr, 0, 1, ImVec2(64, 64), ImVec2(x - cw2 - 8 * k_ - 26 * k_, cy), 52 * k_ / 120.0f);
  }
  cy += 84 * k_;
  // racha: marco de 384x128 (nueve secciones de 28; texto en 24,29 + 336x70); se apaga a los racha.dura s sin derrotar
  if (j.racha >= 5 && j.racha != j.derrotados) {   // R23 SURV-6: si la racha es igual a los derrotados no aporta nada
    const float a = std::clamp(1.0f - j.racha_t / std::max(0.1f, datos_.racha.dura), 0.0f, 1.0f), rw = 240 * k_, s = rw / 384.0f, rh = 128 * s;
    const ImVec2 ra(W - m - rw, cy), rb(W - m, cy + rh);
    DibujarNueveTex(dl, T3(kRachaMarco), 28, 28, 28, 28, ra, rb, s, IM_COL32(255, 255, 255, int(255 * (0.45f + 0.55f * a))));
    std::snprintf(buf, sizeof buf, "%s x%d", T::T("Racha", "Streak"), j.racha);
    const float zx = ra.x + 24 * s, zy = ra.y + 29 * s, zw = 336 * s, zh = 70 * s;
    S::MenuTextB3(dl, ImVec2(zx + zw * 0.5f, zy + zh * 0.5f), zh * 0.62f, buf, j.racha >= 25, false, zw);
    dl->AddRectFilled(ImVec2(zx, zy + zh + 2 * s), ImVec2(zx + zw * a, zy + zh + 9 * s), IM_COL32(255, 210, 80, 220), 3 * s);   // tiempo que le queda
  }
  // jefe: nombre y barra bajo el reloj; letrero al aparecer
  const Jefe& b = j.jefe;
  const float pct_jefe = std::clamp(b.vida / std::max(1.0f, b.vida_max), 0.0f, 1.0f);
  const ps2::Tex *marco = T3(kJefeMarco), *relleno = T3(kJefeRelleno);
  const ps2::Tex *jm = ArteMejoras("gpt3/survivors_hud_jefe_barra_marco.png"), *jr = ArteMejoras("gpt3/survivors_hud_jefe_barra_relleno.png");
  const bool barra = pres_t_ < 0;   // tanda 4: la barra del jefe sale cuando acaba su presentacion (no se pisan)
  if (barra && b.estado != EstadoJefe::Ausente && b.estado != EstadoJefe::Muerto && jm && jr) {
    // encargo 3: marco y relleno de 1000x72 con el mismo origen; canal [30,20, 940x32]: el relleno se recorta por porcentaje y el marco va encima
    const float bw = 820 * k_, s = bw / 1000.0f, bx = (W - bw) * 0.5f, by = y1 + ni + 40 * k_;   // R23 SURV-6: mas corta y separada del reloj
    const float u = (30.0f + 940.0f * pct_jefe) / 1000.0f;
    {   // R23 SURV-7: placa del bloque del jefe con el filete bronce de los paneles (el panel entero dejaba sus pestanas sueltas)
      const ImVec2 qa(bx - 104 * k_, by - 20 * k_), qb(bx + bw + 20 * k_, by + 72 * s + 16 * k_);
      dl->AddRectFilled(qa, qb, IM_COL32(10, 30, 44, 225), 6 * k_);
      dl->AddRect(qa, qb, IM_COL32(20, 12, 6, 255), 6 * k_, 0, 5 * k_);
      dl->AddRect(qa, qb, IM_COL32(214, 128, 44, 255), 6 * k_, 0, 2.5f * k_);
    }
    {   // R23 SUR-5: la cara del jefe (marcador de B3) a la izquierda de la barra
      const ImVec2 ca(bx - 90 * k_, by - 10 * k_), cb(bx - 10 * k_, by + 70 * k_);
      if (CaraB3(dl, B3Jefe(), ca, cb)) MarcoCara(dl, ca, cb, k_);
    }
    dl->AddImage(jm->id(), ImVec2(bx, by), ImVec2(bx + 1000 * s, by + 72 * s));   // R23 SURV-6: marco debajo (su canal opaco tapaba la vida)
    dl->AddImage(jr->id(), ImVec2(bx, by), ImVec2(bx + 1000 * s * u, by + 72 * s), ImVec2(0, 0), ImVec2(u, 1));
    if (const int nf = j.NFases(); nf > 1)   // SUR-8: marcas de fase en la barra (66/33 %, o 50 %): la ya pasada, apagada
      for (int f = 1; f < nf; ++f) {
        const float um = j.UmbralFase_(f), xm = bx + (30.0f + 940.0f * um) * s;
        dl->AddLine(ImVec2(xm, by + 12 * s), ImVec2(xm, by + 60 * s), b.fase > f ? IM_COL32(120, 110, 90, 200) : IM_COL32(255, 235, 170, 255), std::max(2.0f, 3 * s));
      }
    char nb[96];
    std::snprintf(nb, sizeof nb, "%s   %.0f%%", Es(j.DJ().txt), double(pct_jefe * 100));
    TxtC(dl, 34 * k_, ImVec2(W * 0.5f, by + 36 * s), IM_COL32(255, 235, 200, 255), nb);   // R23 SURV-7: 28 -> 34
  } else if (barra && b.estado != EstadoJefe::Ausente && b.estado != EstadoJefe::Muerto && marco && relleno) {
    // tanda 3: marco y relleno de 1024x64 con el mismo origen; relleno recortado por porcentaje en [12, 1012) x [35, 55) y el marco encima.
    // La placa del nombre (252,4 + 520x24) se repinta a 1,6x sobre la suya (centrada, misma base) para que el nombre se lea.
    const float bw = 1000 * k_, s = bw / 1024.0f, bx = (W - bw) * 0.5f, by = y1 + ni + 26 * k_;
    const float u = (12.0f + 1000.0f * pct_jefe) / 1024.0f;
    dl->AddImage(relleno->id(), ImVec2(bx, by), ImVec2(bx + 1024 * s * u, by + 64 * s), ImVec2(0, 0), ImVec2(u, 1));
    dl->AddImage(marco->id(), ImVec2(bx, by), ImVec2(bx + 1024 * s, by + 64 * s));
    const float ps = s * 1.6f, pw = 520 * ps, ph = 24 * ps, pcx = W * 0.5f, pby = by + 28 * s;
    dl->AddImage(marco->id(), ImVec2(pcx - pw * 0.5f, pby - ph), ImVec2(pcx + pw * 0.5f, pby), ImVec2(252.0f / 1024, 4.0f / 64), ImVec2(772.0f / 1024, 28.0f / 64));
    TxtC(dl, 16 * ps * 1.05f, ImVec2(pcx, pby - ph * 0.5f), IM_COL32(255, 235, 200, 255), Es(j.DJ().txt));   // zona del nombre (258,8 + 508x16)
  } else if (barra && b.estado != EstadoJefe::Ausente && b.estado != EstadoJefe::Muerto) {   // sin el arte: la barra de vida de la tanda 2
    const float bw = 720 * k_, bh = bw / 8.0f, by = y1 + ni + 10 * k_;
    S::MenuTextB3(dl, ImVec2(W * 0.5f, by + 4 * k_), 30 * k_, Es(j.DJ().txt), true, false);
    DibujarBarra(dl, "vida", ImVec2((W - bw) * 0.5f, by + 22 * k_), ImVec2((W + bw) * 0.5f, by + 22 * k_ + bh), pct_jefe, IM_COL32(255, 120, 255, 255));
  }
  // flechas en el borde hacia el jefe y los cofres que quedan fuera de la pantalla
  // (las de cofre: solo las 3 mas cercanas al jugador)
  if (b.estado != EstadoJefe::Ausente && b.estado != EstadoJefe::Muerto)
    if (ImVec2 q; Flecha(dl, kFlechaJefe, vista_.P(b.x, b.y), W, H, vista_.S * 0.6f * j.DJ().escala, vista_.S * kAlto * j.DJ().escala, &q)) {
      const ImVec2 c(W * 0.5f, H * 0.55f), d(c.x - q.x, c.y - q.y);   // R23 SURV-7: la cara del jefe junto a su flecha, hacia dentro
      const float l = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y)), r = 34 * k_;
      const ImVec2 fc(q.x + d.x / l * 78 * k_, q.y + d.y / l * 78 * k_);
      if (CaraB3(dl, B3Jefe(), ImVec2(fc.x - r, fc.y - r), ImVec2(fc.x + r, fc.y + r))) MarcoCara(dl, ImVec2(fc.x - r, fc.y - r), ImVec2(fc.x + r, fc.y + r), k_ * 0.7f);
    }
  int cofres[kMaxObjetos], nc = 0;
  for (int i = 0; i < j.nObjetos; ++i)
    if (j.objetos[i].tipo == TipoObjeto::Cofre) cofres[nc++] = i;
  const auto d2 = [&](int i) { const float dx = j.objetos[i].x - p.x, dy = j.objetos[i].y - p.y; return dx * dx + dy * dy; };
  std::sort(cofres, cofres + nc, [&](int x, int y) { return d2(x) < d2(y); });
  for (int c = 0, puestas = 0; c < nc && puestas < 3; ++c)   // los de fuera de la pantalla mas cercanos
    puestas += Flecha(dl, kFlechaCofre, vista_.P(j.objetos[cofres[c]].x, j.objetos[cofres[c]].y), W, H, vista_.S * 0.7f, vista_.S * 1.4f);
  // tanda 4: elites (las 2 mas cercanas fuera de la pantalla) y objetos especiales (capsula, iman, bomba, reloj: los 2 mas cercanos)
  {
    int cerca[2] = {-1, -1};
    float dc[2] = {1e30f, 1e30f};
    for (int i = 0, n = j.NumEnemigos(); i < n; ++i) {
      const VistaEnemigo e = j.Enemigo(i);
      if (e.anillo <= 0 || (b.estado != EstadoJefe::Ausente && b.estado != EstadoJefe::Muerto)) continue;   // R23 SURV-7: con jefe, solo su flecha
      const ImVec2 q = vista_.P(e.x, e.y);
      if (q.x >= 0 && q.x <= W && q.y >= 0 && q.y <= H) continue;
      const float d = (e.x - p.x) * (e.x - p.x) + (e.y - p.y) * (e.y - p.y);
      if (d < dc[0]) cerca[1] = cerca[0], dc[1] = dc[0], cerca[0] = i, dc[0] = d;
      else if (d < dc[1]) cerca[1] = i, dc[1] = d;
    }
    for (const int i : cerca)
      if (i >= 0) {
        const VistaEnemigo e = j.Enemigo(i);
        Flecha(dl, kFlechaElite, vista_.P(e.x, e.y), W, H, vista_.S * 0.6f * e.escala, vista_.S * kAlto * e.escala);
      }
    int esp[kMaxObjetos], ne = 0;
    for (int i = 0; i < j.nObjetos; ++i)
      if (j.objetos[i].tipo <= TipoObjeto::Reloj && (b.estado == EstadoJefe::Ausente || b.estado == EstadoJefe::Muerto)) esp[ne++] = i;   // R23 SURV-7: con jefe, solo su flecha (la de arriba caia sobre su cabeza)
    std::sort(esp, esp + ne, [&](int x, int y) { return d2(x) < d2(y); });
    for (int c = 0, puestas = 0; c < ne && puestas < 2; ++c)
      puestas += Flecha(dl, kFlechaEspecial, vista_.P(j.objetos[esp[c]].x, j.objetos[esp[c]].y), W, H, vista_.S * 0.6f, vista_.S * 1.2f);
  }
  // tanda 4: presentacion del jefe (el retrato 3D ya esta debajo, en la lista de fondo: Mundo)
  if (pres_t_ >= 0) {
    pres_t_ += pausa_ ? 0 : dt;
    Pres pr;
    if (PresGeom(W, H, &pr)) {
      const ImU32 col = IM_COL32(255, 255, 255, int(255 * pr.a));
      const ps2::Tex* placa = MetaV2() ? Arte5("jefe_intro_placa_nombre.png") : nullptr;   // tanda 5: placa de GPT (1200x220) en lugar del rotulo de la tanda 4
      if (placa) {
        DibujarCuadro(dl, placa, 0, 1, ImVec2(600, 110), pr.rc, pr.rs * 1.05f, col);
        {   // R23 SURV-6: la cara de B3 del jefe dentro del circulo de la placa (salia vacio). Circulo de la placa: centro (100,110), radio ~88 de 1200x220
          const float sr = pr.rs * 1.05f, r = 88 * sr;   // el craneo de jefe del juego (el busto ya esta en el marco cuadrado)
          const ImVec2 cc(pr.rc.x + (100 - 600) * sr, pr.rc.y);
          dl->AddCircleFilled(cc, r, IM_COL32(60, 10, 14, int(230 * pr.a)), 40);
          if (const ps2::Tex* cr = T3(kCraneo)) DibujarCuadro(dl, cr, 0, 1, ImVec2(64, 64), cc, r * 1.5f / 120.0f, col);
        }
        {   // R23 SURV-7: retrato de B3 del jefe en la zona segura del marco (32,32 + 192x192 de 256)
          const float z = 96.0f * pr.ps;
          CaraB3(dl, B3Jefe(), ImVec2(pr.pc.x - z, pr.pc.y - z), ImVec2(pr.pc.x + z, pr.pc.y + z), col, true);
        }
        DibujarCuadro(dl, T3(kMarcoRetrato), 0, 1, ImVec2(128, 128), pr.pc, pr.ps, col);
        S::MenuTextB3(dl, ImVec2(pr.rc.x, pr.rc.y), 150 * pr.rs * 0.62f, Es(j.DJ().txt), true, false, 800 * pr.rs);
      } else {
      DibujarCuadro(dl, T3(kRotuloJefe), 0, 1, ImVec2(640, 180), pr.rc, pr.rs, col);
      DibujarCuadro(dl, T3(kMarcoRetrato), 0, 1, ImVec2(128, 128), pr.pc, pr.ps, col);
      // nombre con la letra del juego en su zona (176,110 + 928x170; centro 640,195 del lienzo con ancla 640,180)
      S::MenuTextB3(dl, ImVec2(pr.rc.x, pr.rc.y + 15 * pr.rs), 170 * pr.rs * 0.6f, Es(j.DJ().txt), true, false, 928 * pr.rs);
      }
    } else {
      pres_t_ = -1;
    }
  }
  // rotulo de aviso (cada minuto y al salir el jefe): arte de la tanda 3 con el texto del motor en su zona (40,32 + 944x96 de 1024x160)
  if (aviso_jefe_ >= 0 && !(MetaV2() && j.estado == Estado::Eligiendo)) {   // v2: en la subida / el cofre el aviso espera su turno
    aviso_jefe_ += pausa_ ? 0 : dt;
    const float a = std::min(1.0f, std::min(aviso_jefe_ * 4.0f, (3.0f - aviso_jefe_) * 2.0f));
    if (a > 0) {
      // SUR-04: el rotulo del jefe (transformacion) va mas pequeno: debajo de la barra y sin tapar la cabeza del jefe
      const float aw = 1000 * k_ * (0.92f + 0.08f * a) * (aviso_rojo_ ? 0.46f : 0.52f);
      // R23 SURV-6: el de oleada, pegado bajo el reloj (o bajo la barra del jefe si esta), estrecho: casi no pisa la accion
      const bool hay_barra = pres_t_ < 0 && j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto;
      // R23 SURV-7: el rojo del jefe va pegado bajo su barra (un solo bloque de HUD; antes flotaba entre el jefe y el jugador) y el de
      // oleada es una linea de texto bajo el reloj, sin caja (en cuatro rondas se dijo que tapaba la accion)
      const float cy = aviso_rojo_ ? (hay_barra ? y1 + ni + 196 * k_ : y1 + ni + 60 * k_) : (hay_barra ? y1 + ni + 156 * k_ : y1 + ni * 0.5f + 82 * k_);
      const ImU32 col = IM_COL32(255, 255, 255, int(255 * a));
      const ps2::Tex* roja = aviso_rojo_ ? T3(kRotuloDerrota) : nullptr;   // R23 SURV-6: el del jefe con las cintas rojas del rotulo de derrota
      if (roja) {
        const float sr = aw * 1.15f / 1024.0f;
        DibujarCuadro(dl, roja, 0, 1, ImVec2(512, 124), ImVec2(W * 0.5f, cy), sr, col);
        S::MenuTextB3(dl, ImVec2(W * 0.5f, cy), 152 * sr * 0.5f, aviso_txt_, true, false, 824 * sr);
      } else {
        const float tw = std::min(900 * k_, Medir(46 * k_, aviso_txt_).x) * 0.5f + 34 * k_;   // R23 SURV-7: pastilla oscura detras
        dl->AddRectFilled(ImVec2(W * 0.5f - tw, cy - 28 * k_), ImVec2(W * 0.5f + tw, cy + 28 * k_), IM_COL32(6, 14, 28, int(190 * a)), 28 * k_);
        dl->AddRect(ImVec2(W * 0.5f - tw, cy - 28 * k_), ImVec2(W * 0.5f + tw, cy + 28 * k_), IM_COL32(214, 128, 44, int(200 * a)), 28 * k_, 0, 2 * k_);
        S::MenuTextB3(dl, ImVec2(W * 0.5f, cy), 46 * k_, aviso_txt_, aviso_rojo_, false, 900 * k_);
      }
    } else if (aviso_jefe_ > 3.0f) {
      aviso_jefe_ = -1;
    }
  }
  // tecnicas (abajo a la izquierda) y pasivas (abajo a la derecha)
  const float lado = 104 * k_, paso = lado + 8 * k_, fy = H - m - lado;   // R23 SURV-7: 92 -> 104
  for (int i = 0; i < j.nTecnicas; ++i) {
    const Poseida& t = j.tecnicas[i];
    if (t.def < 0 || t.def >= int(datos_.tecnicas.size())) continue;
    const DefTecnica& d = datos_.tecnicas[size_t(t.def)];
    Ficha(dl, d.icono.c_str(), ImVec2(m + i * paso, fy), lado, t.nivel, d.evoluciona_de >= 0, t.nivel >= kNivelMax, j.Recarga01(i));
  }
  for (int i = 0; i < j.nPasivas; ++i) {
    const Poseida& t = j.pasivas[i];
    if (t.def < 0 || t.def >= int(datos_.pasivas.size())) continue;
    Ficha(dl, datos_.pasivas[size_t(t.def)].icono.c_str(), ImVec2(W - m - lado - i * paso, fy), lado, t.nivel, false, t.nivel >= kNivelMax);
  }
  // linea de ayuda de la partida: abajo en el centro, entre las casillas de tecnicas y las de pasivas (no tapa al jugador)
  if (!fin_ && !pausa_ && j.estado == Estado::Jugando && j.tiempo < 12.0f)   // R23 SURV-6: solo al empezar (luego esta en la pausa)
    TxtC(dl, 26 * k_, ImVec2(W * 0.5f, H - m - 14 * k_), IM_COL32(230, 235, 250, 190),
         T::T("Start: pausa    Zoom: rueda / + - / palanca derecha", "Start: pause    Zoom: wheel / + - / right stick"));
  // dano recibido: borde rojo
  flash_ = std::max(0.0f, flash_ - dt * 3.0f);
  if (flash_ > 0) dl->AddRect(ImVec2(0, 0), ImVec2(W, H), IM_COL32(255, 30, 20, int(150 * flash_)), 0, 0, 26 * k_);
  if (p.vida > 0 && p.vida < p.vida_max * 0.25f)   // poca vida: latido
    dl->AddRect(ImVec2(0, 0), ImVec2(W, H), IM_COL32(200, 0, 0, int(60 + 50 * std::sin(t_ * 6.0f))), 0, 0, 14 * k_);
  if (MetaV2() && j.config.meta_v2) AvisoLogro(dl, W, k_, dt, !pausa_ && j.estado == Estado::Jugando, p2_);
}

void Modo::Subida(ImDrawList* dl, float W, float H) {
  const Juego& j = *j_;
  if (MetaV2() && j.config.meta_v2) {   // A10 / A11: tarjetas por rareza, re-sortear / saltar / desterrar y el cofre como espectaculo
    const Ctx c{nullptr, dl, W, H, k_, t_, t_opc_, datos_, j, p2_, prog_, op_sel_};
    return j.CofreAbierto() ? CofreV2(c) : SubidaV2(c);
  }
  const float en = Suave(t_opc_ / 0.25f);
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 10, int(165 * en)));
  S::MenuTextB3(dl, ImVec2(W * 0.5f, 120 * k_ - (1 - en) * 40 * k_), 70 * k_,
                eleccion_cofre_ ? T::T("¡Cofre! Mejora gratis", "Chest! Free upgrade") : T::T("¡Subes de nivel!", "Level up!"), true, false);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s %d", T::T("Nivel", "Level"), j.jugador.nivel);
  TxtC(dl, 34 * k_, ImVec2(W * 0.5f, 185 * k_), IM_COL32(255, 230, 140, int(255 * en)), buf);
  const int n = j.nOpciones;
  const float h = 560 * k_, w = h * 400.0f / 560.0f, hueco = 40 * k_, x0 = (W - (n * w + (n - 1) * hueco)) * 0.5f, y0 = H * 0.5f - h * 0.5f + 40 * k_;
  for (int i = 0; i < n; ++i) {
    const Opcion& o = j.opciones[i];
    const float a = Suave((t_opc_ - 0.08f * float(i)) / 0.3f);
    if (a <= 0) continue;
    const bool el = i == op_sel_;
    const float g = el ? 1.04f + 0.01f * std::sin(t_ * 4.0f) : 1.0f;
    const ImVec2 c(x0 + i * (w + hueco) + w * 0.5f, y0 + h * 0.5f + (1 - a) * 140 * k_);
    const ImVec2 p0(c.x - w * 0.5f * g, c.y - h * 0.5f * g), p1(c.x + w * 0.5f * g, c.y + h * 0.5f * g);
    const float cw = p1.x - p0.x, ch = p1.y - p0.y;
    const int al = int(255 * a);
    if (el) dl->AddRectFilled(ImVec2(p0.x - 8 * k_, p0.y - 8 * k_), ImVec2(p1.x + 8 * k_, p1.y + 8 * k_), IM_COL32(255, 200, 40, int(60 * a)), 22 * k_);
    DibujarImagen(dl, Ui(el ? "ui_tarjeta_resaltada" : "ui_tarjeta_normal"), p0, p1, IM_COL32(255, 255, 255, al));
    // tanda 2 del arte: icono en la zona cuadrada de la tarjeta (108,54 + 184x184 de 400x560) y texto en (36,286 + 328x238)
    const float ic = ch * 0.30f;
    const ImVec2 ci(c.x, p0.y + ch * 0.261f);
    IconoEn(dl, j.Icono(o), ImVec2(ci.x - ic * 0.5f, ci.y - ic * 0.5f), ImVec2(ci.x + ic * 0.5f, ci.y + ic * 0.5f), IM_COL32(255, 255, 255, al));
    // nombre, nivel y descripcion debajo de la linea (0,36 del alto)
    const Textos& tx = j.Texto(o);
    S::MenuTextB3(dl, ImVec2(c.x, p0.y + ch * 0.555f), 34 * k_, Es(tx), el, false, cw * 0.80f);
    const char* etiqueta = nullptr;
    ImU32 ce = IM_COL32(120, 200, 255, al);
    if (o.tipo == TipoOpcion::Evolucion) etiqueta = T::T("¡Evolución!", "Evolution!"), ce = IM_COL32(255, 120, 255, al);
    else if (o.tipo != TipoOpcion::Curar && o.nivel <= 1) etiqueta = T::T("¡Nueva!", "New!"), ce = IM_COL32(120, 255, 160, al);
    else if (o.tipo != TipoOpcion::Curar) std::snprintf(buf, sizeof buf, T::T("Nv. %d", "Lv. %d"), o.nivel), etiqueta = buf;
    if (etiqueta) TxtC(dl, 28 * k_, ImVec2(c.x, p0.y + ch * 0.625f), ce, etiqueta);
    const char* desc = Desc(tx);
    const float fs = 24 * k_, ancho = cw * 0.8f;
    Txt(dl, fs, ImVec2(p0.x + cw * 0.1f, p0.y + ch * 0.69f), IM_COL32(225, 230, 245, al), desc, ancho);
  }
  Ayuda(dl, W, H, T::T("Elegir", "Choose"), nullptr);
}

void Modo::Pausa(ImDrawList* dl, float W, float H) {
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 10, 150));
  // tanda 4: panel de pausa (900x700, nueve secciones de 52 desde el borde del PNG; contenido 52,57 + 795x587)
  const float pw = 760 * k_, s = pw / 900.0f, ph = 700 * s, x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
  if (!DibujarNueveTex(dl, T3(kPanelPausa), 52, 52, 52, 52, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), s))
    DibujarNueve(dl, "ui_ventana_dialogo", ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), 0.6f * k_);
  const float cy = y0 + 57 * s, cx = W * 0.5f;
  S::MenuTextB3(dl, ImVec2(cx, cy + 50 * s), 64 * s, T::T("Pausa", "Pause"), true, false);
  const Juego& j = *j_;
  char buf[160], reloj[16];
  Reloj15(reloj, sizeof reloj, j.Minuto());
  std::snprintf(buf, sizeof buf, "%s%s  ·  %s  ·  %s %d", Es(j.Escenario().txt), j.config.dificultad ? T::T(" (difícil)", " (hard)") : "", reloj,
                T::T("Nivel", "Level"), j.jugador.nivel);
  TxtC(dl, 28 * s, ImVec2(cx, cy + 112 * s), IM_COL32(220, 228, 250, 255), buf);
  const char* op[2] = {T::T("Continuar", "Resume"), T::T("Salir al HUB", "Quit to the HUB")};
  for (int i = 0; i < 2; ++i) {
    const float bw = 460 * s, bh = bw * 80.0f / 320.0f, by = cy + 170 * s + float(i) * (bh + 18 * s);
    const ImVec2 a(cx - bw * 0.5f, by), b(cx + bw * 0.5f, by + bh);
    DibujarNueve(dl, pausa_sel_ == i ? "ui_boton_encima" : "ui_boton_normal", a, b, 0.9f * s);
    S::MenuTextB3(dl, ImVec2(cx, by + bh * 0.5f), 38 * s, op[i], pausa_sel_ == i, false, bw * 0.85f);
  }
  if (j.vidas_extra > 0) TxtC(dl, 26 * s, ImVec2(cx, y0 + ph - 100 * s), IM_COL32(150, 255, 170, 230), T::T("Te queda una vida extra.", "You have an extra life left."));
  TxtC(dl, 26 * s, ImVec2(cx, y0 + ph - 62 * s), IM_COL32(220, 225, 245, 230),
       T::T("Zoom: rueda / + - / palanca derecha", "Zoom: wheel / + - / right stick"));
  Ayuda(dl, W, H, T::T("Aceptar", "OK"), T::T("Continuar", "Resume"));
}

void Modo::FinV2(const Juego& j) {
  p2_.res_partidas = prog_.partidas, p2_.res_record = prog_.ultimo_record, p2_.res_logros.clear();
  prog_.cofres += p2_.cofres;
  EvaluarLogros(datos_, ListaLogros(), j, p2_.cofres, true, &prog_, &p2_.res_logros);
  for (const int i : p2_.res_logros) REXLOG_INFO("dbz3 survivors: logro desbloqueado id={} zeni={}", ListaLogros()[size_t(i)].id, ListaLogros()[size_t(i)].zeni);
  GuardarProg();
}

namespace {
// Piezas registradas en native_ui.cpp (data_usi 2033 y 2047), sin titulos rasterizados en un idioma.
bool CabeceraCierre(ImDrawList* dl, cierre::Cuadro r, float k, const char* titulo, bool gana) {
  const ImVec2 a(r.x, r.y), b(r.x + r.w, r.y + r.h);
  const S::Pic placa = S::Img(gana ? "tac_barra_azul" : "tac_barra_roja");
  const float cx = r.x + 0.5f * r.w;
  if (placa) {
    // 2047_24 termina en curva a la derecha; 2047_25 a la izquierda. Reflejar la mitad con remate.
    const ImVec4 uv = gana ? ImVec4(230, 3, 464, 83) : ImVec4(4, 3, 231, 83);
    S::DrawPic(dl, placa, a, ImVec2(cx, b.y), uv, IM_COL32_WHITE, gana);
    S::DrawPic(dl, placa, ImVec2(cx, a.y), b, uv, IM_COL32_WHITE, !gana);
  } else {
    S::Frame(dl, a, b, 0.65f * k, true);
  }
  S::MenuTextB3(dl, ImVec2(cx, r.y + 0.35f * r.h), 50 * k, titulo, gana, false, r.w - 90 * k);
  S::MenuTextB3(dl, ImVec2(cx, r.y + 0.71f * r.h), 23 * k, cierre::kBeta, true, false);
  return bool(placa);
}
}  // namespace

bool Modo::DerrotaNativa(ImDrawList* dl, float W, float H) {
  if (!REXCVAR_GET(dbz3_survivors_cierre_b3)) return false;
  const float k = cierre::Escala(W, H);
  if (k <= 0) return false;
  const bool placa = CabeceraCierre(dl, cierre::Derrota(W, k), k, T::T("Derrota", "Defeat"), false);
  if (Pr().Alguna() && !sur2_cierre_dicho_ && (placa || t_fin_ >= 0.6f)) {
    sur2_cierre_dicho_ = true;
    REXLOG_INFO("dbz3 survivors: sur2 derrota idioma={} placa={} beta=1", T::LangCode(), int(placa));
  }
  return true;
}

void Modo::Resultados(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  const Juego& j = *j_;
  const bool v2 = MetaV2() && j.config.meta_v2;
  if (v2 && p2_.res_partidas != prog_.partidas) {   // una vez por partida: logros finales, cofres a los totales y guardado
    FinV2(j);
    REXLOG_INFO("dbz3 survivors: resultados v2 puntos={} record={} nuevos_logros={}", PuntosPartida(int(j.tiempo), j.derrotados, j.estado == Estado::Victoria), p2_.res_record ? 1 : 0, p2_.res_logros.size());
  }
  // RANK-ONLINE: la partida (de verdad, no las de prueba) puede ir al ranking mundial con X
  dbz3::rank::Partida rk;
  if (dbz3::rank::Activo() && !Pr().Alguna()) {
    rk.modo = 's', rk.jugador = B3Jugador(), rk.segundos = int(j.tiempo), rk.bajas = j.derrotados, rk.nivel = j.jugador.nivel;
    rk.victoria = j.estado == Estado::Victoria, rk.dificultad = j.config.dificultad, rk.escenario = j.Escenario().id;
    rk.puntos = dbz3::rank::PuntosSurvivors(rk.segundos, rk.bajas, rk.victoria), rk.lugar = Es(j.Escenario().txt);
    if (dbz3::rank::Encima(dl, S::GameArea(ImGui::GetIO()), t_, pad)) return;
    pad = dbz3::rank::Entrada(pad, rk, dbz3::rank::kBotonX);
  }
  if (v2 && (pad & kLB)) {   // A14: compartir = captura de la pantalla a disco
    std::filesystem::path f;
    if (CapturarVentana(&f)) {
      REXLOG_INFO("dbz3 survivors: captura guardada {}", f.string());
      std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, "%s", M::L("Captura guardada en survivors_capturas.", "Screenshot saved in survivors_capturas.", "Capture enregistrée dans survivors_capturas.", "Screenshot in survivors_capturas gespeichert.", "Schermata salvata in survivors_capturas."));
    } else {
      REXLOG_WARN("dbz3 survivors: no se pudo guardar la captura");
      std::snprintf(p2_.sub_msg, sizeof p2_.sub_msg, "%s", M::L("No se pudo guardar la captura.", "Could not save the screenshot.", "Impossible d'enregistrer la capture.", "Screenshot konnte nicht gespeichert werden.", "Impossibile salvare la schermata."));
    }
    p2_.sub_msg_t = t_;
  }
  if (pad & kB) return MenuV2() ? Ir(Pantalla(kPantPortada)) : Salir();   // v2: B vuelve al menu propio
  if (pad & kA) return Empezar(sel_, escn_, dif_);
  if (pad & kY) return Ir(Pantalla::Mapa);   // otro escenario
  const float k_cierre = REXCVAR_GET(dbz3_survivors_cierre_b3) ? cierre::Escala(W, H) : 0;
  const bool b3 = k_cierre > 0;
  const bool gana = j.estado == Estado::Victoria;
  if (b3) {
    fondo->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(10, 14, 28, 255));
    if (const S::Pic p = S::Img("fondo_duelo")) {  // mismo fondo B3 que usa el Duelo, manteniendo su proporcion
      const float s = std::max(W / p.w, H / p.h);
      S::DrawPic(fondo, p, ImVec2((W - p.w * s) * 0.5f, (H - p.h * s) * 0.5f),
                 ImVec2((W + p.w * s) * 0.5f, (H + p.h * s) * 0.5f), ImVec4(0, 0, 0, 0), IM_COL32(255, 255, 255, 150));
    }
  } else {
    FondoMenu(fondo, W, H, 0.5f);
  }
  if (!gana) fondo->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(110, 8, 14, 120));   // SUR-06: la derrota se tiñe de rojo oscuro (no se parece a una victoria)
  const float a = Suave(t_pant_ / 0.35f);
  if (!b3 && v2 && p2_.res_record) {   // cinta de "nuevo record"
    const float rw = 330 * k_, rh = 54 * k_, rx = (W + 1150 * k_) * 0.5f - rw - 30 * k_, ry = (H - 1150 * k_ * 800.0f / 1200.0f) * 0.5f + 40 * k_;
    if (const ps2::Tex* cr = Arte5("cinta_nuevo_record.png")) {   // tanda 5: la cinta de GPT con el texto encima
      DibujarImagen(dl, cr, ImVec2(rx - 20 * k_, ry - rh * 0.4f), ImVec2(rx + rw + 20 * k_, ry + rh * 1.4f), IM_COL32(255, 255, 255, int(255 * a)));
      TxtC(dl, 30 * k_, ImVec2(rx + rw * 0.5f, ry + rh * 0.5f), IM_COL32(255, 255, 255, int(255 * a)), M::L("¡Nuevo récord!", "New record!", "Nouveau record !", "Neuer Rekord!", "Nuovo record!"));
    } else {
      Cinta(dl, ImVec2(rx + rw * 0.5f, ry + rh * 0.5f), rw, rh, IM_COL32(230, 90, 30, int(255 * a)), M::L("¡Nuevo récord!", "New record!", "Nouveau record !", "Neuer Rekord!", "Nuovo record!"), 30 * k_);
    }
  }
  // POST3/RANK: con el bloque del ranking el panel crece 100 (visto en juego: su ultima linea y la del alias pisaban el marco de abajo)
  const float k = b3 ? k_cierre : k_;
  const auto box = cierre::Resultados(W, H, k, rk.modo != 0, a);
  const float pw = b3 ? box.w : 1150 * k_, ph = b3 ? box.h : pw * 800.0f / 1200.0f + (rk.modo ? 100 * k_ : 0);
  const float x0 = b3 ? box.x : (W - pw) * 0.5f, y0 = b3 ? box.y : (H - ph) * 0.5f + (1 - a) * 60 * k_;
  if (!b3 && v2) {   // 20/11: la estrella de victoria es el destello del propio B3 (native_ui, data_usi 2047) extraido de los AFS en marcha; nada de arte de terceros.
    if (gana) {   // sin la pieza nativa, el impacto nativo del combate (tambien del juego); nunca un PNG de fuera
      S::Pic star = S::Img("tac_destello");
      if (!star) star = S::Img("fx_impacto");
      if (star) {
        const float fw = pw * 0.92f, fh = fw * star.h / star.w;
        S::DrawPic(dl, star, ImVec2(W * 0.5f - fw * 0.5f, y0 + 20 * k_ - fh * 0.5f),
                   ImVec2(W * 0.5f + fw * 0.5f, y0 + 20 * k_ + fh * 0.5f), ImVec4(0, 0, 0, 0),
                   IM_COL32(255, 255, 255, int(235 * a)));
      }
    } else if (const ps2::Tex* fx = Arte5("resultado_derrota_fondo.png")) {
      const float fw = pw * 0.98f, fh = fw * 0.25f;
      DibujarImagen(dl, fx, ImVec2(W * 0.5f - fw * 0.5f, y0 + 20 * k_ - fh * 0.5f), ImVec2(W * 0.5f + fw * 0.5f, y0 + 20 * k_ + fh * 0.5f), IM_COL32(255, 255, 255, int(255 * a)));
    }
  }
  if (b3) S::Frame(dl, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), 0.8f * k, true);
  else DibujarNueve(dl, "ui_panel_resultados", ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), 0.8f * k, IM_COL32(255, 255, 255, int(255 * a)));
  if (b3) {
    const ImU32 viva = IM_COL32(255, 255, 255, int(255 * a)), apagada = IM_COL32(164, 168, 180, int(255 * a));
    const ImVec2 p(x0 + 90 * k, y0 + 178 * k), q(x0 + 229 * k, p.y);
    for (const auto& [es_jefe, ini] : {std::pair{false, p}, std::pair{true, q}}) {
      const ImVec2 fin(ini.x + 110 * k, ini.y + 110 * k);
      if (CaraB3(dl, es_jefe ? B3Jefe() : B3Jugador(), ini, fin, gana != es_jefe ? viva : apagada, true))
        S::Frame(dl, ini, fin, 0.38f * k, false);
    }
  } else if (v2) {   // R23 SUR-5: a los lados del panel, la cara del luchador (izquierda) y la del jefe (derecha); la del perdedor, apagada
    const float cs = std::min(230 * k_, x0 - 30 * k_), cy0 = y0 + 30 * k_;
    if (cs > 80 * k_) {
      const ImU32 vivo = IM_COL32(255, 255, 255, int(255 * a)), apagado = IM_COL32(170, 170, 180, int(255 * a));
      const ImVec2 la(x0 - 15 * k_ - cs, cy0), lb(la.x + cs, cy0 + cs), ra(x0 + pw + 15 * k_, cy0), rb(ra.x + cs, cy0 + cs);
      if (CaraB3(dl, B3Jugador(), la, lb, gana ? vivo : apagado)) MarcoCara(dl, la, lb, k_);
      if (CaraB3(dl, B3Jefe(), ra, rb, gana ? apagado : vivo)) MarcoCara(dl, ra, rb, k_);
      // encargo 4: en la derrota, el luchador de rodillas bajo su cara (pieza propia, sin rasgos de nadie)
      if (!gana)
        if (const ps2::Tex* dr = Arte("encargo4/derrota.png")) {
          // centrada en el hueco izquierdo (0..x0) y apoyada abajo, sin salirse de la pantalla
          const float s = std::min(x0 - 40 * k_, H - 60 * k_ - (lb.y + 20 * k_)), cx = x0 * 0.5f, pie = H - 60 * k_;
          if (s > 120 * k_) DibujarImagen(dl, dr, ImVec2(cx - s * 0.5f, pie - s), ImVec2(cx + s * 0.5f, pie), vivo);
        }
    }
  }
  if (!b3 && !gana)   // SUR-06: calaveras del propio juego a los lados del titulo
    if (const ps2::Tex* cr = T3(kCraneo))
      for (const float sx : {x0 + 150 * k_, x0 + pw - 150 * k_}) DibujarCuadro(dl, cr, 0, 1, ImVec2(64, 64), ImVec2(sx, y0 + 40 * k_), 110 * k_ / 120.0f, IM_COL32(255, 255, 255, int(255 * a)));
  const char* titulo = gana ? T::T("¡Victoria!", "Victory!") : T::T("Derrota", "Defeat");
  if (b3) {
    const bool placa = CabeceraCierre(dl, {W * 0.5f - 330 * k, y0 + 24 * k, 660 * k, 128 * k}, k, titulo, gana);
    if (Pr().Alguna() && !sur2_cierre_dicho_) {
      const bool marco = S::Img("marco") && S::Img("panel");
      if ((placa && marco) || t_pant_ >= 1.2f) {
        sur2_cierre_dicho_ = true;
        REXLOG_INFO("dbz3 survivors: sur2 resultados resultado={} idioma={} placa={} marco={} beta=1",
                    gana ? "victoria" : "derrota", T::LangCode(), int(placa), int(marco));
      }
    }
  } else if (const ps2::Tex* r = T3(gana ? kRotuloVictoria : kRotuloDerrota)) {   // tanda 3: rotulo vacio (1024x256) con el texto en su zona
    const float s = 680 * k_ / 1024.0f * (0.9f + 0.1f * a), cy = y0 + 40 * k_;
    const float zx = gana ? 144 : 100, zy = 48, zw = gana ? 736 : 824, zh = gana ? 160 : 152;   // result_title (xywh)
    DibujarCuadro(dl, r, 0, 1, ImVec2(512, 128), ImVec2(W * 0.5f, cy), s, IM_COL32(255, 255, 255, int(255 * a)));
    S::MenuTextB3(dl, ImVec2(W * 0.5f + (zx + zw * 0.5f - 512) * s, cy + (zy + zh * 0.5f - 128) * s), zh * s * 0.5f, titulo, gana, false, zw * s);
  } else {
    S::MenuTextB3(dl, ImVec2(W * 0.5f, y0 + 80 * k_), 66 * k_, titulo, gana, false);
  }
  // medalla: platino = victoria en dificil (tanda 4); oro = victoria; plata = mitad de la partida; bronce = el resto
  const Medalla med = MedallaDe(gana, j.Minuto(), j.config.dificultad);
  const float md = 230 * k_ * (0.85f + 0.15f * Suave((t_pant_ - 0.2f) / 0.3f));
  if (b3) {
    static_assert(kBronce == 1 && kPlata == 2 && kOro == 3 && kPlatino == 4);
    if (med != kSinMedalla)
      S::MenuTextB3(dl, ImVec2(x0 + 215 * k, y0 + 323 * k), 38 * k, cierre::TextoMedalla(unsigned(med) - 1, T::LangCode()), true, false, 286 * k);
    if (v2 && p2_.res_record)
      S::MenuTextB3(dl, ImVec2(x0 + 215 * k, y0 + 365 * k), 26 * k, M::L("¡Nuevo récord!", "New record!", "Nouveau record !", "Neuer Rekord!", "Nuovo record!"), false, false, 282 * k);
  } else if (med == kPlatino && T3(kMedallaPlatino)) DibujarCuadro(dl, T3(kMedallaPlatino), 0, 1, ImVec2(128, 128), ImVec2(x0 + 230 * k_, y0 + 300 * k_), md / 256.0f);
  else Pieza(dl, med >= kOro ? "ui_medalla_oro" : med == kPlata ? "ui_medalla_plata" : "ui_medalla_bronce", ImVec2(x0 + 230 * k_, y0 + 300 * k_), md);
  if (nuevo_esc_ >= 0 && nuevo_esc_ < int(datos_.escenarios.size())) {   // escenario recien desbloqueado (sello de la tanda 2)
    char nb[128];
    std::snprintf(nb, sizeof nb, T::T("¡Nuevo escenario: %s!", "New stage: %s!"), Es(datos_.escenarios[size_t(nuevo_esc_)].txt));
    const ImVec2 c(x0 + 230 * k_, y0 + 440 * k_);
    if (b3) S::MenuTextB3(dl, ImVec2(x0 + 215 * k, y0 + 400 * k), 24 * k, nb, false, false, 286 * k);
    else {
      DibujarAjustada(dl, ArteMejoras("rogue/ui_nuevo.png"), ImVec2(c.x - 140 * k_, c.y - 30 * k_), ImVec2(c.x - 64 * k_, c.y + 30 * k_));
      TxtMax(dl, 24 * k_, ImVec2(c.x - 60 * k_, c.y - 14 * k_), IM_COL32(150, 255, 170, 255), nb, 300 * k_);
    }
  }
  // datos
  char buf[96], reloj[16];
  Reloj15(reloj, sizeof reloj, j.Minuto());
  const float fx = x0 + 400 * k, fs = 34 * k;
  float fy = y0 + 170 * k;
  const auto fila = [&](const char* et, const char* val) {
    if (b3) {
      TxtMax(dl, fs, ImVec2(fx, fy), IM_COL32(200, 210, 240, 255), et, 230 * k);
      TxtMax(dl, fs, ImVec2(fx + 260 * k, fy), IM_COL32(255, 230, 120, 255), val, 416 * k);
    } else {
      Txt(dl, fs, ImVec2(fx, fy), IM_COL32(200, 210, 240, 255), et);
      Txt(dl, fs, ImVec2(fx + 260 * k, fy), IM_COL32(255, 230, 120, 255), val);
    }
    fy += b3 ? 37 * k : fs * 1.35f;
  };
  fila(T::T("Personaje", "Fighter"), Es(datos_.personajes[size_t(j.personaje)].txt));
  std::snprintf(buf, sizeof buf, "%s%s", Es(j.Escenario().txt), j.config.dificultad ? T::T(" (difícil)", " (hard)") : "");
  fila(T::T("Escenario", "Stage"), buf);
  fila(T::T("Tiempo", "Time"), reloj);
  std::snprintf(buf, sizeof buf, "%d", j.jugador.nivel);
  fila(T::T("Nivel", "Level"), buf);
  std::snprintf(buf, sizeof buf, "%d", j.derrotados);
  fila(T::T("Derrotados", "Defeated"), buf);
  std::snprintf(buf, sizeof buf, "+%d  (%s %d)", j.zeni, T::T("total", "total"), prog_.zeni);   // tanda 4: van a la tienda
  if (const ps2::Tex* z = b3 ? nullptr : T3(kObjeto0 + int(TipoObjeto::Zeni)))   // la moneda junto a la cifra
    DibujarCuadro(dl, z, 0, 1, ImVec2(64, 124), ImVec2(fx + 260 * k_ + Medir(fs, buf).x + 30 * k_, fy + fs * 1.05f), fs * 1.3f / 128.0f);
  fila(T::T("Zeni", "Zeni"), buf);
  const int puntos = PuntosPartida(int(j.tiempo), j.derrotados, gana);
  if (v2) {   // A14: puntos y el mejor de este escenario y dificultad
    std::snprintf(buf, sizeof buf, "%d  (%s %d)", puntos, M::L("mejor", "best", "meilleur", "Bestwert", "migliore"), prog_.record[std::clamp(j.config.escenario, 0, kMaxEscenarios - 1)][j.config.dificultad & 1]);
    fila(M::L("Puntos", "Points", "Points", "Punkte", "Punti"), buf);
  }
  // dano por tecnica: las 5 primeras con su icono
  int orden[kMaxDefs], n = 0;
  for (int i = 0; i < kMaxDefs && i < int(datos_.tecnicas.size()); ++i)
    if (j.dano_tecnica[i] > 0) orden[n++] = i;
  std::sort(orden, orden + n, [&](int x, int y) { return j.dano_tecnica[x] > j.dano_tecnica[y]; });
  n = std::min(n, rk.modo ? 2 : v2 ? (p2_.res_logros.empty() ? 3 : 2) : 5);   // RANK-ONLINE: con el bloque del ranking, 2
  const double tope = n ? j.dano_tecnica[orden[0]] : 1.0;
  fy = std::max(fy + 14 * k, y0 + 430 * k);   // debajo de la medalla
  if (b3) {
    if (v2) {
      std::string lista;
      for (int q = 0; q < j.nTecnicas + j.nPasivas; ++q) {
        const bool tec = q < j.nTecnicas;
        const Poseida& ps = tec ? j.tecnicas[q] : j.pasivas[q - j.nTecnicas];
        if (ps.def < 0 || size_t(ps.def) >= (tec ? datos_.tecnicas.size() : datos_.pasivas.size())) continue;
        if (!lista.empty()) lista += " / ";
        lista += Es(tec ? datos_.tecnicas[size_t(ps.def)].txt : datos_.pasivas[size_t(ps.def)].txt);
        lista += " " + std::to_string(ps.nivel);
      }
      if (!lista.empty()) TxtMax(dl, 25 * k, ImVec2(x0 + 110 * k, fy), IM_COL32(255, 223, 132, 255), lista.c_str(), pw - 220 * k);
      fy += 31 * k;
    }
    TxtMax(dl, 28 * k, ImVec2(x0 + 110 * k, fy), IM_COL32(200, 210, 240, 255), T::T("Daño por técnica", "Damage by technique"), pw - 220 * k);
    fy += (v2 ? 31 : 42) * k;
  } else {
    Txt(dl, 28 * k_, ImVec2(x0 + 110 * k_, fy), IM_COL32(200, 210, 240, 255), T::T("Daño por técnica", "Damage by technique"));
  }
  if (!b3 && v2) {   // fila de tecnicas y pasivas con su nivel (puntos pequenos; el 5 = "MAX")
    float ix = x0 + 520 * k_;
    const float is = 52 * k_;
    for (int q = 0; q < j.nTecnicas + j.nPasivas && ix + is < x0 + pw - 70 * k_; ++q, ix += is + 6 * k_) {
      const bool tec = q < j.nTecnicas;
      const Poseida& ps = tec ? j.tecnicas[q] : j.pasivas[q - j.nTecnicas];
      const char* ico = ps.def < 0 ? nullptr : tec ? (size_t(ps.def) < datos_.tecnicas.size() ? datos_.tecnicas[size_t(ps.def)].icono.c_str() : nullptr)
                                                   : (size_t(ps.def) < datos_.pasivas.size() ? datos_.pasivas[size_t(ps.def)].icono.c_str() : nullptr);
      if (!ico) continue;
      Pieza(dl, "ui_casilla_mejora", ImVec2(ix + is * 0.5f, fy + 14 * k_), is);
      IconoComercial(dl, ico, ImVec2(ix + is * 0.12f, fy + 14 * k_ - is * 0.38f), ImVec2(ix + is * 0.88f, fy + 14 * k_ + is * 0.38f));
      char lv[8];
      std::snprintf(lv, sizeof lv, ps.nivel >= kNivelMax ? "MAX" : "%d", ps.nivel);
      TxtC(dl, 22 * k_, ImVec2(ix + is * 0.5f, fy + 14 * k_ + is * 0.64f), ps.nivel >= kNivelMax ? IM_COL32(255, 120, 255, 255) : IM_COL32(255, 220, 90, 255), lv);
    }
  }
  if (!b3) fy += (v2 ? 62 : 42) * k_;
  for (int r = 0; r < n; ++r) {
    const DefTecnica& d = datos_.tecnicas[size_t(orden[r])];
    const float ic = 36 * k, y = fy + r * (ic + 6 * k), bx = x0 + 110 * k + ic + 230 * k, bw = pw - (bx - x0) - 200 * k;
    if (!b3) IconoComercial(dl, d.icono.c_str(), ImVec2(x0 + 110 * k, y), ImVec2(x0 + 110 * k + ic, y + ic));
    TxtMax(dl, 24 * k, ImVec2(x0 + (b3 ? 110 * k : 110 * k + ic + 10 * k), y + ic * 0.2f), IM_COL32_WHITE, Es(d.txt), (b3 ? 246 : 210) * k);
    const float f = float(j.dano_tecnica[orden[r]] / tope);
    dl->AddRectFilled(ImVec2(bx, y + ic * 0.3f), ImVec2(bx + bw, y + ic * 0.7f), IM_COL32(0, 0, 0, 120), 4 * k);
    dl->AddRectFilled(ImVec2(bx, y + ic * 0.3f), ImVec2(bx + bw * f, y + ic * 0.7f), Rgb(d.color, 0.95f), 4 * k);
    std::snprintf(buf, sizeof buf, "%.0f", j.dano_tecnica[orden[r]]);
    Txt(dl, 24 * k, ImVec2(bx + bw + 12 * k, y + ic * 0.2f), IM_COL32(255, 230, 120, 255), buf);
  }
  if (v2 && !rk.modo && !p2_.res_logros.empty()) {   // logros que han saltado en esta partida
    std::string l;
    for (const int i : p2_.res_logros) l += (l.empty() ? "" : ", ") + std::string(LogroTxt(ListaLogros()[size_t(i)], false));
    TxtMax(dl, 32 * k, ImVec2(x0 + 110 * k, y0 + ph - 100 * k), IM_COL32(150, 255, 170, 255), (std::string(M::L("Logros: ", "Achievements: ", "Succès : ", "Erfolge: ", "Traguardi: ")) + l).c_str(), pw - 220 * k);
  }
  if (rk.modo) {   // RANK-ONLINE: bloque abajo y su boton (X)
    dbz3::rank::Bloque(dl, ImVec2(x0 + 110 * k, y0 + 588 * k), (pw - 220 * k) / (1.5f * k), 1.5f * k, rk);
    if (dbz3::rank::Consultando())   // RANK-ONLINE: aviso de privacidad (Si / No)
      return Ayuda(dl, W, H, M::L("Sí, enviar", "Yes, send", "Oui, envoyer", "Ja, senden", "Sì, invia"), M::L("No", "No", "Non", "Nein", "No"));
    if (dbz3::rank::EligiendoAlias()) return Ayuda(dl, W, H, T::T("Usar este nombre", "Use this name"), T::T("Ahora no (no se envía)", "Not now (not sent)"));
    std::vector<std::pair<const char*, const char*>> v = {{"A", T::T("Otra partida", "Play again")}, {"B", MenuV2() ? M::L("Menú", "Menu", "Menu", "Menü", "Menu") : T::T("Volver al HUB", "Back to the HUB")},
                                                          {"Y", T::T("Mapa", "Map")}};
    if (const char* x = dbz3::rank::EtiquetaBoton()) v.push_back({"X", x});
    return S::HelpLine(dl, ImVec2(W * 0.5f, H - 52 * k), 34 * k, v);
  }
  if (v2) {
    if (t_ - p2_.sub_msg_t < 3.0f) TxtC(dl, 26 * k, ImVec2(W * 0.5f, H - 100 * k), IM_COL32(150, 255, 170, 255), p2_.sub_msg);
    return S::HelpLine(dl, ImVec2(W * 0.5f, H - 52 * k), 34 * k, {{"A", T::T("Otra partida", "Play again")}, {"B", M::L("Menú", "Menu", "Menu", "Menü", "Menu")}, {"Y", T::T("Mapa", "Map")},
                                                                    {"LB", M::L("Compartir", "Share", "Partager", "Teilen", "Condividi")}});
  }
  Ayuda(dl, W, H, T::T("Otra partida", "Play again"), MenuV2() ? M::L("Menú", "Menu", "Menu", "Menü", "Menu") : T::T("Volver al HUB", "Back to the HUB"), T::T("Mapa", "Map"));
}

void Modo::Mensaje(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  if (pad & (kA | kB)) return Salir();
  fondo->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(12, 16, 34, 255));
  const float pw = 1000 * k_, ph = 300 * k_, x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
  if (!DibujarNueve(dl, "ui_ventana_dialogo", ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), 0.6f * k_)) dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(0, 0, 0, 200), 12 * k_);
  Txt(dl, 30 * k_, ImVec2(x0 + 70 * k_, y0 + 60 * k_), IM_COL32_WHITE, error_.c_str(), pw - 140 * k_);
  Ayuda(dl, W, H, nullptr, T::T("Volver al HUB", "Back to the HUB"));
}

void Modo::Aviso(const char* texto, bool jefe) {   // rotulo de oleada (texto del motor encima del arte)
  std::snprintf(aviso_txt_, sizeof aviso_txt_, "%s", texto);
  aviso_jefe_ = 0, aviso_rojo_ = jefe;
}

void Modo::Minimapa(ImDrawList* dl, ImVec2 c, float lado) {
  const Juego& j = *j_;
  const float s = lado / 256.0f, R = 101.0f * s, k = R / 34.0f;   // 34 de mundo hasta el borde
  dl->AddCircleFilled(c, R, Rgb(j.Escenario().deco.color, 0.85f), 48);
  dl->AddCircleFilled(c, R, IM_COL32(6, 12, 30, 140), 48);
  const auto punto = [&](int pieza, float wx, float wy, bool al_borde, float tam) {
    float dx = (wx - j.jugador.x) * k, dy = -(wy - j.jugador.y) * k;
    const float l = std::sqrt(dx * dx + dy * dy), lim = R - tam * 0.45f;
    if (l > lim) {
      if (!al_borde) return;
      dx *= lim / l, dy *= lim / l;
    }
    DibujarCuadro(dl, T3(pieza), 0, 1, ImVec2(16, 16), ImVec2(c.x + dx, c.y + dy), tam / 32.0f);
  };
  for (int i = 0, n = j.NumEnemigos(), puestos = 0; i < n && puestos < 24; ++i) {
    const VistaEnemigo e = j.Enemigo(i);
    if (e.anillo > 0) punto(kMiniElite, e.x, e.y, false, 22 * k_), ++puestos;
  }
  for (int i = 0; i < j.nObjetos; ++i)
    if (j.objetos[i].tipo == TipoObjeto::Cofre) punto(kMiniCofre, j.objetos[i].x, j.objetos[i].y, true, 22 * k_);
  if (j.jefe.estado != EstadoJefe::Ausente && j.jefe.estado != EstadoJefe::Muerto) punto(kMiniJefe, j.jefe.x, j.jefe.y, true, 30 * k_);
  if (const ps2::Tex* t = T3(kMiniJugador)) {
    const float r = 13 * k_;
    DibujarGirada(dl, t, c, ImVec2(r, r), std::atan2(j.jugador.dx, j.jugador.dy), 1.0f);
  }
  if (const ps2::Tex* mf = T3(kMiniMarco)) DibujarCuadro(dl, mf, 0, 1, ImVec2(128, 128), c, s);
  else dl->AddCircle(c, R, IM_COL32(255, 170, 40, 255), 48, 3 * k_);
}


// ---------------------------------------------------------------- ganchos del modo: coleccion, logros y pruebas (meta v2)
void Modo::PantallasAlEvento(const Evento& e) {
  if (!MetaV2() || !j_ || !j_->config.meta_v2) return;
  Juego& j = *j_;
  EstadoP2& p = p2_;
  if (j.pasos < p.ult_pasos) {   // partida nueva
    p.cofres = 0, p.dicho &= ~7u, p.logro_cola.clear(), p.logro_mostrado = -1;
  }
  p.ult_pasos = j.pasos;
  // pruebas: DBZ3_TEST_SURVIVORS_COFRE=<tier> cofre al lado del jugador a los 20 s; DBZ3_TEST_SURVIVORS_LOGRO=1 da el primer logro a los 5 s
  static const char* tc = std::getenv("DBZ3_TEST_SURVIVORS_COFRE");
  static const bool tl = std::getenv("DBZ3_TEST_SURVIVORS_LOGRO") != nullptr;
  if (tc && !(p.dicho & 1) && j.tiempo >= 20 && j.estado == Estado::Jugando && j.nObjetos < kMaxObjetos) {
    p.dicho |= 1;
    const int tier = std::atoi(tc) >= 5 ? 5 : std::atoi(tc) >= 3 ? 3 : 1;
    j.objetos[j.nObjetos++] = {j.jugador.x, j.jugador.y, TipoObjeto::Cofre, uint8_t(tier)};
    REXLOG_INFO("dbz3 survivors: cofre forzado tier={}", tier);
  }
  std::vector<int> nuevos;
  if (tl && !(p.dicho & 2) && j.tiempo >= 5 && !ListaLogros().empty() && !((prog_.logros >> 0) & 1u)) {
    p.dicho |= 2;
    prog_.logros |= 1u, prog_.zeni += ListaLogros()[0].zeni;
    nuevos.push_back(0);
  }
  // coleccion: lo que se ve por primera vez
  switch (e.tipo) {
    case TipoEvento::Muerte:
      if (e.a >= 0 && e.a < 32) prog_.vistos_enem |= 1u << e.a;
      break;
    case TipoEvento::JefeAparece: {
      const int jf = j.Escenario().jefe;
      if (jf >= 0 && jf < 32) prog_.vistos_jefe |= 1u << jf;
      break;
    }
    case TipoEvento::CofreAbre:
      ++p.cofres;
      REXLOG_INFO("dbz3 survivors: cofre premios={} tier={}", j.nPremios, j.tierCofre);
      [[fallthrough]];
    case TipoEvento::Subida:
    case TipoEvento::Evolucion:
    case TipoEvento::Tecnica:
      for (int i = 0; i < j.nTecnicas; ++i)
        if (j.tecnicas[i].def >= 0 && j.tecnicas[i].def < 64) prog_.vistos_tec |= uint64_t(1) << j.tecnicas[i].def;
      for (int i = 0; i < j.nPasivas; ++i)
        if (j.pasivas[i].def >= 0 && j.pasivas[i].def < 64) prog_.vistos_pas |= uint64_t(1) << j.pasivas[i].def;
      break;
    default: break;
  }
  // logros (los de una partida, en directo; el resto, al acabar). Las muertes cuentan a todo correr: se mira cada 32 pasos
  const bool mirar = e.tipo == TipoEvento::Subida || e.tipo == TipoEvento::Evolucion || e.tipo == TipoEvento::CofreAbre || e.tipo == TipoEvento::JefeMuere ||
                     e.tipo == TipoEvento::Racha || e.tipo == TipoEvento::Rompible || e.tipo == TipoEvento::Objeto || ((j.pasos & 31) == 0);
  if (mirar) EvaluarLogros(datos_, ListaLogros(), j, p.cofres, false, &prog_, &nuevos);
  if (!nuevos.empty()) {
    for (const int i : nuevos) {
      p.logro_cola.push_back(i);
      REXLOG_INFO("dbz3 survivors: logro desbloqueado id={} zeni={}", ListaLogros()[size_t(i)].id, ListaLogros()[size_t(i)].zeni);
    }
    GuardarProg();   // la recompensa no se pierde aunque se abandone la partida
  }
}

Pantalla Modo::Inicial() const {
  if (!MenuV2()) return Pantalla::Eleccion;
  if (const char* m = PruebaMenu()) {
    if (!std::strcmp(m, "coleccion")) return Pantalla::Coleccion;
    if (!std::strcmp(m, "logros")) return Pantalla::Logros;
    if (!std::strcmp(m, "ranking")) return Pantalla::Clasificacion;
    if (!std::strcmp(m, "comojugar")) return Pantalla::ComoJugar;
    if (!std::strcmp(m, "opciones")) return Pantalla::Opciones;
    return Pantalla::Portada;
  }
  return Pr().piloto || Pr().mando ? Pantalla::Eleccion : Pantalla::Portada;
}

// Pulsaciones de la subida de nivel v2 y del cofre (antes de leer izq / der / A en Partida)
void Modo::SubidaMando(uint16_t& pad, float dt) {
  Juego& j = *j_;
  if (!MetaV2() || !j.config.meta_v2) return;
  EstadoP2& p = p2_;
  if (j.CofreAbierto()) {   // el cofre se abre solo; A recoge cuando acaba la apertura. t_opc_ se queda en 0 mientras tanto (el piloto no lo salta)
    p.cofre_t += dt;
    static const bool prueba = std::getenv("DBZ3_TEST_SURVIVORS_COFRE") != nullptr;
    if (p.cofre_t < (prueba ? 6.0f : 2.8f)) t_opc_ = 0, pad &= ~kA;
    if (p.cofre_t >= 0.0f && !(p.dicho & 4)) {
      p.dicho |= 4;
      REXLOG_INFO("dbz3 survivors: cofre cerrado tier={}", j.tierCofre);
    }
    if (p.cofre_t >= 2.8f && !(p.dicho & 8)) {
      p.dicho |= 8;
      REXLOG_INFO("dbz3 survivors: cofre abierto tier={}", j.tierCofre);
    }
    return;
  }
  p.cofre_t = 0, p.dicho &= ~12u;
  if (t_opc_ < 0.3f) return;   // la animacion de entrada no se salta sin querer
  // pruebas: DBZ3_TEST_SURVIVORS_SUBIDA=1 re-sortea en la 1a subida, destierra en la 2a y las deja ver (el piloto no las cierra antes)
  static const bool tsub = std::getenv("DBZ3_TEST_SURVIVORS_SUBIDA") != nullptr;
  if (tsub) {
    p.t_prueba += dt;
    if (!(p.dicho & 32) && t_opc_ > 1.1f) p.dicho |= 32, j.zeni += 200, pad |= kX, p.t_prueba = 0;
    else if ((p.dicho & 32) && !(p.dicho & 64) && t_opc_ > 1.1f) p.dicho |= 64, pad |= kLB, p.t_prueba = 0;
    else if ((p.dicho & 96) != 96 || p.t_prueba < 5.0f) t_opc_ = std::min(t_opc_, 1.2f);
  }
  const auto dicho = [&](const char* t) {
    std::snprintf(p.sub_msg, sizeof p.sub_msg, "%s", t);
    p.sub_msg_t = t_;
  };
  if (pad & kX) {
    if (j.Resortear()) {
      REXLOG_INFO("dbz3 survivors: subida resortear n={} zeni={}", j.resorteos, j.zeni);
      opc_dicha_ = false;
    } else {
      dicho(M::L("Te faltan zeni para re-sortear.", "Not enough zeni to reroll.", "Pas assez de zeni pour retirer.", "Nicht genug Zeni zum Neuwürfeln.", "Zeni insufficienti per ritirare."));
    }
    pad &= ~(kX | kA);
  }
  if (pad & kY) {
    if (j.Saltar()) {
      REXLOG_INFO("dbz3 survivors: subida saltada zeni={}", j.zeni);
      opc_dicha_ = false, eleccion_cofre_ = false;
    }
    pad &= ~(kY | kA);
  }
  if (pad & kLB) {
    if (op_sel_ < j.nOpciones && j.Desterrar(op_sel_)) {
      REXLOG_INFO("dbz3 survivors: subida desterrada quedan={}", j.DesterrosRestantes());
      opc_dicha_ = false;
    } else {
      dicho(M::L("Esa opción no se puede desterrar.", "That option cannot be banished.", "Cette option ne peut pas être bannie.", "Diese Option kann nicht verbannt werden.", "Questa opzione non si può bandire."));
    }
    pad &= ~(kLB | kA);
  }
}

// ---------------------------------------------------------------- A12: portada y menu propio
namespace {
const char* NombreEstacion(int i) {
  switch (i) {
    case 0: return M::L("Jugar", "Play", "Jouer", "Spielen", "Gioca");
    case 1: return M::L("Tienda", "Shop", "Boutique", "Laden", "Negozio");
    case 2: return M::L("Colección", "Collection", "Collection", "Sammlung", "Collezione");
    case 3: return M::L("Logros", "Achievements", "Succès", "Erfolge", "Traguardi");
    case 4: return M::L("Clasificación", "Ranking", "Classement", "Rangliste", "Classifica");
    case 5: return M::L("Cómo jugar", "How to play", "Comment jouer", "Spielanleitung", "Come si gioca");
    default: return M::L("Opciones", "Options", "Options", "Optionen", "Opzioni");
  }
}
}  // namespace

void Modo::Portada(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  constexpr int n = 7;
  EstadoP2& p = p2_;
  zoom_ = float(prog_.opc_zoom10) / 10.0f, g_opciones.volumen = prog_.opc_volumen, g_opciones.sacudida = prog_.opc_sacudida != 0;
  const auto abrir = [&](int s) {
    REXLOG_INFO("dbz3 survivors: menu estacion={} {}", s, kEstaciones[s]);
    Sonar("aceptar", 3, 0);
    p.portada_sel = s;
    switch (s) {
      case 0: Ir(Pantalla::Eleccion); break;
      case 1: AbrirTienda(); break;
      case 2: Ir(Pantalla::Coleccion); break;
      case 3: Ir(Pantalla::Logros); break;
      case 4: Ir(Pantalla::Clasificacion); break;
      case 5: Ir(Pantalla::ComoJugar); break;
      default: Ir(Pantalla::Opciones); break;
    }
  };
  if (Recorrido() && t_pant_ > 2.8f && p.paso_prueba < n) {   // pruebas: portada > cada estacion > volver
    ++p.paso_prueba;
    if (p.paso_prueba < n) return abrir(p.paso_prueba);
    REXLOG_INFO("dbz3 survivors: menu recorrido completo");
  }
  if (pad & kB) return Salir();
  if (pad & kArriba) p.portada_sel = (p.portada_sel + n - 1) % n, Sonar("mover", 2, 0);
  if (pad & kAbajo) p.portada_sel = (p.portada_sel + 1) % n, Sonar("mover", 2, 0);
  if (pad & kA) return abrir(p.portada_sel);
  if (const ps2::Tex* pf = Arte("encargo4/portada_fondo.png")) {   // encargo 4: fondo pintado propio (horda en el horizonte)
    DibujarImagen(fondo, pf, ImVec2(0, 0), ImVec2(W, H));
    fondo->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 8, 24, 60));
  } else {
    FondoMenu(fondo, W, H, 0.42f, datos_.escenarios[size_t(std::clamp(int(t_ / 20.0f) % 4, 0, int(datos_.escenarios.size()) - 1))].id.c_str());
  }
  Precarga(3);
  for (int e = 0; e < int(datos_.escenarios.size()); ++e) PrecargaEscenario(e, 1), Fondo(datos_.escenarios[size_t(e)].id.c_str(), "suelo");   // R23 SURV-7: miniaturas de los 6
  // R23 SURV-6: dos columnas con el sistema de resultados. Izquierda: logo de B3 + nombre del modo y el menu en su panel.
  // Derecha: el panel del luchador (rotulo con su nombre, retrato de B3, tecnica inicial, estadisticas y los escenarios con su medalla).
  const float lx = 470 * k_;
  float ly = 200 * k_;   // donde acaba el logo
  if (const S::Pic lg = S::Img("b3_logo_dbz")) {   // el logo de portada de B3 + el nombre del modo con la letra del juego
    const float w = 500 * k_, h = w * lg.h / std::max(1.0f, lg.w), y = 18 * k_;
    S::DrawPic(dl, lg, ImVec2(lx - w * 0.5f, y), ImVec2(lx + w * 0.5f, y + h));
    S::MenuTextB3(dl, ImVec2(lx, y + h + 10 * k_), 60 * k_, "BUDOKAI SURVIVORS", true, false, 720 * k_);
    {   // R23 SURV-7: BETA como subtitulo del logo, entre dos filetes dorados (la cinta naranja suelta parecia pegada)
      const float by = y + h + 64 * k_, tw = Medir(34 * k_, "BETA").x + 70 * k_;
      dl->AddLine(ImVec2(lx - tw * 0.5f - 150 * k_, by), ImVec2(lx - tw * 0.5f - 14 * k_, by), IM_COL32(230, 170, 60, 230), 3 * k_);
      dl->AddLine(ImVec2(lx + tw * 0.5f + 14 * k_, by), ImVec2(lx + tw * 0.5f + 150 * k_, by), IM_COL32(230, 170, 60, 230), 3 * k_);
      PanelMenu(dl, ImVec2(lx - tw * 0.5f, by - 26 * k_), ImVec2(lx + tw * 0.5f, by + 26 * k_));
      S::MenuTextB3(dl, ImVec2(lx, by), 34 * k_, "BETA", true, false);
    }
    ly = y + h + 96 * k_;
  } else if (const ps2::Tex* lg = Arte5("logo_titulo.png")) {
    const float w = 640 * k_, h = w * float(lg->h) / std::max(1.0f, float(lg->w));
    DibujarImagen(dl, lg, ImVec2(lx - w * 0.5f, 20 * k_), ImVec2(lx + w * 0.5f, 20 * k_ + h));
    ly = 20 * k_ + h;
  } else {
    S::MenuTextB3(dl, ImVec2(lx, 120 * k_), 64 * k_, "Dragon Budokai Survivors", true, false, 720 * k_);
  }
  // menu: panel con las 7 entradas (botones nativos ui_boton_*)
  const ImVec2 ma(100 * k_, std::max(ly + 12 * k_, 236 * k_)), mb(840 * k_, 990 * k_);
  PanelMenu(fondo, ma, mb);
  const float bw = 620 * k_, paso = (mb.y - ma.y - 70 * k_) / float(n), bh = std::min(88 * k_, paso - 12 * k_), y0 = ma.y + 35 * k_ + (paso - bh) * 0.5f;   // R23 SURV-7: sin hueco abajo
  for (int i = 0; i < n; ++i) {
    const bool el = i == p.portada_sel;
    const float g = el ? 1.03f + 0.01f * std::sin(t_ * 4.0f) : 1.0f, w = bw * g, h = bh * g;
    const ImVec2 c(lx, y0 + paso * float(i) + bh * 0.5f), a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
    if (el) fondo->AddRectFilled(ImVec2(a.x - 8 * k_, a.y - 8 * k_), ImVec2(b.x + 8 * k_, b.y + 8 * k_), IM_COL32(255, 200, 40, 80), 16 * k_);
    DibujarNueve(dl, el ? "ui_boton_encima" : "ui_boton_normal", a, b, 0.00783f * h);
    // R23 SURV-7: solo texto, como el menu nativo de B3 (los iconos de la tanda 5 eran de estilos y escalas distintos)
    S::MenuTextB3(dl, c, 44 * k_, NombreEstacion(i), el, false, w - 60 * k_);
  }
  // panel del luchador (el ultimo elegido)
  const DefPersonaje& pj = datos_.personajes[size_t(std::clamp(sel_, 0, int(datos_.personajes.size()) - 1))];
  const Modelo m = ModeloDe(pj.fid);
  const ImVec2 pa(920 * k_, 104 * k_), pb(W - 80 * k_, 990 * k_);
  PanelMenu(fondo, pa, pb);
  const float pcx = (pa.x + pb.x) * 0.5f, px = pa.x + 56 * k_;
  Rotulo(dl, ImVec2(pcx, pa.y + 92 * k_), 600 * k_, Es(pj.txt));
  TxtC(dl, 24 * k_, ImVec2(pcx, pa.y + 50 * k_), IM_COL32(255, 215, 150, 255), M::L("Tu luchador", "Your fighter", "Ton combattant", "Dein Kämpfer", "Il tuo lottatore"));
  const float cs = 290 * k_;   // la cara de B3 es de baja resolucion: a mas de ~2x se ve borrosa
  const ImVec2 ca(px, pa.y + 190 * k_), cb(ca.x + cs, ca.y + cs);
  if (CaraB3(dl, m.b3, ca, cb, IM_COL32_WHITE, true)) MarcoCara(dl, ca, cb, k_ * 1.5f);
  else DibujarFiguraEnRect(fondo, ca, cb, m.b3, m.forma, pj.fid, -0.15f, kClipEspera, t_ + 1.3f);
  const ImU32 gris = IM_COL32(200, 210, 240, 255), oro = IM_COL32(255, 225, 90, 255);
  const float ix = cb.x + 40 * k_, iw = pb.x - 50 * k_ - ix;
  Txt(dl, 28 * k_, ImVec2(ix, ca.y), gris, M::L("Técnica inicial", "Starting technique", "Technique de départ", "Starttechnik", "Tecnica iniziale"));
  if (pj.tecnica >= 0 && pj.tecnica < int(datos_.tecnicas.size())) {
    const DefTecnica& t = datos_.tecnicas[size_t(pj.tecnica)];
    const float ic = 64 * k_, y = ca.y + 42 * k_;
    Pieza(dl, "ui_casilla_mejora", ImVec2(ix + ic * 0.5f, y + ic * 0.5f), ic);
    IconoEn(dl, t.icono.c_str(), ImVec2(ix + ic * 0.12f, y + ic * 0.12f), ImVec2(ix + ic * 0.88f, y + ic * 0.88f));
    TxtMax(dl, 32 * k_, ImVec2(ix + ic + 14 * k_, y + ic * 0.5f - 18 * k_), oro, Es(t.txt), iw - ic - 14 * k_);
  }
  {   // estadisticas con su icono
    char v[4][16];
    const float dano = pj.tecnica >= 0 && pj.tecnica < int(datos_.tecnicas.size()) ? datos_.tecnicas[size_t(pj.tecnica)].dano[0] : 0.0f;
    std::snprintf(v[0], 16, "%.0f", double(pj.vida)), std::snprintf(v[1], 16, "%.1f", double(pj.velocidad)), std::snprintf(v[2], 16, "%.0f", double(dano));
    std::snprintf(v[3], 16, "%.1f", double(pj.recogida));
    const char* et[4] = {T::T("Vida", "Health"), T::T("Velocidad", "Speed"), M::L("Daño", "Damage", "Dégâts", "Schaden", "Danno"), M::L("Alcance", "Pickup", "Portée", "Reichweite", "Portata")};
    const int ics[4] = {kStatVida, kStatVelocidad, kStatDano, kStatAlcance};
    const float is = 42 * k_;
    for (int s = 0; s < 4; ++s) {
      const float y = ca.y + 136 * k_ + 48 * k_ * float(s);
      if (T3(ics[s])) Pieza3(dl, ics[s], ImVec2(32, 32), ImVec2(ix + is * 0.5f, y), is / 56.0f);
      Txt(dl, 30 * k_, ImVec2(ix + is + 14 * k_, y - 17 * k_), gris, et[s]);
      Txt(dl, 30 * k_, ImVec2(pb.x - 60 * k_ - Medir(30 * k_, v[s]).x, y - 17 * k_), IM_COL32_WHITE, v[s]);
    }
  }
  {   // escenarios: miniatura y la mejor medalla de cada uno; el mejor record
    dl->AddLine(ImVec2(px, cb.y + 34 * k_), ImVec2(pb.x - 56 * k_, cb.y + 34 * k_), IM_COL32(200, 120, 40, 200), 2 * k_);
    S::MenuTextB3(dl, ImVec2(pcx, cb.y + 76 * k_), 36 * k_, M::L("Escenarios", "Stages", "Niveaux", "Arenen", "Scenari"), true, false);
    const int ne = int(datos_.escenarios.size());
    const float hueco = 14 * k_, ew = (pb.x - pa.x - 112 * k_ - hueco * float(ne - 1)) / float(ne), eh = ew * 246.0f / 478.0f, ey = cb.y + 112 * k_;
    int mejor = 0;
    for (int e = 0; e < ne; ++e) {
      const ImVec2 a(px + float(e) * (ew + hueco), ey), b(a.x + ew, ey + eh);
      const bool ab = prog_.Desbloqueado(e);
      Miniatura(fondo, e, a, b);
      if (!ab) fondo->AddRectFilled(a, b, IM_COL32(6, 10, 30, 160));   // R23 SURV-7: como en el mapa
      dl->AddRect(a, b, IM_COL32(190, 120, 40, 255), 4 * k_, 0, 2 * k_);
      if (!ab) DibujarAjustada(dl, ArteMejoras("rogue/ui_candado.png"), ImVec2((a.x + b.x) * 0.5f - 18 * k_, (a.y + b.y) * 0.5f - 18 * k_), ImVec2((a.x + b.x) * 0.5f + 18 * k_, (a.y + b.y) * 0.5f + 18 * k_));
      if (const int md = prog_.medalla[e]) {
        const ImVec2 mc(b.x - 8 * k_, a.y + 8 * k_);   // R23 SURV-7: sobre la esquina de la miniatura
        if (md == kPlatino) DibujarAjustada(dl, T3(kMedallaPlatino), ImVec2(mc.x - 24 * k_, mc.y - 24 * k_), ImVec2(mc.x + 24 * k_, mc.y + 24 * k_));
        else Pieza(dl, md == kOro ? "ui_medalla_oro" : md == kPlata ? "ui_medalla_plata" : "ui_medalla_bronce", mc, 48 * k_);
      } else if (ab) {   // abierto sin medalla: el hueco de la medalla
        (void)0;   // R23 SURV-7: sin hueco de medalla (con la medalla en la esquina sobra)
      }
      if (e < kMaxEscenarios) mejor = std::max({mejor, prog_.record[e][0], prog_.record[e][1]});
    }
    // resumen del progreso: cuatro cifras grandes con su etiqueta
    dl->AddLine(ImVec2(px, ey + eh + 72 * k_), ImVec2(pb.x - 56 * k_, ey + eh + 72 * k_), IM_COL32(200, 120, 40, 200), 2 * k_);
    const int val[4] = {mejor, prog_.partidas, prog_.victorias, prog_.total_derrotados};
    const char* et[4] = {M::L("Récord", "Best score", "Record", "Rekord", "Record"), M::L("Partidas", "Runs", "Parties", "Runden", "Partite"),
                         M::L("Victorias", "Wins", "Victoires", "Siege", "Vittorie"), M::L("Derrotados", "Defeated", "Vaincus", "Besiegt", "Sconfitti")};
    const float colw = (pb.x - pa.x - 112 * k_) / 4.0f, ry = ey + eh + 120 * k_;
    for (int q = 0; q < 4; ++q) {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%d", val[q]);
      const float cx = px + colw * (float(q) + 0.5f);
      S::MenuTextB3(dl, ImVec2(cx, ry), 44 * k_, buf, true, false, colw - 10 * k_);
      TxtC(dl, 26 * k_, ImVec2(cx, ry + 46 * k_), gris, et[q]);
    }
  }
  ZeniTotal(dl, ImVec2(W - 40 * k_, 60 * k_), 40 * k_);
  Ayuda(dl, W, H, M::L("Elegir", "Select", "Choisir", "Wählen", "Scegli"), T::T("Volver al HUB", "Back to the HUB"));
}

// ---------------------------------------------------------------- A12: opciones propias (volumen, zoom de serie, sacudida)
void Modo::Opciones(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  EstadoP2& p = p2_;
  if (Recorrido() && t_pant_ > 3.0f) pad |= kB;
  const auto volver = [&] {
    GuardarProg();
    Sonar("atras", 5, 0);
    Ir(Pantalla(kPantPortada));
  };
  if (pad & kB) return volver();
  const int n_opciones = REXCVAR_GET(dbz3_survivors_autoapuntado) ? 4 : 3;
  p.opc_sel = std::clamp(p.opc_sel, 0, n_opciones - 1);
  if (pad & kArriba) p.opc_sel = (p.opc_sel + n_opciones - 1) % n_opciones, Sonar("mover", 2, 0);
  if (pad & kAbajo) p.opc_sel = (p.opc_sel + 1) % n_opciones, Sonar("mover", 2, 0);
  const int d = ((pad & kDer) ? 1 : 0) - ((pad & kIzq) ? 1 : 0);
  if (d) {
    if (p.opc_sel == 0) prog_.opc_volumen = std::clamp(prog_.opc_volumen + 10 * d, 0, 100);
    else if (p.opc_sel == 1) prog_.opc_zoom10 = std::clamp(prog_.opc_zoom10 + d, 10, 22), zoom_tocado_ = true;
    else if (p.opc_sel == 2) prog_.opc_sacudida = !prog_.opc_sacudida;
    else prog_.opc_autoapunte = !prog_.opc_autoapunte;   // SUR-8
    Sonar("mover", 2, 0);
    REXLOG_INFO("dbz3 survivors: opciones volumen={} zoom={:.1f} sacudida={}", prog_.opc_volumen, double(prog_.opc_zoom10) / 10.0, prog_.opc_sacudida);
  }
  zoom_ = float(prog_.opc_zoom10) / 10.0f, g_opciones.volumen = prog_.opc_volumen, g_opciones.sacudida = prog_.opc_sacudida != 0;
  FondoMenu(fondo, W, H, 0.55f, "llanura");
  Titulo(dl, W, 110, M::L("Opciones", "Options", "Options", "Optionen", "Opzioni"), 64);
  const float pw = 1100 * k_, ph = 650 * k_, x0 = (W - pw) * 0.5f, y0 = 200 * k_;
  if (!DibujarNueveTex(dl, T3(kTiendaPanel), 52, 52, 52, 52, ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), pw / 1400.0f))
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(8, 30, 50, 224), 12 * k_);
  const char* nombres[4] = {M::L("Volumen del modo", "Mode volume", "Volume du mode", "Lautstärke des Modus", "Volume della modalità"),
                            M::L("Zoom de serie", "Default zoom", "Zoom par défaut", "Standard-Zoom", "Zoom predefinito"),
                            M::L("Sacudida de pantalla", "Screen shake", "Secousse d'écran", "Bildschirmwackeln", "Scossa dello schermo"),
                            M::L("Autoapuntado", "Auto-aim", "Visée auto", "Autozielen", "Mira automatica")};
  for (int i = 0; i < n_opciones; ++i) {
    const bool el = i == p.opc_sel;
    const float y = y0 + 90 * k_ + float(i) * 130 * k_, bh = 100 * k_;
    if (el) dl->AddRectFilled(ImVec2(x0 + 50 * k_, y), ImVec2(x0 + pw - 50 * k_, y + bh), IM_COL32(255, 200, 40, 60), 14 * k_);
    S::MenuTextB3(dl, ImVec2(x0 + 70 * k_ + 250 * k_, y + bh * 0.5f), 48 * k_, nombres[i], el, false, 500 * k_);
    char v[32];
    if (i == 0) std::snprintf(v, sizeof v, "%d %%", prog_.opc_volumen);
    else if (i == 1) std::snprintf(v, sizeof v, "%.1f", double(prog_.opc_zoom10) / 10.0);
    else if (i == 2) std::snprintf(v, sizeof v, "%s", prog_.opc_sacudida ? M::L("Sí", "Yes", "Oui", "Ja", "Sì") : M::L("No", "No", "Non", "Nein", "No"));
    else std::snprintf(v, sizeof v, "%s", prog_.opc_autoapunte ? M::L("Sí", "Yes", "Oui", "Ja", "Sì") : M::L("No", "No", "Non", "Nein", "No"));
    const float bx0 = x0 + 600 * k_, bx1 = x0 + pw - 210 * k_;
    if (i < 2) {   // barra
      const float f = i == 0 ? float(prog_.opc_volumen) / 100.0f : float(prog_.opc_zoom10 - 10) / 12.0f;
      dl->AddRectFilled(ImVec2(bx0, y + bh * 0.5f - 8 * k_), ImVec2(bx1, y + bh * 0.5f + 8 * k_), IM_COL32(0, 0, 0, 140), 8 * k_);
      dl->AddRectFilled(ImVec2(bx0, y + bh * 0.5f - 8 * k_), ImVec2(bx0 + (bx1 - bx0) * f, y + bh * 0.5f + 8 * k_), IM_COL32(255, 210, 80, 255), 8 * k_);
      dl->AddCircleFilled(ImVec2(bx0 + (bx1 - bx0) * f, y + bh * 0.5f), 16 * k_, IM_COL32(255, 255, 255, 255), 24);
    }
    TxtC(dl, 46 * k_, ImVec2(i < 2 ? bx1 + 75 * k_ : (bx0 + bx1) * 0.5f, y + bh * 0.5f), el ? IM_COL32(255, 230, 120, 255) : IM_COL32(220, 225, 240, 255), v);
  }
  TxtC(dl, 30 * k_, ImVec2(W * 0.5f, y0 + ph + 56 * k_), IM_COL32(210, 220, 245, 240),
       M::L("Izquierda / derecha cambia el valor. El zoom de serie es el de la partida al empezar.", "Left / right changes the value. The default zoom is the one you start the run with.",
            "Gauche / droite change la valeur. Le zoom par défaut est celui du début de partie.", "Links / rechts ändert den Wert. Der Standard-Zoom gilt beim Start der Partie.",
            "Sinistra / destra cambia il valore. Lo zoom predefinito è quello a inizio partita."));
  Ayuda(dl, W, H, nullptr, M::L("Volver", "Back", "Retour", "Zurück", "Indietro"));
}

// ---------------------------------------------------------------- A12 / B11: como jugar
void Modo::ComoJugar(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  if (Recorrido() && t_pant_ > 3.0f) pad |= kB;
  if (pad & kB) {
    Sonar("atras", 5, 0);
    return Ir(Pantalla(kPantPortada));
  }
  FondoMenu(fondo, W, H, 0.6f, "llanura");
  Titulo(dl, W, 100, M::L("Cómo jugar", "How to play", "Comment jouer", "Spielanleitung", "Come si gioca"), 64);
  struct Paso { const char* arte; const char* icono; const char* t; const char* d; };
  const Paso pasos[5] = {
      {"tutorial_01_moverse.png", "fisica_01", M::L("1. Muévete", "1. Move", "1. Déplace-toi", "1. Bewegen", "1. Muoviti"),
       M::L("Palanca o cruceta (flechas / WASD). Tus técnicas atacan solas: tú solo esquivas.", "Stick or d-pad (arrows / WASD). Your techniques fire on their own: you just dodge.",
            "Joystick ou croix (flèches / WASD). Tes techniques attaquent seules : tu esquives.", "Stick oder Steuerkreuz (Pfeile / WASD). Deine Techniken greifen von selbst an: du weichst aus.",
            "Levetta o croce (frecce / WASD). Le tue tecniche attaccano da sole: tu schivi.")},
      {"tutorial_02_gemas.png", "utilidad_05", M::L("2. Recoge", "2. Collect", "2. Ramasse", "2. Einsammeln", "2. Raccogli"),
       M::L("Las gemas dan experiencia; los zeni van a la tienda; rompe cajas y barriles para más botín.", "Gems give experience; zeni go to the shop; break crates and barrels for more loot.",
            "Les gemmes donnent de l'expérience ; les zeni vont à la boutique ; casse caisses et tonneaux.", "Edelsteine geben Erfahrung; Zeni gehen in den Laden; zerstöre Kisten und Fässer für Beute.",
            "Le gemme danno esperienza; gli zeni vanno al negozio; rompi casse e barili per più bottino.")},
      {"tutorial_03_subir_nivel.png", "ki_01", M::L("3. Sube de nivel", "3. Level up", "3. Monte de niveau", "3. Stufenaufstieg", "3. Sali di livello"),
       M::L("Elige 1 de 3 mejoras. Puedes re-sortear (zeni), saltar o desterrar una opción.", "Pick 1 of 3 upgrades. You can reroll (zeni), skip or banish an option.",
            "Choisis 1 amélioration sur 3. Tu peux retirer (zeni), passer ou bannir une option.", "Wähle 1 von 3 Verbesserungen. Du kannst neu würfeln (Zeni), überspringen oder verbannen.",
            "Scegli 1 miglioramento su 3. Puoi ritirare (zeni), saltare o bandire un'opzione.")},
      {"tutorial_04_evolucion.png", "evolucion_01", M::L("4. Evoluciona", "4. Evolve", "4. Évolue", "4. Entwickeln", "4. Evolvi"),
       M::L("Una técnica al nivel 5 más su pasiva se convierte en una evolución al abrir un cofre.", "A level 5 technique plus its passive evolves when you open a chest.",
            "Une technique niveau 5 et son passif évoluent à l'ouverture d'un coffre.", "Eine Technik auf Stufe 5 und ihr Passiv entwickeln sich beim Öffnen einer Truhe.",
            "Una tecnica al livello 5 e la sua passiva evolvono aprendo un forziere.")},
      {"tutorial_05_jefe.png", "ki_08", M::L("5. Vence al jefe", "5. Beat the boss", "5. Bats le boss", "5. Besiege den Boss", "5. Batti il boss"),
       M::L("Cuando llega el jefe, aguanta y derrótalo para ganar. Gana, o llega al minuto 10, para abrir el siguiente escenario.", "When the boss arrives, hold out and defeat it to win. Win, or reach minute 10, to unlock the next stage.",
            "Quand le boss arrive, tiens bon et bats-le pour gagner. Gagne, ou atteins la minute 10, pour ouvrir la scène suivante.", "Wenn der Boss kommt, halte durch und besiege ihn. Gewinne oder erreiche Minute 10, um die nächste Stufe freizuschalten.",
            "Quando arriva il boss, resisti e sconfiggilo per vincere. Vinci, o arriva al minuto 10, per aprire lo scenario successivo.")}};
  const float cw = 600 * k_, ch = 330 * k_, hueco = 24 * k_;
  for (int i = 0; i < 5; ++i) {
    const int fila = i / 3, col = i % 3, nfila = fila == 0 ? 3 : 2;
    const float x = (W - (float(nfila) * cw + float(nfila - 1) * hueco)) * 0.5f + float(col) * (cw + hueco), y = 170 * k_ + float(fila) * (ch + hueco + 70 * k_);
    const ps2::Tex* art = Arte5(pasos[i].arte);
    const float iw = cw, ih = cw * 0.5625f * 0.52f;   // viñeta recortada a media altura
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw, y + ch + 70 * k_), IM_COL32(8, 24, 46, 235), 12 * k_);
    dl->AddRect(ImVec2(x, y), ImVec2(x + cw, y + ch + 70 * k_), IM_COL32(255, 170, 40, 255), 12 * k_, 0, 3 * k_);
    if (art) dl->AddImageRounded(art->id(), ImVec2(x + 8 * k_, y + 8 * k_), ImVec2(x + iw - 8 * k_, y + 8 * k_ + ih), ImVec2(0, 0.24f), ImVec2(1, 0.76f), IM_COL32_WHITE, 8 * k_);
    else {
      Pieza(dl, "ui_casilla_mejora", ImVec2(x + cw * 0.5f, y + 8 * k_ + ih * 0.5f), ih * 0.8f);
      IconoEn(dl, pasos[i].icono, ImVec2(x + cw * 0.5f - ih * 0.28f, y + 8 * k_ + ih * 0.5f - ih * 0.28f), ImVec2(x + cw * 0.5f + ih * 0.28f, y + 8 * k_ + ih * 0.5f + ih * 0.28f));
    }
    S::MenuTextB3(dl, ImVec2(x + cw * 0.5f, y + ih + 42 * k_), 42 * k_, pasos[i].t, true, false, cw * 0.9f);
    Txt(dl, 28 * k_, ImVec2(x + 22 * k_, y + ih + 78 * k_), IM_COL32(230, 235, 250, 255), pasos[i].d, cw - 44 * k_);
  }
  Ayuda(dl, W, H, nullptr, M::L("Volver", "Back", "Retour", "Zurück", "Indietro"));
}

// ---------------------------------------------------------------- C08: coleccion (tecnicas, evoluciones, pasivas, enemigos, jefes)
void Modo::Coleccion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  EstadoP2& p = p2_;
  if (Recorrido() && t_pant_ > 3.0f) pad |= kB;
  if (pad & kB) {
    Sonar("atras", 5, 0);
    return Ir(Pantalla(kPantPortada));
  }
  constexpr int kTabs = 5;
  // las listas de cada pestaña: indices en datos_
  std::vector<int> lista;
  const auto cargar = [&](int tab) {
    lista.clear();
    if (tab == 0 || tab == 1)
      for (int i = 0; i < int(datos_.tecnicas.size()); ++i)
        if ((datos_.tecnicas[size_t(i)].evoluciona_de >= 0) == (tab == 1)) lista.push_back(i);
    if (tab == 2)
      for (int i = 0; i < int(datos_.pasivas.size()); ++i) lista.push_back(i);
    if (tab == 3)
      for (int i = 0; i < int(datos_.enemigos.size()); ++i) lista.push_back(i);
    if (tab == 4)
      for (int i = 0; i < int(datos_.jefes.size()); ++i) lista.push_back(i);
  };
  if (pad & kLB) p.col_tab = (p.col_tab + kTabs - 1) % kTabs, p.col_sel = 0, Sonar("mover", 2, 0);
  if (pad & kRB) p.col_tab = (p.col_tab + 1) % kTabs, p.col_sel = 0, Sonar("mover", 2, 0);
  cargar(p.col_tab);
  const int n = int(lista.size()), cols = 6;
  p.col_sel = std::clamp(p.col_sel, 0, std::max(0, n - 1));
  if (n > 0) {
    if (pad & kIzq) p.col_sel = (p.col_sel + n - 1) % n, Sonar("mover", 2, 0);
    if (pad & kDer) p.col_sel = (p.col_sel + 1) % n, Sonar("mover", 2, 0);
    if (pad & kAbajo) p.col_sel = std::min(p.col_sel + cols, n - 1), Sonar("mover", 2, 0);
    if (pad & kArriba) p.col_sel = std::max(p.col_sel - cols, 0), Sonar("mover", 2, 0);
  }
  const auto visto = [&](int tab, int i) {
    if (tab <= 1) return i < 64 && ((prog_.vistos_tec >> i) & 1u);
    if (tab == 2) return i < 64 && ((prog_.vistos_pas >> i) & 1u);
    if (tab == 3) return i < 32 && ((prog_.vistos_enem >> i) & 1u);
    return i < 32 && ((prog_.vistos_jefe >> i) & 1u);
  };
  FondoMenu(fondo, W, H, 0.6f, "llanura");
  Titulo(dl, W, 90, M::L("Colección", "Collection", "Collection", "Sammlung", "Collezione"), 60);
  const char* tabs[kTabs] = {M::L("Técnicas", "Techniques", "Techniques", "Techniken", "Tecniche"), M::L("Evoluciones", "Evolutions", "Évolutions", "Entwicklungen", "Evoluzioni"),
                             M::L("Pasivas", "Passives", "Passifs", "Passive", "Passive"), M::L("Enemigos", "Enemies", "Ennemis", "Gegner", "Nemici"),
                             M::L("Jefes", "Bosses", "Boss", "Bosse", "Boss")};
  // pestañas con su contador "X de N"
  const float tw = 300 * k_, tx0 = (W - float(kTabs) * tw) * 0.5f;
  for (int t = 0; t < kTabs; ++t) {
    std::vector<int> l2;
    std::swap(l2, lista);
    cargar(t);
    int vistos = 0;
    for (const int i : lista) vistos += visto(t, i);
    const int total = int(lista.size());
    std::swap(l2, lista);
    const bool el = t == p.col_tab;
    const ImVec2 a(tx0 + float(t) * tw + 6 * k_, 150 * k_), b(tx0 + float(t + 1) * tw - 6 * k_, 150 * k_ + 78 * k_);
    dl->AddRectFilled(a, b, el ? IM_COL32(255, 190, 50, 230) : IM_COL32(8, 24, 46, 225), 10 * k_);
    dl->AddRect(a, b, IM_COL32(255, 170, 40, 255), 10 * k_, 0, 2 * k_);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s  %d/%d", tabs[t], vistos, total);
    TxtMax(dl, 28 * k_, ImVec2(a.x + 14 * k_, a.y + 39 * k_ - 28 * k_ * 0.55f), el ? IM_COL32(20, 20, 30, 255) : IM_COL32(230, 235, 250, 255), buf, b.x - a.x - 28 * k_);
  }
  // rejilla
  const float cell = 168 * k_, gap = 18 * k_, gx = 80 * k_, gy = 260 * k_;
  for (int q = 0; q < n; ++q) {
    const int i = lista[size_t(q)];
    const bool el = q == p.col_sel, v = visto(p.col_tab, i);
    const ImVec2 a(gx + float(q % cols) * (cell + gap), gy + float(q / cols) * (cell + gap)), b(a.x + cell, a.y + cell), c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    if (el) dl->AddRectFilled(ImVec2(a.x - 8 * k_, a.y - 8 * k_), ImVec2(b.x + 8 * k_, b.y + 8 * k_), IM_COL32(255, 200, 40, 90), 14 * k_);
    Pieza(dl, "ui_casilla_mejora", c, cell);
    const char* ico = p.col_tab <= 1 ? datos_.tecnicas[size_t(i)].icono.c_str() : p.col_tab == 2 ? datos_.pasivas[size_t(i)].icono.c_str() : nullptr;
    if (ico) {
      IconoEn(dl, ico, ImVec2(a.x + cell * 0.15f, a.y + cell * 0.15f), ImVec2(b.x - cell * 0.15f, b.y - cell * 0.15f), v ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));   // sin ver: silueta negra
    } else {
      TxtMax(dl, 24 * k_, ImVec2(a.x + 6 * k_, c.y - 12 * k_), v ? IM_COL32(255, 225, 120, 255) : IM_COL32(120, 125, 150, 255),
             v ? (p.col_tab == 3 ? datos_.enemigos[size_t(i)].id.c_str() : Es(datos_.jefes[size_t(i)].txt)) : "?", cell - 12 * k_);
    }
    if (!v) TxtC(dl, 56 * k_, c, IM_COL32(255, 255, 255, 230), "?");
  }
  // detalle de la elegida (derecha)
  const float dx = W * 0.66f, dw = W - dx - 60 * k_, dy = 270 * k_;
  dl->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + dw, dy + 640 * k_), IM_COL32(8, 24, 46, 235), 14 * k_);
  dl->AddRect(ImVec2(dx, dy), ImVec2(dx + dw, dy + 640 * k_), IM_COL32(255, 170, 40, 255), 14 * k_, 0, 3 * k_);
  if (n > 0) {
    const int i = lista[size_t(p.col_sel)];
    const bool v = visto(p.col_tab, i);
    const ImVec2 mc(dx + dw * 0.5f, dy + 170 * k_);
    char buf[256] = {};
    const char *nombre = "???", *desc = "";
    if (p.col_tab <= 1) {
      const DefTecnica& t = datos_.tecnicas[size_t(i)];
      if (v) nombre = Es(t.txt), desc = Desc(t.txt);
      // pista de sinergia: la base dice con que evoluciona; la evolucion, que pide
      if (p.col_tab == 0) {
        for (const DefTecnica& e : datos_.tecnicas)
          if (e.evoluciona_de == i && e.pasiva >= 0 && size_t(e.pasiva) < datos_.pasivas.size() && v)
            std::snprintf(buf, sizeof buf, "%s %s + %s", M::L("Mejor con:", "Best with:", "Idéal avec :", "Am besten mit:", "Meglio con:"), Es(datos_.pasivas[size_t(e.pasiva)].txt), M::L("evoluciona", "it evolves", "elle évolue", "sie entwickelt sich", "evolve"));
      } else if (t.evoluciona_de >= 0 && t.pasiva >= 0 && size_t(t.pasiva) < datos_.pasivas.size()) {
        std::snprintf(buf, sizeof buf, "%s %s %s %s", M::L("Pista:", "Hint:", "Indice :", "Tipp:", "Indizio:"), Es(datos_.tecnicas[size_t(t.evoluciona_de)].txt), M::L("al máximo +", "maxed +", "au max +", "auf Max +", "al massimo +"), Es(datos_.pasivas[size_t(t.pasiva)].txt));
      }
      IconoEn(dl, t.icono.c_str(), ImVec2(mc.x - 90 * k_, mc.y - 90 * k_), ImVec2(mc.x + 90 * k_, mc.y + 90 * k_), v ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));
      if (!v) TxtC(dl, 120 * k_, mc, IM_COL32(150, 165, 215, 255), "?");
    } else if (p.col_tab == 2) {
      const DefPasiva& t = datos_.pasivas[size_t(i)];
      if (v) nombre = Es(t.txt), desc = Desc(t.txt);
      IconoEn(dl, t.icono.c_str(), ImVec2(mc.x - 90 * k_, mc.y - 90 * k_), ImVec2(mc.x + 90 * k_, mc.y + 90 * k_), v ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));
      if (!v) TxtC(dl, 120 * k_, mc, IM_COL32(150, 165, 215, 255), "?");
    } else {
      const bool boss = p.col_tab == 4;
      const int fid = boss ? datos_.jefes[size_t(i)].fid : datos_.enemigos[size_t(i)].fid;
      if (v) {
        nombre = boss ? Es(datos_.jefes[size_t(i)].txt) : datos_.enemigos[size_t(i)].id.c_str();
        const Modelo m = ModeloDe(fid);
        DibujarFiguraEnRect(fondo, ImVec2(dx + dw * 0.5f - 130 * k_, dy + 20 * k_), ImVec2(dx + dw * 0.5f + 130 * k_, dy + 330 * k_), m.b3, m.forma, fid, 0.3f * std::sin(t_ * 0.8f), kClipEspera, t_);
        if (boss) std::snprintf(buf, sizeof buf, "%s: %.0f", M::L("Vida", "Health", "Vie", "Leben", "Vita"), double(datos_.jefes[size_t(i)].vida));
        else std::snprintf(buf, sizeof buf, "%s %.0f  %s %.1f", M::L("Vida", "Health", "Vie", "Leben", "Vita"), double(datos_.enemigos[size_t(i)].vida), M::L("Velocidad", "Speed", "Vitesse", "Tempo", "Velocità"), double(datos_.enemigos[size_t(i)].velocidad));
      } else {
        TxtC(dl, 150 * k_, mc, IM_COL32(120, 130, 170, 255), "?");
      }
    }
    S::MenuTextB3(dl, ImVec2(dx + dw * 0.5f, dy + 360 * k_), 40 * k_, nombre, true, false, dw * 0.9f);
    if (v && desc[0]) Txt(dl, 30 * k_, ImVec2(dx + 24 * k_, dy + 405 * k_), IM_COL32(230, 235, 250, 255), desc, dw - 48 * k_);
    if (!v) TxtC(dl, 26 * k_, ImVec2(dx + dw * 0.5f, dy + 420 * k_), IM_COL32(190, 195, 220, 255), M::L("Aún no la has visto: aparece en partida.", "Not seen yet: it shows up in a run.", "Pas encore vu : il apparaît en partie.", "Noch nicht gesehen: erscheint in einer Partie.", "Non ancora vista: compare in partita."));
    if (buf[0]) Txt(dl, 30 * k_, ImVec2(dx + 24 * k_, dy + 520 * k_), IM_COL32(255, 210, 110, 255), buf, dw - 48 * k_);
  }
  Ayuda(dl, W, H, nullptr, M::L("Volver", "Back", "Retour", "Zurück", "Indietro"));
  S::HelpLine(dl, ImVec2(W * 0.5f, H - 100 * k_), 28 * k_, {{"LB", M::L("Pestaña", "Tab", "Onglet", "Reiter", "Scheda")}, {"RB", M::L("Pestaña", "Tab", "Onglet", "Reiter", "Scheda")}});
}

// ---------------------------------------------------------------- C08: logros (lista con su condicion y su recompensa)
void Modo::Logros(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  EstadoP2& p = p2_;
  if (Recorrido() && t_pant_ > 3.0f) pad |= kB;
  if (pad & kB) {
    Sonar("atras", 5, 0);
    return Ir(Pantalla(kPantPortada));
  }
  const std::vector<Logro>& ls = ListaLogros();
  const int n = int(ls.size());
  if (n > 0) {
    if (pad & kArriba) p.log_sel = (p.log_sel + n - 1) % n, Sonar("mover", 2, 0);
    if (pad & kAbajo) p.log_sel = (p.log_sel + 1) % n, Sonar("mover", 2, 0);
    if (pad & kIzq) p.log_sel = std::max(0, p.log_sel - 6), Sonar("mover", 2, 0);
    if (pad & kDer) p.log_sel = std::min(n - 1, p.log_sel + 6), Sonar("mover", 2, 0);
  }
  p.log_sel = std::clamp(p.log_sel, 0, std::max(0, n - 1));
  FondoMenu(fondo, W, H, 0.6f, "llanura");
  Titulo(dl, W, 90, M::L("Logros", "Achievements", "Succès", "Erfolge", "Traguardi"), 60);
  int hechos = 0;
  for (int i = 0; i < n; ++i) hechos += int((prog_.logros >> i) & 1u);
  char buf[96];
  std::snprintf(buf, sizeof buf, M::L("%d de %d", "%d of %d", "%d sur %d", "%d von %d", "%d di %d"), hechos, n);
  S::MenuTextB3(dl, ImVec2(W - 160 * k_, 90 * k_), 44 * k_, buf, true, false, 260 * k_);
  const int visibles = 7;
  const int primero = std::clamp(p.log_sel - 3, 0, std::max(0, n - visibles));
  const float rw = W * 0.74f, rh = 110 * k_, x0 = (W - rw) * 0.5f, y0 = 170 * k_;
  for (int r = 0; r < visibles && primero + r < n; ++r) {
    const int i = primero + r;
    const Logro& l = ls[size_t(i)];
    const bool el = i == p.log_sel, ok = (prog_.logros >> i) & 1u;
    const float y = y0 + float(r) * (rh + 12 * k_);
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + rw, y + rh), ok ? IM_COL32(20, 54, 40, 235) : IM_COL32(8, 24, 46, 235), 12 * k_);
    dl->AddRect(ImVec2(x0, y), ImVec2(x0 + rw, y + rh), el ? IM_COL32(255, 210, 70, 255) : ok ? IM_COL32(110, 220, 140, 255) : IM_COL32(90, 130, 190, 255), 12 * k_, 0, (el ? 5.0f : 2.5f) * k_);
    Pieza(dl, "ui_casilla_mejora", ImVec2(x0 + 70 * k_, y + rh * 0.5f), 90 * k_);
    IconoEn(dl, l.icono.c_str(), ImVec2(x0 + 34 * k_, y + rh * 0.5f - 36 * k_), ImVec2(x0 + 106 * k_, y + rh * 0.5f + 36 * k_), ok ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));
    if (!ok) TxtC(dl, 40 * k_, ImVec2(x0 + 70 * k_, y + rh * 0.5f), IM_COL32(255, 255, 255, 220), "?");
    TxtMax(dl, 34 * k_, ImVec2(x0 + 140 * k_, y + 14 * k_), ok ? IM_COL32(255, 235, 150, 255) : IM_COL32(215, 220, 235, 255), LogroTxt(l, false), rw - 480 * k_);
    Txt(dl, 24 * k_, ImVec2(x0 + 140 * k_, y + 58 * k_), IM_COL32(205, 212, 235, 255), LogroTxt(l, true), rw - 480 * k_);
    float rx = x0 + rw - 20 * k_;
    if (l.zeni > 0) {
      char z[24];
      std::snprintf(z, sizeof z, "+%d zeni", l.zeni);
      rx -= Medir(28 * k_, z).x;
      Txt(dl, 28 * k_, ImVec2(rx, y + 16 * k_), ok ? IM_COL32(150, 255, 170, 255) : IM_COL32(255, 230, 120, 255), z);
    }
    if (l.desbloquea.rfind("luchador:", 0) == 0) {
      std::snprintf(buf, sizeof buf, "%s: %s", M::L("Abre", "Unlocks", "Débloque", "Schaltet frei", "Sblocca"), l.desbloquea.c_str() + 9);
      TxtMax(dl, 24 * k_, ImVec2(x0 + rw - 300 * k_, y + 62 * k_), IM_COL32(120, 200, 255, 255), buf, 280 * k_);
    } else if (ok) {
      TxtMax(dl, 22 * k_, ImVec2(x0 + rw - 300 * k_, y + 66 * k_), IM_COL32(150, 255, 170, 255), M::L("¡Conseguido!", "Done!", "Obtenu !", "Geschafft!", "Ottenuto!"), 280 * k_);
    }
  }
  Ayuda(dl, W, H, nullptr, M::L("Volver", "Back", "Retour", "Zurück", "Indietro"));
}

// ---------------------------------------------------------------- A15: clasificacion mundial desde el menu (tabla de ranking_online.cpp con sus filtros)
void Modo::Clasificacion(ImDrawList* fondo, ImDrawList* dl, float W, float H, uint16_t pad) {
  EstadoP2& p = p2_;
  if (Recorrido() && t_pant_ > 3.0f) pad |= kB;
  const bool local = RankingLocal();
  if (!(p.dicho & 16) && local) {
    p.dicho |= 16;
    REXLOG_INFO("dbz3 survivors: ranking tabla local filas=14");
  }
  if (!dbz3::rank::Activo() && !local) {   // el ranking es un ajuste aparte (apagado de serie)
    if (pad & kB) {
      Sonar("atras", 5, 0);
      return Ir(Pantalla(kPantPortada));
    }
    FondoMenu(fondo, W, H, 0.6f, "llanura");
    Titulo(dl, W, 110, M::L("Clasificación", "Ranking", "Classement", "Rangliste", "Classifica"), 64);
    const float pw = 1100 * k_, ph = 260 * k_, x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
    DibujarNueve(dl, "ui_ventana_dialogo", ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), 0.6f * k_);
    Txt(dl, 30 * k_, ImVec2(x0 + 70 * k_, y0 + 60 * k_), IM_COL32_WHITE,
        M::L("El ranking mundial está apagado. Enciéndelo en Ajustes rápidos > Modos > Ranking online (beta).", "The world ranking is off. Turn it on in Quick settings > Modes > Online ranking (beta).",
             "Le classement mondial est désactivé. Active-le dans Réglages rapides > Modes > Classement en ligne (bêta).", "Die Weltrangliste ist aus. Schalte sie unter Schnelleinstellungen > Modi > Online-Rangliste (Beta) ein.",
             "La classifica mondiale è spenta. Attivala in Impostazioni rapide > Modalità > Classifica online (beta)."), pw - 140 * k_);
    return Ayuda(dl, W, H, nullptr, M::L("Volver", "Back", "Retour", "Zurück", "Indietro"));
  }
  FondoMenu(fondo, W, H, 0.5f, "llanura");
  const bool cerro = dbz3::rank::Tabla(dl, S::GameArea(ImGui::GetIO()), t_, pad, 0);
  if (!cerro && (pad & kB)) {
    Sonar("atras", 5, 0);
    Ir(Pantalla(kPantPortada));
  }
}

}  // namespace dbz3::survivors::interno
