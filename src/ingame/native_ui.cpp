// dbz3 - Ver native_ui.h. Formatos (los mismos que leen mod center hd/roster_build.py y
// texturas_originales.py):
//   - AFS: "AFS\0", n (u32 LE) y tabla de (direccion, tamano) u32 LE desde +8.
//   - Entrada comprimida con XMemCompress "nativo": cabecera de 0x30 B (BE: magia 0FF512EE,
//     ventana +0x10, tamano sin comprimir u64 +0x18) y bloques (u32 tamano) de tramas LZX:
//     [FF, u16 sin comprimir, u16 comprimido] o [u16 comprimido] (0x8000 sin comprimir).
//   - Texturas: DDS normales ("DDS " + 124) dentro de la entrada; se reconocen por el XXH3 del
//     nivel base, el mismo hash que usa el pack de texturas HD.
#include "native_ui.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/system/lzx.h>

#include "../launcher/settings.h"
#include "../region.h"

namespace dbz3::ingame::native_ui {
namespace {

struct Piece {
  const char* name;
  int entry;        // data_usi.afs
  uint64_t hash;    // XXH3 del nivel base (nombre de la textura en el pack HD)
  bool trim;        // recortar al dibujo (los anfitriones se colocan por los pies)
  bool cmn = false; // H11: la entrada es de data_cmn.afs (efectos de combate) y no de data_usi.afs
};
constexpr Piece kPieces[] = {
    {"fondo_duelo", 2055, 0x13AF5A36B28A0842ull, false},  // Duelo: cielo rosa y arboles
    {"pergamino", 2033, 0xF35DB93F312ABC3Eull, false},    // cinta del titulo (rodillo verde y cordon)
    {"borde", 2033, 0x4B735AD364D513BAull, false},        // tira dorada de los lados (sombra a la izquierda)
    {"marco", 2033, 0xDECD0591337A311Bull, false},
    {"agua", 2033, 0x20EA5281A0037C1Dull, false},         // agua azul de la franja del texto elegido (512x128)
    {"panel", 2033, 0x4048A4AF7E6C64CFull, false},        // interior oscuro de las listas (4,7-309,350)
    {"filas_vs", 2055, 0x1732359AF24A2EFBull, false},     // 1P VS COM, 1P VS 2P...
    {"bocadillo", 2055, 0xBEFBE5B92F49B4F9ull, false},
    {"flechas", 2062, 0x31874164D2247BDEull, false},
    {"botones", 2062, 0xAEA34DDB47477372ull, false},      // LB LT RB RT Y B A X
    {"boton_back", 2033, 0x9607A868D438558Eull, false},
    {"boton_start", 2062, 0xE56FC5E5DEFE917Bull, false},
    {"anfitrion_tagteam_a", 2055, 0x356EB63B3D15A405ull, true},   // Kaio del Duelo
    {"anfitrion_tagteam_b", 2055, 0x44657FFB8C1D8B3Full, true},   // Kaio del Duelo
    {"anfitrion_online", 2055, 0x107B679B010365CDull, true},      // Kaio del Norte
    {"icono", 2045, 0x7E84F2B3B86A667Dull, true},                 // bola de 1 estrella: icono de la ventana
    {"bola_estrella", 2045, 0x7E84F2B3B86A667Dull, true},         // H11: la misma bola para Dragon Tactics (la de arriba la consume el icono de la ventana)
    // Skill Shop (tiendas de los retos, rpg.cpp)
    {"capsulas", 2064, 0x109EBFC124CBEB25ull, false},      // botellas roja, verde, amarilla y gris (31x83, cada 36 px)
    {"caja_zenie", 2053, 0xE607F676540C0AF1ull, false},    // caja del dinero con monedas (1,3-323,105)
    {"letras_zenie", 2053, 0xC1F09CA7DC159B3Cull, false},  // rotulo ZENIE (2,6-152,55)
    {"cifras_zenie", 2053, 0xF5EAAD6A69134362ull, false},  // cifras naranjas 0-9 y coma (1024x256, y 10-127)
    // marcador del combate (data_usi 0, 256x256): cifras 0-9 blancas con perfil (0-4 en y 4-34, 5-9 en y 40-70),
    // caras del contador de rivales, "/" (124,76-139,94), "V" y "%"; el juego las tine (el "2/2", naranja). Frente R6
    {"cifras_hud", 0, 0x38CE22710151CFA1ull, false},
    {"vs_torneo", 2047, 0xEEC9E52D040DC990ull, false},     // "VS" naranja del Torneo Mundial (0,4-236,103). R6
    // Modo Heroe, aspecto nuevo (HEROE-V04, heroe_ui_v2.h; solo con dbz3_heroe_ui_nueva). Hashes comprobados con
    // E:\DBZ3HD_trabajo\heroe_visual\verificar_piezas.py (misma regla que Extract: DdsSize + XXH3 del nivel base).
    {"heroe_kami", 2039, 0x74CCE71F92035CB7ull, false},           // 2039_13 Palacio de Kami 1024 (dibujo 0,0-900,672)
    {"heroe_kamehouse", 2062, 0xD84474804397FD5Eull, false},      // 2062_10 Kame House 1024 DXT5 (0,0-912,684)
    {"heroe_noche", 2045, 0x4C0F90CCAA4270A9ull, false},          // 2045_11 cielo de noche 1024 (1,1-900,672)
    {"heroe_peana", 2022, 0x4D0493CE5F3A98AEull, false},          // 2022_06 peana cian del selector (6,3-304,69)
    {"heroe_orbe", 2043, 0x2D52A07C07BDD47Dull, false},           // 2043_45 orbe gris (1,1-226,226), se tine
    {"heroe_placa_du", 2020, 0x466153F82C0C760Full, false},       // 2020_09 placa de fila de Dragon Universe (1,7-442,67)
    {"heroe_bolitas", 2049, 0xCEFD279693222E9Dull, false},        // 2049_16 bolitas 1-8 del Torneo (cada 60 px: 4,3-50,48)
    {"heroe_radar_aro", 2062, 0xD4C270A1543BBBB2ull, false},      // 2062_09 aro del radar 1024 DXT5 (13,4-655,686)
    {"heroe_radar_rejilla", 2062, 0xCEE1D646B05C574Aull, false},  // 2062_11 rejilla del radar 1024 DXT5 (7,3-600,600)
    {"heroe_barra_torneo", 2047, 0xC5EDF58D3C6CCEFFull, false},   // R23 HER-10: 2047_24 barra azul del Torneo (12,12-440,70); hash de ui_dump2
    // R23 HER-04 v3: cabezas redondas del selector (5 por tira, 72x72 cada 84 px desde 6,6); mismas texturas que 2027_03/07/08
    {"heroe_cabezas_13", 2047, 0x071C113EB3682CB4ull, false},     // 2047_13: Goku adulto (i0), Gohan, ...
    {"heroe_cabezas_15", 2047, 0x4313F609EE1DEC4Dull, false},     // 2047_15: Krilin, Piccolo, Goku, ...
    {"heroe_cabezas_16", 2047, 0x28055828F9157070ull, false},     // 2047_16: ..., Freezer (i3)
    {"heroe_cabezas_18", 2047, 0x025001542947F798ull, false},     // 2047_18: ..., Cell (i1), Majin Bu, Super Bu, Kid Bu (i2-i4)
    // R23 HER-05 v3: medidor de la pantalla Edit Skills (data_usi 1985; volcado en E:/DBZ3HD_trabajo/ui_dump3)
    {"edit_capsula", 1985, 0x0A01D9E75204436Bull, false},         // 1985_15 capsula azul, la crema de fondo y el relleno cian
    {"edit_placa", 1985, 0x7F77DF0EA4CA5830ull, false},           // 1985_16 placa azul clara (20,2-384,64)
    {"edit_cifras", 1985, 0x48CEFDAD3AD7E458ull, false},          // 1985_08 cifras blancas "1234567890%" (y 5-35)
    // HEROE-V05-B (mapa): fotos redondas (2022, 256x256) y panoramicas (2043, 512x512, opacas arriba) del selector de
    // escenarios, por FILA de nombres de escenarios.cpp (0 Red Ribbon, 1 Abuelo, 2 Buu, 3 Archipielago, 4 Kaioshin,
    // 5 Habitacion del Tiempo, 6 Namek, 7 Montanas, 8 Torneo, 9 Ciudad, 10 Llanuras, 11 Ring de Cell). Todas RGBA8, hash
    // comprobado con verificar_piezas.py (OK). La fila 11 (2022_21 tanque y restos / 2043_41) y la 7 (2022_28 / 2043_33,
    // mesetas) salen por el orden de la tabla y lo que se ve en la foto: comprobar en el juego.
    {"heroe_esc_0", 2022, 0x1459FE3A632D52D3ull, false},   // 2022_11
    {"heroe_esc_1", 2022, 0xED42B3C9503AE249ull, false},   // 2022_12
    {"heroe_esc_2", 2022, 0xA6495695CF6AC196ull, false},   // 2022_13
    {"heroe_esc_3", 2022, 0xA0918103AF443C40ull, false},   // 2022_14
    {"heroe_esc_4", 2022, 0x70A8441BC03889A2ull, false},   // 2022_15
    {"heroe_esc_5", 2022, 0xB86E7D4A0444E543ull, false},   // 2022_16
    {"heroe_esc_6", 2022, 0x784316AB2D06704Dull, false},   // 2022_17
    {"heroe_esc_7", 2022, 0x3CEE2708B04559D7ull, false},   // 2022_28
    {"heroe_esc_8", 2022, 0x8D4263706786E196ull, false},   // 2022_18
    {"heroe_esc_9", 2022, 0xF25EB6840462EBEFull, false},   // 2022_19
    {"heroe_esc_10", 2022, 0xB574979A1F881347ull, false},  // 2022_20
    {"heroe_esc_11", 2022, 0x91593ECBF8D0A966ull, false},  // 2022_21
    {"heroe_esc_desconocido", 2022, 0xD87EB3F504B36B55ull, false},   // 2022_22 foto "?" (cruce del Heroe: opcional sorteado)
    {"heroe_pan_0", 2043, 0xCB4820B7E8474677ull, false},   // 2043_40
    {"heroe_pan_1", 2043, 0x24FF92037759909Full, false},   // 2043_39
    {"heroe_pan_2", 2043, 0xCA785453BB1BD217ull, false},   // 2043_38
    {"heroe_pan_3", 2043, 0x3772978C7D38C2B1ull, false},   // 2043_37
    {"heroe_pan_4", 2043, 0x42C3E812C9ACC59Bull, false},   // 2043_29
    {"heroe_pan_5", 2043, 0x4674566058810B45ull, false},   // 2043_35
    {"heroe_pan_6", 2043, 0x5086BA98DA7AD075ull, false},   // 2043_34
    {"heroe_pan_7", 2043, 0xACE3ACA9FE4AFE22ull, false},   // 2043_33
    {"heroe_pan_8", 2043, 0x6C985448F48ACBF5ull, false},   // 2043_32
    {"heroe_pan_9", 2043, 0x88E842A661AA10EAull, false},   // 2043_31
    {"heroe_pan_10", 2043, 0x7DE363AA95603032ull, false},  // 2043_30
    {"heroe_pan_11", 2043, 0xD2BCC5D9A1A026D4ull, false},  // 2043_41
    // R23 SURV-4 (Survivors, arte del juego en vez de arte por encargar): portada de B3 (data_usi 2045). Hashes de E:/DBZ3HD_trabajo/ui_dump2.
    {"b3_logo_dbz", 2045, 0x0CBB5DF261053F53ull, true},     // 2045_07 "DRAGON BALL Z" 512x256
    {"b3_logo_budokai3", 2045, 0xF39E2E0D03DC4954ull, true}, // 2045_19 "BUDOKAI 3" 512x256
    {"b3_arte_cell", 2045, 0x4C79F71F97AE4312ull, true},     // 2045_20 Cell perfecto con la bola de energia 512x512
    {"b3_arte_freezer", 2045, 0x39BBCDADE6FC4EEEull, true},  // 2045_03 Freezer 512x512
    // Dragon Tactics, maquetas v2. Hashes comprobados con verificar_piezas.py (control OK) contra ui_dump2; todas RGBA8.
    {"tac_placa_du", 2020, 0x466153F82C0C760Full, false},        // 2020_09 placa de fila de Dragon Universe 512x128
    {"tac_carta_1", 2068, 0xD8C97E929EB280A4ull, false},         // 2068_01 carta del Skill List 1024x1024
    {"tac_carta_2", 2069, 0x3CF2D20F20E4CD0Bull, false},         // 2069_01 1024x1024
    {"tac_carta_3", 2070, 0x6EB41CBC98844662ull, false},         // 2070_01 1024x1024
    {"tac_carta_4", 2071, 0xB187642050D8A3FBull, false},         // 2071_01 1024x1024
    {"tac_bocadillo_lado", 2033, 0x34F4D7BBD6DA2299ull, false},  // 2033_09 1024x256
    {"tac_torneo_fondo", 2049, 0x73C63D7D90350FCBull, false},    // 2049_00 1024x1024
    {"tac_locutor", 2049, 0xFF7AFE5C73AA5EB4ull, false},         // 2049_05 512x512
    {"tac_flecha_cian", 2049, 0xEEFEECBA0E3B4A23ull, false},     // 2049_07 64x128
    {"tac_bolitas", 2049, 0xCEFD279693222E9Dull, false},         // 2049_16 512x64
    {"tac_destello", 2047, 0x900A4356A3BB4FB7ull, false},        // 2047_21 512x256
    {"tac_barra_azul", 2047, 0xC5EDF58D3C6CCEFFull, false},      // 2047_24 512x128
    {"tac_barra_roja", 2047, 0x3F89DD9955240F54ull, false},      // 2047_25 512x128
    {"tac_hud_placa", 0, 0xD457813D562A7ED8ull, false},          // POST 12/11: data_usi 0, tex 30 (512x128): placa azul de rayas del marcador de combate (4,1-361,68)
    {"tac_cabeza_0", 2047, 0x071C113EB3682CB4ull, false},        // 2047_13..20 512x128
    {"tac_cabeza_1", 2047, 0x45CEB0C32F6E2FE3ull, false},
    {"tac_cabeza_2", 2047, 0x4313F609EE1DEC4Dull, false},
    {"tac_cabeza_3", 2047, 0x28055828F9157070ull, false},
    {"tac_cabeza_4", 2047, 0x40D4B7C9E31945C9ull, false},
    {"tac_cabeza_5", 2047, 0x025001542947F798ull, false},
    {"tac_cabeza_6", 2047, 0xC32709676A6F6AA3ull, false},
    {"tac_cabeza_7", 2047, 0xE10C0477915093B8ull, false},
    // H11 (Dragon Tactics): texturas de efectos del combate del juego (data_cmn, DXT3). 504 = efectos comunes; 553 = efectos de Vegeta.
    {"fx_impacto", 504, 0x40AF1EBA62CD4A73ull, false, true},   // estallido radial blanco 256x256
    // CAC3: nodos 0x90 y 0x230 del #ACE 12 del banco común (BSP 504), fogonazo y núcleo.
    {"cac3_estallido", 504, 0x0A83F1084D8FD002ull, false, true}, // #AZT t16, 128x128
    {"cac3_nucleo", 504, 0x8AC8FAF388B7BC40ull, false, true},    // #AZT t0, 32x32
    {"fx_anillo", 504, 0xC5C95EF10DC7667Dull, false, true},    // onda de choque 64x64
    {"fx_nucleo", 504, 0xBC4107469032B90Full, false, true},    // punto blanco 32x32
    {"fx_rayo", 504, 0xA85E604E0EA9A4C9ull, false, true},      // chorro de energia azul 64x64
    {"fx_halo", 553, 0x9E585F231E5CD258ull, false, true},      // ovalo blanco suave 64x64
    {"fx_brillo", 553, 0x0F42A9F23C423FC0ull, false, true},    // bola de brillo rosa 64x64
    {"fx_chispas", 531, 0xC8611BC86718688Bull, false, true},   // chispas de golpe 128x128
    // POST2 12/11 (Survivors): texturas de los #ACE exactos del combate (atlas 2x2, sin recortar). 504 = banco comun 0; 533 = BSP de Goku.
    {"fx_golpe", 504, 0xC52ED001D62B4DA1ull, false, true},     // banco 0 #ACE 1 (rayas del golpe) t2 64x64
    {"fx_kibola", 504, 0x47355D0DC97D7DE8ull, false, true},    // banco 0 #ACE 9 (cabeza de la rafaga de ki, codigo 350) t15 64x64
    {"fx_kame_cabeza", 533, 0x2610A90B24AFA7E4ull, false, true},  // BSP 533 AMB0 #ACE 2 (cabeza del Kamehameha) t31 64x64
    {"fx_kame_brillo", 533, 0x5650F9AE8D679B95ull, false, true},  // t32 64x64
    {"fx_kame_raya", 533, 0xA57611EB6A4077BDull, false, true},    // t33 64x64
    // Dragon Tactics: bola y rayo de la tecnica de energia de cada unidad, tal como los liga el juego nativo (BSP del personaje en data_cmn;
    // tabla generada por mod center hd/tactics_fx.py: solo entrada y hash, la imagen sale de la copia del jugador). Nombres tkfx/<id>/bola|rayo.
#define TKFX(id, entrada, hbola, hrayo, r, g, b)     {"tkfx/" id "/bola", entrada, hbola, false, true}, {"tkfx/" id "/rayo", entrada, hrayo, false, true},
#include "../tactics/fx_nativo.inc"
#undef TKFX
};
// Cara del marcador: ID -> entrada de data_cmn (#AZT hud_face; textura 0 = forma normal).
// Rutas del Heroe (cruce de caminos, maqueta 1): retrato de dialogo de B3 (data_cmn 1416-1499, el busto de 541x530 arriba a la
// izquierda de su textura 1024) recortado a la cara (110,40-430,360). Solo los rivales que salen en un cruce; nunca 1464/1465/1486.
constexpr std::pair<int, int> kRetratos[] = {{7, 1436}, {20, 1461}, {21, 1462}, {29, 1469}, {30, 1471}, {33, 1477}, {42, 1491}, {43, 1492}};

constexpr std::pair<int, int> kFaces[] = {
    {0, 476},  {1, 474},  {2, 473},  {3, 472},  {4, 471},  {5, 478},  {6, 480},  {7, 500},
    {8, 494},  {9, 493},  {10, 482}, {11, 487}, {12, 496}, {13, 502}, {14, 492}, {15, 498},
    {16, 485}, {17, 497}, {18, 489}, {19, 486}, {20, 475}, {21, 490}, {27, 468}, {28, 452},
    {29, 453}, {30, 454}, {32, 456}, {33, 463}, {34, 460}, {35, 461}, {36, 462}, {37, 466},
    {38, 465}, {39, 457}, {40, 459}, {41, 455}, {42, 491}, {43, 464}};

std::mutex g_mu;
std::map<std::string, Image> g_images;
std::atomic<bool> g_started{false}, g_ready{false};

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }

std::vector<uint8_t> ReadEntry(const std::filesystem::path& afs, int n) {
  if (n < 0) return {};
  std::ifstream f(afs, std::ios::binary | std::ios::ate);
  const auto end = f.tellg();
  if (end < std::streamoff(8)) return {};
  const uint64_t file_size = static_cast<uint64_t>(end);
  f.seekg(0);
  uint8_t head[8];
  if (!f.read(reinterpret_cast<char*>(head), 8) || std::memcmp(head, "AFS", 3) != 0) return {};
  const uint64_t count = Le32(head + 4);
  const uint64_t table_end = 8 + count * 8;
  if (static_cast<uint64_t>(n) >= count || table_end > file_size) return {};
  uint8_t ent[8];
  f.seekg(8 + 8 * std::streamoff(n));
  if (!f.read(reinterpret_cast<char*>(ent), 8)) return {};
  const uint64_t offset = Le32(ent), length = Le32(ent + 4);
  // El lector de UI ya limita descompresión a 64 MiB; acotar también entrada cruda.
  if (!length || length > (64ull << 20) || offset < table_end ||
      offset > file_size || length > file_size - offset) return {};
  std::vector<uint8_t> raw(static_cast<size_t>(length));
  f.seekg(static_cast<std::streamoff>(offset));
  if (raw.empty() || !f.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()))) return {};
  // XMemCompress nativo: las tramas LZX seguidas, sin sus cabeceras, como hace el SDK con el xex
  if (raw.size() < 0x30 || Be32(raw.data()) != 0x0FF512EE) return raw;
  const uint32_t window = Be32(&raw[0x10]);
  const uint64_t total = uint64_t(Be32(&raw[0x18])) << 32 | Be32(&raw[0x1C]);
  if (total == 0 || total > (64ull << 20)) return {};
  std::vector<uint8_t> stream;
  stream.reserve(raw.size());
  for (size_t p = 0x30; p + 4 <= raw.size();) {
    const size_t end = std::min(raw.size(), p + 4 + Be32(&raw[p]));
    if (end == p + 4) break;
    for (size_t q = p + 4; q + 2 <= end;) {
      size_t c;
      if (raw[q] == 0xFF) {
        if (q + 5 > end) break;
        c = Be16(&raw[q + 3]);
        q += 5;
      } else {
        c = Be16(&raw[q]);
        q += 2;
      }
      if (c == 0 || q + c > end) break;
      stream.insert(stream.end(), raw.begin() + std::ptrdiff_t(q), raw.begin() + std::ptrdiff_t(q + c));
      q += c;
    }
    p = end;
  }
  std::vector<uint8_t> out(total);
  if (lzx_decompress(stream.data(), stream.size(), out.data(), out.size(), window, nullptr, 0) != 0) return {};
  return out;
}

// DXT3 (BC2): bloques de 4x4 con alfa de 4 bits (8 B) y color 565 (8 B).
void DecodeDxt3(const uint8_t* src, int w, int h, uint8_t* dst) {
  for (int by = 0; by < (h + 3) / 4; ++by) {
    for (int bx = 0; bx < (w + 3) / 4; ++bx, src += 16) {
      uint8_t c[4][3];
      for (int k = 0; k < 2; ++k) {
        const uint16_t v = uint16_t(src[8 + 2 * k] | src[9 + 2 * k] << 8);
        c[k][0] = uint8_t((v >> 11) * 255 / 31);
        c[k][1] = uint8_t(((v >> 5) & 63) * 255 / 63);
        c[k][2] = uint8_t((v & 31) * 255 / 31);
      }
      for (int j = 0; j < 3; ++j) {
        c[2][j] = uint8_t((2 * c[0][j] + c[1][j]) / 3);
        c[3][j] = uint8_t((c[0][j] + 2 * c[1][j]) / 3);
      }
      const uint32_t cbits = Le32(src + 12);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= w || y >= h) continue;
        uint8_t* o = dst + (size_t(y) * w + x) * 4;
        const int ci = (cbits >> (2 * i)) & 3;
        o[0] = c[ci][0];
        o[1] = c[ci][1];
        o[2] = c[ci][2];
        const int a4 = (src[i / 2] >> (4 * (i & 1))) & 15;
        o[3] = uint8_t(a4 * 17);
      }
    }
  }
}

// DXT5 (BC3): bloques de 4x4 con alfa interpolado (8 B) y color 565 (8 B).
void DecodeDxt5(const uint8_t* src, int w, int h, uint8_t* dst) {
  for (int by = 0; by < (h + 3) / 4; ++by) {
    for (int bx = 0; bx < (w + 3) / 4; ++bx, src += 16) {
      uint8_t a[8] = {src[0], src[1]};
      for (int i = 2; i < 8; ++i) {
        a[i] = a[0] > a[1] ? uint8_t(((8 - i) * a[0] + (i - 1) * a[1]) / 7)
                           : i < 6 ? uint8_t(((6 - i) * a[0] + (i - 1) * a[1]) / 5) : (i == 6 ? 0 : 255);
      }
      uint64_t abits = 0;
      for (int i = 0; i < 6; ++i) abits |= uint64_t(src[2 + i]) << (8 * i);
      uint8_t c[4][3];
      for (int k = 0; k < 2; ++k) {
        const uint16_t v = uint16_t(src[8 + 2 * k] | src[9 + 2 * k] << 8);
        c[k][0] = uint8_t((v >> 11) * 255 / 31);
        c[k][1] = uint8_t(((v >> 5) & 63) * 255 / 63);
        c[k][2] = uint8_t((v & 31) * 255 / 31);
      }
      for (int j = 0; j < 3; ++j) {
        c[2][j] = uint8_t((2 * c[0][j] + c[1][j]) / 3);
        c[3][j] = uint8_t((c[0][j] + 2 * c[1][j]) / 3);
      }
      const uint32_t cbits = Le32(src + 12);
      for (int i = 0; i < 16; ++i) {
        const int x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= w || y >= h) continue;
        uint8_t* o = dst + (size_t(y) * w + x) * 4;
        const int ci = (cbits >> (2 * i)) & 3;
        o[0] = c[ci][0];
        o[1] = c[ci][1];
        o[2] = c[ci][2];
        o[3] = a[(abits >> (3 * i)) & 7];
      }
    }
  }
}

int Shift(uint32_t m) {
  int s = 0;
  while (m && !(m & 1)) {
    m >>= 1;
    ++s;
  }
  return s;
}

// DDS en d (cabecera LE de 128 B: alto +12, ancho +16, formato +80/+84/+88, mascaras +92):
// tamano del nivel base si es RGBA8 o DXT5 y cabe en avail; si no, 0.
size_t DdsSize(const uint8_t* d, size_t avail) {
  if (avail < 128) return 0;
  const int h = int(Le32(d + 12)), w = int(Le32(d + 16));
  const uint32_t flags = Le32(d + 80), bits = Le32(d + 88);
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
  const size_t size = !(flags & 4) && bits == 32                           ? size_t(w) * h * 4
                      : (flags & 4) && (std::memcmp(d + 84, "DXT5", 4) == 0 || std::memcmp(d + 84, "DXT3", 4) == 0) ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16
                                                                            : 0;
  return 128 + size <= avail ? size : 0;
}

bool DecodeDds(const uint8_t* d, size_t avail, Image& img) {
  const size_t size = DdsSize(d, avail);
  if (!size) return false;
  const int h = int(Le32(d + 12)), w = int(Le32(d + 16));
  const bool dxt5 = Le32(d + 80) & 4, dxt3 = dxt5 && std::memcmp(d + 84, "DXT3", 4) == 0;
  img.w = w;
  img.h = h;
  img.rgba.assign(size_t(w) * h * 4, 0);
  if (dxt5) {
    (dxt3 ? DecodeDxt3 : DecodeDxt5)(d + 128, w, h, img.rgba.data());
    return true;
  }
  uint32_t m[4] = {Le32(d + 92), Le32(d + 96), Le32(d + 100), Le32(d + 104)};
  if (!m[0] && !m[1] && !m[2]) {   // sin mascaras: A8R8G8B8
    m[0] = 0x00FF0000;
    m[1] = 0x0000FF00;
    m[2] = 0x000000FF;
    m[3] = 0xFF000000;
  }
  const int sh[4] = {Shift(m[0]), Shift(m[1]), Shift(m[2]), Shift(m[3])};
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const uint32_t v = Le32(d + 128 + 4 * i);
    for (int k = 0; k < 4; ++k) img.rgba[4 * i + k] = m[k] ? uint8_t((v & m[k]) >> sh[k]) : 255;
  }
  return true;
}

void Trim(Image& img) {
  int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x)
      if (img.rgba[(size_t(y) * img.w + x) * 4 + 3] > 8) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
  if (x1 < 0) return;
  Image out;
  out.w = x1 - x0 + 1;
  out.h = y1 - y0 + 1;
  out.rgba.resize(size_t(out.w) * out.h * 4);
  for (int y = 0; y < out.h; ++y) {
    std::memcpy(&out.rgba[size_t(y) * out.w * 4], &img.rgba[(size_t(y + y0) * img.w + x0) * 4], size_t(out.w) * 4);
  }
  img = std::move(out);
}

void Extract(std::filesystem::path dir) {
  int found = 0, faces = 0, fx_tecnicas = 0;
  // EU no trae data_usi.afs: la de US si esta al lado, si no su data_eng.afs. Las piezas se
  // reconocen por el hash de la textura, asi que un archivo que no las tenga solo no las da.
  std::filesystem::path usi = dir / "data_usi.afs";
  std::error_code ec;
  for (const auto& alt : {dir.parent_path() / "us" / "data_usi.afs", dir / "data_eng.afs"}) {
    if (std::filesystem::exists(usi, ec)) break;
    usi = alt;
  }
  for (const bool cmn : {false, true}) {   // H11: primero las de data_usi, luego las de data_cmn
    std::vector<int> entries;
    for (const Piece& p : kPieces)
      if (p.cmn == cmn && std::find(entries.begin(), entries.end(), p.entry) == entries.end()) entries.push_back(p.entry);
    for (const int n : entries) {
      const std::vector<uint8_t> b = ReadEntry(cmn ? dir / "data_cmn.afs" : usi, n);
      for (size_t i = 0; i + 128 <= b.size(); ++i) {
        if (std::memcmp(&b[i], "DDS |", 5) != 0) continue;
        const size_t size = DdsSize(&b[i], b.size() - i);
        if (!size) continue;
        const uint64_t hash = XXH3_64bits(&b[i + 128], size);
        for (const Piece& p : kPieces) {
          Image img;
          if (p.cmn != cmn || p.entry != n || p.hash != hash || !DecodeDds(&b[i], b.size() - i, img)) continue;
          if (p.trim) Trim(img);
          std::lock_guard<std::mutex> lock(g_mu);
          if (std::strncmp(p.name, "tkfx/", 5) == 0) ++fx_tecnicas;
          g_images[p.name] = std::move(img);
          ++found;
        }
      }
    }
  }
  for (const auto& [id, n] : kFaces) {
    const std::vector<uint8_t> b = ReadEntry(dir / "data_cmn.afs", n);
    const auto z = std::search(b.begin(), b.end(), "#AZT", "#AZT" + 4) - b.begin();
    if (size_t(z) + 0x18 > b.size()) continue;
    const size_t idx = size_t(z) + Be32(&b[size_t(z) + 0x14]);
    if (idx + 4 > b.size()) continue;
    const size_t o = size_t(z) + Be32(&b[idx]);
    if (o + 24 > b.size()) continue;
    const size_t at = size_t(z) + Be32(&b[o + 20]);
    Image img;
    if (at < b.size() && DecodeDds(&b[at], b.size() - at, img)) {
      Image rec = img;   // la cara sin el margen transparente: los menus la centran sin estirarla
      Trim(rec);
      if (id == 0) REXLOG_INFO("dbz3 ui: cara 0 textura {}x{}, recortada {}x{}", img.w, img.h, rec.w, rec.h);
      std::lock_guard<std::mutex> lock(g_mu);
      g_images["caras_rec/" + std::to_string(id)] = std::move(rec);
      g_images["caras/" + std::to_string(id)] = std::move(img);
      ++faces;
    }
  }
  for (const auto& [id, n] : kRetratos) {   // mismo #AZT que las caras; recorte a la cara (la textura entera son 4 MB)
    const std::vector<uint8_t> b = ReadEntry(dir / "data_cmn.afs", n);
    const auto z = std::search(b.begin(), b.end(), "#AZT", "#AZT" + 4) - b.begin();
    if (size_t(z) + 0x18 > b.size()) continue;
    const size_t idx = size_t(z) + Be32(&b[size_t(z) + 0x14]);
    if (idx + 4 > b.size()) continue;
    const size_t o = size_t(z) + Be32(&b[idx]);
    if (o + 24 > b.size()) continue;
    const size_t at = size_t(z) + Be32(&b[o + 20]);
    Image img;
    if (at >= b.size() || !DecodeDds(&b[at], b.size() - at, img) || img.w < 430 || img.h < 360) continue;
    Image cara;
    cara.w = 320, cara.h = 320;
    cara.rgba.resize(size_t(cara.w) * cara.h * 4);
    for (int y = 0; y < cara.h; ++y)
      std::memcpy(&cara.rgba[size_t(y) * cara.w * 4], &img.rgba[(size_t(y + 40) * img.w + 110) * 4], size_t(cara.w) * 4);
    std::lock_guard<std::mutex> lock(g_mu);
    g_images["retratos/" + std::to_string(id)] = std::move(cara);
  }
  REXLOG_INFO("dbz3 ui: {} de {} piezas ({} de tecnicas de Tactics) y {} caras sacadas del juego ({})", found, std::size(kPieces), fx_tecnicas, faces,
              dir.string());
}

}  // namespace

void Prefetch() {
  const std::filesystem::path root = dbz3::HostDataRoot();
  if (root.empty() || g_started.exchange(true)) return;
  // B24: la carpeta de la region montada (dbz3_region) gana aunque exista la otra
  std::thread([dir = root / (dbz3::settings::Region() == "eu" ? "eu" : "us")]() {
    Extract(dir);
    g_ready = true;
  }).detach();
}

bool Ready() { return g_ready.load(); }

bool DecodificarDds(std::span<const uint8_t> dds, Image& out) {
  return dds.size() >= 128 && std::memcmp(dds.data(), "DDS ", 4) == 0 && DecodeDds(dds.data(), dds.size(), out);
}

bool CombatAtlas(int entrada) {
  const auto root=dbz3::HostDataRoot();if(root.empty())return false;
  const auto b=ReadEntry(root/(dbz3::settings::Region()=="eu"?"eu":"us")/"data_cmn.afs",entrada);
  size_t z=0;uint32_t cantidad=0,tabla=0;
  for(size_t p=0;p+32<=b.size();++p)if(!std::memcmp(b.data()+p,"#AZT",4)) {
    const uint32_t n=Be32(b.data()+p+16),t=Be32(b.data()+p+20);
    if(n>cantidad&&n<512&&t<=b.size()-p&&uint64_t(t)+4ull*n<=b.size()-p)z=p,cantidad=n,tabla=t;
  }
  if(!cantidad)return false;
  unsigned cargadas=0;
  for(uint32_t i=0;i<cantidad;++i) {
    const size_t o=z+Be32(b.data()+z+tabla+4*i);if(o+28>b.size())continue;
    const size_t d=z+Be32(b.data()+o+20),n=Be32(b.data()+o+24);
    if(d>b.size()||n>b.size()-d)continue;
    Image im;if(!DecodeDds(b.data()+d,n,im))continue;
    std::lock_guard<std::mutex> l(g_mu);
    g_images["fxreal/"+std::to_string(entrada)+"/"+std::to_string(i)]=std::move(im);++cargadas;
  }
  REXLOG_INFO("dbz3 survivors: atlas original FX BSP={} texturas={}",entrada,cargadas);
  return cargadas>0;
}

bool Take(const std::string& name, Image& out) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_images.find(name);
  if (it == g_images.end()) return false;
  out = std::move(it->second);
  g_images.erase(it);
  return true;
}

std::vector<uint8_t> LeerEntrada(const std::filesystem::path& afs, int n) { return ReadEntry(afs, n); }

}  // namespace dbz3::ingame::native_ui
