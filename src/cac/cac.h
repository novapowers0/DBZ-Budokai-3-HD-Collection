// dbz3 - Creador de Personaje (CaC): slots de heroe, camara propia y catalogo de piezas.
// Frente 02 (studies 6). Modulo INDEPENDIENTE de heroe.cpp (que sigue siendo el que "viste"
// el cuerpo en combate): aqui vive el ESTADO de los N heroes guardados, la camara del creador
// y el catalogo de piezas (cuerpo/cara/pelo/ropa/color/voz/aura). Lo consume la pantalla de
// creacion (assets_propios/ui) y los modos que ofrecen el heroe (Duelo/Tag Team).
//
// Guardado: user_data/dbz3/cac.toml (version 1; .tmp + rename y la anterior en .bak, como rpg.toml).
#ifndef DBZ3_CAC_H_
#define DBZ3_CAC_H_

#include <string>
#include <vector>

namespace dbz3::cac {

// --- catalogo de piezas (lo genera `mod center hd/cac/piezas_cac.py`) --------
// tipo: "cuerpo" | "cara" | "pelo" | "ropa" | "color" | "voz" | "aura"
struct Pieza {
  std::string tipo;
  int id = 0;              // indice dentro del tipo
  std::string nombre;      // rotulo legible
  int idx_afs = -1;        // entrada AFS de la pieza convertida (-1: nativa de B3)
  std::string archivo;     // bin #AMB de la pieza convertida ("" = nativa)
};
const std::vector<Pieza>& Catalogo();
const std::vector<Pieza>& PiezasDe(const std::string& tipo);
void CargarCatalogo(const std::string& path);   // lee piezas_cac.json/toml (opcional)

// --- un heroe guardado -------------------------------------------------------
// Color HSV tipo UT (Hue/Saturation/Brightness). Rangos [H]: h 0-360, s y v -100..100 (no medidos en UT).
struct Hsv { int h = 0, s = 0, v = 0; };
constexpr int kNumPartes = 4;   // "Part 1..Part 4" del Create de UT

struct Heroe {
  std::string nombre = "Heroe";
  int raza = 0, cuerpo = 0, traje = 0;
  int cara = 0, pelo = 0, ropa = 0;     // piezas UT (aun no conectadas al juego: frente 02)
  // Campos del Modo Heroe (heroe.cpp Exportar/Importar). Los IDs son los del juego, no indices:
  //   raza: indice en kRazas; cuerpo: ID del cuerpo; estilo: ID del luchador de estilo (kEstilos[].id);
  //   moveset: ID del donante (-1: el del estilo); color 0-31; voz 0-31; aura: indice de kAuras.
  int color = 0;                        // paleta del avatar (heroe::PonerColor)
  int voz = 0;                          // banco de voz (heroe::PonerVoz)
  int aura = 0;                         // aura (heroe::PonerAura)
  int estilo = 0;                       // ID del luchador de estilo (0 = Goku)
  int moveset = -1;                     // donante del moveset (-1: el del estilo)
  // Create (UT): color propio de cada parte, logo (indice; 0 = ninguno) y scouter (0 off / 1 on).
  Hsv parte[kNumPartes];
  int logo = 0, scouter = 0;
  // Edit (UT): pelo = pieza de pelo (ya en `pelo`); color base y color propio del pelo.
  Hsv pelo_c1, pelo_c2;
  int tecnicas[4] = {0, 0, 0, 0};       // capsulas de tecnica desbloqueadas con los maestros (H4; solo para mostrar)
  int xp = 0, nivel = 1;                // xp = combates ganados (manda); nivel = 1 + xp/3 (solo para leerlo)
  // studies 8 H1: progreso del Modo Heroe POR FICHA (antes global en hub_heroe.toml / rpg.toml).
  int saga = 0;                         // saga siguiente (crece sin tope: vueltas)
  int ph = 0, ph_vida = 0, ph_dano = 0; // puntos de habilidad sin gastar y gastados en vida / dano
  int maestros = 0;                     // bit i = maestro i entrenado
  int entreno[6] = {0, 0, 0, 0, 0, 0};  // % de entrenamiento con cada maestro (0..100)
  int progreso = 0;                     // 2 = el progreso ya vive en la ficha (migracion hecha una vez)
};
constexpr int kNumTecnicas = 4;
constexpr int kNumEntreno = 6;
constexpr int kProgresoFicha = 2;       // marcador `progreso = 2` de cada [[heroe]]
void ClampHsv(Hsv& c);                  // h 0..360, s y v -100..100
constexpr int kMaxSlots = 8;   // fichas de heroe (cac.toml)
constexpr int kVersion = 2;    // cac.toml v2: fichas + [[archivado]] (v1 se migra al guardar)

// --- slots -------------------------------------------------------------------
int NumSlots();
const Heroe& Slot(int i);
Heroe& SlotEditable(int i);
int SlotActivo();
void ElegirSlot(int i);
int NuevoSlot();                          // devuelve el indice del slot nuevo
void DuplicarSlot(int i);
void BorrarSlot(int i);                   // deja >= 1 slot siempre
void ArchivarSlot(int i);                 // mueve la ficha a [[archivado]] (deja >= 1)
const std::vector<Heroe>& Archivados();
int NumArchivados();
void AjustarSlot(int i);                  // clamp de todos los campos

// --- guardado (patron rpg.toml) ---------------------------------------------
std::string RutaPorDefecto();             // <cwd>/user_data/dbz3/cac.toml
bool Leer(const std::string& path);
bool Guardar(const std::string& path);

// --- camara propia del creador ----------------------------------------------
struct Camara {
  float yaw = 4.0f, pitch = 8.0f, dist = 2.6f;
  bool giro_auto = true;
  float spin = 18.0f;                     // grados/segundo
};
Camara& Cam();
void CamReset(int preset);                // 0 frente, 1 tres cuartos, 2 retrato, 3 cuerpo, 4 giro
void CamMover(float dyaw, float dpitch);  // orbita manual (para el giro_auto)
void CamZoom(float factor);
void CamTick(float dt);                   // girar autom. + limites

// --- enganche con el heroe en combate (lo aplica 03/heroe) -------------------
// Devuelve el heroe a vestir por el motor de modos (estilo + cuerpo + traje) y su
// aperitivo de color/aura. La parte de combate la conecta el frente 03.
struct Preset { int estilo, cuerpo, traje, moveset; };
Preset PresetSlot(int i);

}  // namespace dbz3::cac

#endif  // DBZ3_CAC_H_
