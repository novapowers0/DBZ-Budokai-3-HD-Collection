// dbz3 - Creador de Personaje (CaC): slots de heroe, camara propia y catalogo de piezas.
// Frente 02 (studies 6). Modulo INDEPENDIENTE de heroe.cpp (que sigue siendo el que "viste"
// el cuerpo en combate): aqui vive el ESTADO de los N heroes guardados, la camara del creador
// y el catalogo de piezas (cuerpo/cara/pelo/ropa/color/voz/aura). Lo consume la pantalla de
// creacion (assets_propios/ui) y los modos que ofrecen el heroe (Duelo/Tag Team).
//
// Guardado: user_data/dbz3/cac.toml (version 1; .tmp + rename y la anterior en .bak, como rpg.toml).
#ifndef DBZ3_CAC_H_
#define DBZ3_CAC_H_

#include <cstdint>
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
constexpr int kNumPartes = 6;   // UT: "Part 1..Part 4"; XV2 (C4): piel | pelo | torso | piernas | botas | guantes

constexpr int kNumCapas = 4, kMaxRecetas = 4, kMaxFormasReceta = 8;
struct Capa { int fid = -1; int parte = 0; int64_t quitar = 0; };   // quitar: bit i = mechon i de la capa quitado (CAC-16)
struct RecetaGuardada { std::string nombre, ficha; };   // ficha = texto de heroe::ArgsFicha (receta sin arte)

constexpr bool FidPeloCac2(int64_t fid) { return fid == 75 || fid == 80; }  // PEL1, anfitrión 264
struct Heroe {
  std::string nombre = "Heroe";
  int raza = 0, cuerpo = 0, traje = 0;
  int cara = 0, pelo = 0, ropa = 0;     // piezas UT (aun no conectadas al juego: frente 02)
  int pelo_fid_cac2 = -1;                // candidato FID validado; no altera los IDs de peinado 0..10
  // Campos del Modo Heroe (heroe.cpp Exportar/Importar). Los IDs son los del juego, no indices:
  //   raza: indice en kRazas; cuerpo: ID del cuerpo; estilo: ID del luchador de estilo (kEstilos[].id);
  //   moveset: ID del donante (-1: el del estilo); color 0-31; voz 0-31; aura: indice de kAuras.
  int foto_pose = 0;                     // 0 automatico, 1 brazos bajos, 2 brazos abiertos
  int foto_yaw = 22;                     // grados de la camara de retrato (P2 desde el lado opuesto)
  int color = 0;                        // paleta del avatar (heroe::PonerColor)
  int voz = 0;                          // banco de voz (heroe::PonerVoz)
  int aura = 0;                         // aura (heroe::PonerAura)
  std::string material17, pintura17;   // Ajustes regionales y referencias a lienzos locales, no arte.
  std::string texcol;                   // CAC-17: colores por entrada de textura "tex,entrada,h,s,v|..." (vista_heroe texcol_de)
  std::string aura_col, ki_col;         // C4: colores de aura / ki desbloqueados con los maestros ("#RRGGBB"; "" = el normal)
  int estilo = 0;                       // ID del luchador de estilo (0 = Goku)
  int64_t moveset_logico = -1;          // opcional: no se guarda una plaza dinamica
  int moveset = -1;                     // donante del moveset (-1: el del estilo)
  // Create (UT): color propio de cada parte, logo (indice; 0 = ninguno) y scouter (0 off / 1 on).
  Hsv parte[kNumPartes];
  int logo = 0, scouter = 0;
  // Edit (UT): pelo = pieza de pelo (ya en `pelo`); color base y color propio del pelo.
  Hsv pelo_c1, pelo_c2;
  Hsv piel_c;                           // 25/10: tono de piel unico (cara, brazos y la piel de las prendas)
  Hsv ojos_c;                           // 25/10: color de ojos (0,0,0 = el del rostro); `cara` = tipo de rostro (vista_heroe ROSTROS)
  int xv2[7] = {0, 0, 0, 0, 0, 0, 0};       // C4: piezas XV2 (cuerpo 110..115): modelo de Hair, FaceBase, Bust, Pants, Boots, Rist, FaceEye (0 = la primera)
  int b3_piezas[4] = {-1, -1, -1, -1};       // Luna 6: fid donante B3 por torso/piernas/guantes/botas (-1 = Base)
  int orig[4] = {-1, -1, -1, -1};       // CAC-11: pieza propia 3000-3061 por grupo (cabeza, mandibula, placa, cola; vista_heroe orig1..4)
  int b3_accesorio = -1;               // CAC-07a: fid del donante de accesorios (-1 = ninguno; vista_heroe b3_accesorios=)
  int64_t ocultar = 0;                  // CAC-07: bit i = tira (draw) i del traje oculta (vista_heroe.py ocultar=)
  int ocultar_de = -1;                  // cuerpo * 100 + traje al que se refieren esos bits (otro traje: no valen)
  // cac.toml v3 (CAC-15, CAC-10, CAC-19). capas: fid del luchador nativo + indice de su parte (-1: capa libre; ficha `capaN=FID:PARTE`).
  // recetas: hasta 4 recetas guardadas (nombre + texto de ficha, sin arte). forma_receta[i]: receta que sustituye la forma i (-1: la nativa).
  Capa capas[kNumCapas];
  RecetaGuardada recetas[kMaxRecetas];
  int forma_receta[kMaxFormasReceta] = {-1, -1, -1, -1, -1, -1, -1, -1};
  std::string forma_estallido[kMaxFormasReceta];   // color de estallido de la forma ("#RRGGBB"; "" = el nativo)
  int tecnicas[4] = {0, 0, 0, 0};       // capsulas de tecnica desbloqueadas con los maestros (H4; solo para mostrar)
  int xp = 0, nivel = 1;                // xp = combates ganados (manda); nivel = 1 + xp/3 (solo para leerlo)
  // studies 8 H1: progreso del Modo Heroe POR FICHA (antes global en hub_heroe.toml / rpg.toml).
  int saga = 0;                         // saga siguiente (crece sin tope: vueltas)
  int ph = 0, ph_vida = 0, ph_dano = 0; // puntos de habilidad sin gastar y gastados en vida / dano
  int maestros = 0;                     // bit i = maestro i entrenado
  int entreno[6] = {0, 0, 0, 0, 0, 0};  // % de entrenamiento con cada maestro (0..100)
  int progreso = 0;                     // 2 = el progreso ya vive en la ficha (migracion hecha una vez)
  // studies 5 C2 (cac.toml v2): identidad estable de la ficha, inventario de tecnicas del heroe y las 4 equipadas.
  //  inv: IDs de capsula (#SKC) de las tecnicas que el heroe posee (se llena solo con los donantes que tiene abiertos);
  //  equipo: hasta 4 capsulas de `inv` elegidas para el combate (0 = hueco libre).
  std::string id;                       // "h" + 6 hex; se crea al guardar si falta y no cambia nunca
  std::vector<int> inv;
  int equipo[4] = {0, 0, 0, 0};
  // C3: Esferas del Dragon (bit i = la esfera i+1 recogida en el mapa) y deseos de Shenron pendientes. Cambiar de RAZA o de
  // cuerpo una vez empezada la aventura exige un deseo (7 Esferas); la apariencia (pelo, cara, ropa, colores) es libre.
  int esferas = 0;
  int deseos = 0;
  // C3: desbloqueos conseguidos con los maestros (ids de res/heroe/desbloqueos.toml: aura/ki de color, tecnicas con variante).
  std::vector<std::string> desbloqueos;
};
constexpr int kNumTecnicas = 4;
constexpr int kNumEntreno = 6;
constexpr int kProgresoFicha = 2;       // marcador `progreso = 2` de cada [[heroe]]
void RestaurarColoresCac2(Heroe& h);  // solo tintes cosméticos del creador
void ClampHsv(Hsv& c);                  // h 0..360, s y v -100..100
constexpr int kMaxSlots = 3;   // heroes activos (cac.toml v2). Las fichas de mas (de la v1) pasan a [[archivado]]
constexpr int kVersion = 3;    // v3: capas + recetas (v2: copia .v2.bak y migra al leer); v2: 3 heroes + [[archivado]] + id/inv/equipo (v1: copia .v1.bak)

// --- slots -------------------------------------------------------------------
int NumSlots();
const Heroe& Slot(int i);
Heroe& SlotEditable(int i);
int SlotActivo();
void ElegirSlot(int i);
int NuevoSlot();                          // indice del slot nuevo; con kMaxSlots activos no crea y devuelve -1
bool SlotsLlenos();
bool MigradoDeV1();                       // el ultimo Leer() convirtio un cac.toml v1 (copia en <ruta>.v1.bak)
int ArchivadosPorLimite();                // fichas v1 que no cabian en kMaxSlots y quedaron archivadas
void RestaurarArchivado(int i);           // devuelve un archivado a los activos si hay hueco
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
bool RespaldarCopia(const char* motivo);  // copia cac.toml (el ultimo leido/guardado) a <ruta>.<motivo>-<fecha>.bak antes de un cambio grande

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
