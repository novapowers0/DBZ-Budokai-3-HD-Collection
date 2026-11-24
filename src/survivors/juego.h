// Dragon Budokai Survivors: una partida completa (jugador, horda por tipos, oleadas, gemas, niveles, tecnicas automaticas, evoluciones,
// pasivas y jefe). Sin ImGui ni nada del juego: tests/survivors_juego_test.cpp la compila sola. Contrato: studies 5/ENTREGAS/
// SURVIVORS_DISENO_MVP.md. Los datos (y los textos es/en) salen de res/survivors/survivors.toml (CargarDatos).
//
// Uso (pantallas): CargarDatos una vez; Juego* j = new Juego (es grande: ~0,3 MB, no en pila); j->Iniciar(datos, personaje,
// semilla, 0, config) (config: escenario, dificultad y mejoras permanentes de la tienda); cada 1/60 s, si j->estado == Estado::Jugando, j->Paso({palanca x, palanca y}). Con Estado::Eligiendo se muestran
// j->opciones[0..nOpciones) y se llama j->Elegir(i). Pausa = no llamar a Paso. Al acabar, estado = Victoria o Derrota.
// Coordenadas: plano (x, y) en unidades de mundo (una figura mide ~1,8); en el dibujo y = z. Todo determinista con la semilla.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim.h"

namespace dbz3::survivors {

constexpr int kMaxTecnicas = 6, kMaxPasivas = 6, kNivelMax = 5, kOpciones = 3, kTiposEnemigo = 6;
constexpr int kMaxEfectos = 512, kMaxGemas = 1024, kMaxEventos = 256, kMaxDefs = 48;   // kMaxDefs: tecnicas + evoluciones (v2: 24 + 12)
// tanda 4: escenarios, enemigos (todos los [[enemigo]]: cada escenario usa 6), mejoras permanentes de la tienda y rompibles a la vez
constexpr int kMaxEscenarios = 8, kMaxEnemigos = 24, kMejorasPerm = 18, kMaxRompibles = 48;
constexpr int kMaxRompDefs = 64;  // presupuesto de definiciones (distinto de instancias simultáneas)
constexpr int kDificultades = 3;   // normal, dificil, extremo

// ---------------------------------------------------------------- datos (survivors.toml)
enum class Arquetipo : uint8_t {
  Rayo,          // rayo ancho que atraviesa en linea hacia donde mira (Kamehameha)
  Orbita,        // discos que giran alrededor (Kienzan)
  Teledirigida,  // bolas que buscan al enemigo mas cercano
  Estallido,     // bolas en todas direcciones
  Aura,          // dano continuo alrededor
  RayoFino,      // rayo fino y largo que perfora, al mas cercano (Makankosappo)
  Cono,          // golpe en cono hacia donde mira
  Explosion,     // explosion sobre un enemigo al azar
  Bumeran,       // disco que va y vuelve
  Rafaga,        // muchas bolas pequenas y rapidas al mas cercano
  Barrera,       // anillo que empuja y dana
  Onda,          // onda expansiva periodica
};
// Suerte (objetos que caen), Zeni y Reanimar solo los dan las mejoras permanentes de la tienda
// C04 (pedido de P3): Armadura = fraccion de dano recibido que se quita (tope 60 %); Duracion = los efectos de las tecnicas duran mas;
// Cantidad = proyectiles de mas (la parte entera de la suma: 0,5 por nivel = +1 cada 2 niveles); Maldicion = mas horda y mas zeni y exp
// (la misma fraccion); Crecimiento = dano de mas por cada nivel del luchador.
enum class Efecto : uint8_t { VidaMax, Velocidad, Recogida, Recarga, Dano, Area, Exp, Regen, Suerte, Zeni, Reanimar,
                              Armadura, Duracion, Cantidad, Maldicion, Crecimiento,
                              Resorteo, Salto, Desterro };   // SUR-10: re-sorteo mas barato, saltar da mas, mas destierros
constexpr int kNumEfectos = 19;

struct Textos { std::string es, en, desc_es, desc_en; };

struct DefTecnica {
  std::string id, icono;      // icono = id de res/mejoras/iconos/<icono>.png
  Textos txt;
  Arquetipo arq = Arquetipo::Teledirigida;
  uint32_t color = 0x66CCFF;  // 0xRRGGBB
  float dano[kNivelMax] = {}, recarga[kNivelMax] = {}, area[kNivelMax] = {};
  int cantidad[kNivelMax] = {};
  float velocidad = 10, duracion = 1, alcance = 8, empuje = 0, tick = 0.3f;   // tick: un enemigo no recibe dos golpes de ella antes
  int perfora = 0;                                                              // enemigos extra que atraviesa una bola (-1 = todos)
  int evoluciona_de = -1, pasiva = -1;   // solo evoluciones: tecnica base (al nivel 5) y pasiva que pide (indices en Datos)
};
struct DefPasiva {
  std::string id, icono;
  Textos txt;
  Efecto efecto = Efecto::Dano;
  float por_nivel = 0.1f;   // VidaMax: +vida; Regen: vida/s; el resto: fraccion (+10 % por nivel con 0.1)
};
// A06 (Config::ia): conducta propia de un enemigo. Persigue = la de siempre. Los numeros salen del toml (enemigo.cond_*) o de estos valores por tipo.
enum class Conducta : uint8_t {
  Persigue,
  Tirador,     // se para a 'cond_rango' del jugador, avisa 'cond_aviso' s y dispara una bola de ki cada 'cond_cada' s ('cond_dano')
  Embestida,   // se para y avisa con una franja 'cond_aviso' s, y embiste a 'cond_vel' durante 0,6 s
  Explota,     // pegado al jugador se para, avisa con un anillo ('cond_aviso' s, 'cond_radio') y explota: 'cond_dano' al jugador si sigue dentro
  Elite,       // escudo (fraccion 'cond_escudo' de su vida) e invoca 'cond_n' enemigos cada 'cond_cada' s con aviso
  Invoca,      // solo invoca 'cond_n' enemigos cada 'cond_cada' s con aviso (toml: "invoca")
  Absorbe,     // lo que se le pega cura a los enemigos a 'cond_radio' de el (fraccion 'cond_escudo' del golpe) (toml: "absorbe")
  Frontal,     // escudo de frente: los golpes que le llegan desde el lado del jugador pierden 'cond_escudo' (0,85); por detras, entero (toml: "escudo")
};
struct DefEnemigo {
  std::string id;
  int fid = 371;            // modelo de B3 (mod center hd/tactics_manifest.toml)
  float tono = 0;           // giro de tono en grados al hornear (solo si el luchador no tiene el traje pedido)
  int traje = 0;            // traje alternativo de B3 (colores oficiales); 0 = el de serie
  float vida = 10, velocidad = 2.5f, dano = 5, radio = 0.45f, exp = 1, escala = 1;
  float cofre = 0;          // probabilidad de soltar un cofre al caer (el elite); el jefe lo suelta siempre
  int anillo = 0;           // elite: anillo en el suelo y punto en el minimapa (0 no es elite, 1 oro, 2 rojo, 3 azul)
  Conducta conducta = Conducta::Persigue;
  float cond_cada = 4, cond_aviso = 0.8f, cond_dano = 8, cond_vel = 11, cond_rango = 9, cond_radio = 2.6f, cond_escudo = 0.35f;
  int cond_n = 3;
};
// Objetos que se recogen ademas de las gemas (tanda 3 del arte). Orden fijo: el de 'prob' en [recogibles] y el de los nombres.
enum class TipoObjeto : uint8_t { Capsula, Iman, Bomba, Reloj, Zeni, Cofre };
constexpr int kTiposObjeto = 6, kMaxObjetos = 64;
const char* NombreObjeto(TipoObjeto t);   // "capsula", "iman", "bomba", "reloj", "zeni", "cofre"
struct DefRecogibles {   // [recogibles] de survivors.toml
  float gema_verde = 3, gema_roja = 10;   // exp desde la que la gema es verde / roja (solo dibujo)
  float prob[kTiposObjeto - 1] = {0.003f, 0.001f, 0.001f, 0.001f, 0.02f};   // por enemigo derrotado: capsula, iman, bomba, reloj, zeni
  float cura = 0.3f;                      // capsula: fraccion de la vida maxima
  float iman_dura = 4, iman_vel = 30;     // iman: segundos que atrae TODAS las gemas y a que velocidad
  float bomba_dano = 400, bomba_radio = 18;   // bomba: dano a todo lo que hay a esa distancia del jugador (~la pantalla)
  float reloj_dura = 5;                   // reloj: segundos con la horda congelada
  float reloj_jefe = 0.5f;                // reloj: el jefe no se congela, va a esta fraccion de su ritmo (movimiento, avisos y carga)
  int zeni_valor = 10;
  float victoria_tras = 5;                // s entre la caida del jefe y la victoria (para recoger su cofre); 0 = al momento
};
struct DefJefe {
  std::string id;
  Textos txt;
  int fid = 146;
  float vida = 6000, velocidad = 2.2f, dano = 20, radio = 1.0f, escala = 1.8f, exp = 0;
  float cada = 5;                       // segundos entre ataques
  float aviso = 1.2f;                   // segundos que se ve el aviso antes de cada ataque
  float carga_vel = 16, carga_dura = 0.7f, carga_ancho = 1.6f, carga_dano = 30;
  int anillo_balas = 16;
  float bala_vel = 7, bala_dano = 12, bala_radio = 0.35f;
  // tercer ataque (tanda 4): lluvia de proyectiles en una zona rectangular alrededor del jugador, cada uno avisado por su sombra
  int lluvia_n = 0;                     // proyectiles (0 = sin este ataque)
  float lluvia_ancho = 10, lluvia_alto = 7, lluvia_radio = 1.1f, lluvia_dano = 16, lluvia_aviso = 1.6f;
  // SUR-8: fases por vida (solo jefes con kit): modelo (fid del manifiesto) y forma de cada fase; vacio = las de siempre (2 fases fijas en modo_mundo.cpp)
  std::vector<int> fases_fid, fases_forma;
};
struct DefPersonaje {
  std::string id;
  Textos txt;
  int fid = 264;
  int tecnica = 0;          // indice de su tecnica inicial
  float vida = 100, velocidad = 5, recogida = 2.5f;
  int pasiva = -1;          // C01: pasiva propia del luchador = un Efecto (toml: pasiva = "dano"...) con 'pasiva_valor'; -1 = ninguna
  float pasiva_valor = 0;
};
struct Oleada {
  float minuto = 0;                     // desde que minuto (sobre 15) manda esta fila
  float ritmo = 1;                      // apariciones por segundo
  int tope = 100;                       // enemigos a la vez como mucho (ademas del tope global)
  float peso[kTiposEnemigo] = {1};      // mezcla de tipos
  float vida = 1;                       // multiplicador de vida
};
// ---- tanda 4
// Objeto que se rompe de un golpe de tecnica: pasa a su estado roto en el mismo sitio y suelta lo de su tabla.
struct DefRompible {
  std::string id;                       // arte: tanda4/02_rompibles/rompible_<id>_{entero,roto}.png
  float vida = 5;
  float exp = 0;                        // gema que suelta (0 = ninguna)
  float prob[kTiposObjeto] = {};        // capsula, iman, bomba, reloj, zeni, cofre: suelta como mucho uno (suma <= 1)
};
// Decorado de un escenario (solo dibujo: modo.cpp). Rutas relativas a res/survivors. Sistema de la llanura (tanda 3) generalizado:
// zonas de suelo con 4 variantes (la base de 03_fondos y las 3 de 'suelos'), manchas de transicion y adornos por celda.
struct DefDecorado {
  std::vector<std::string> suelos, manchas, adornos;
  std::vector<float> reparto, manchas_alfa, manchas_tam;   // por mancha: peso, opacidad y tamano relativos
  std::vector<float> anclas;                               // ancla_y de cada adorno (lienzo 256x256, ancla_x 128)
  std::vector<int> planos;                                 // adornos planos (crateres, grietas, losas): con el suelo, debajo de todo
  float densidad_adornos = 0.23f, densidad_manchas = 0.16f;
  uint32_t color = 0x3A523C;                               // relleno bajo el suelo (0xRRGGBB)
};
struct DefEscenario {
  std::string id;                       // llanura, desierto, ruinas, glaciar (= carpeta de 03_fondos)
  Textos txt;
  int jefe = 0;                         // indice en Datos::jefes
  int enemigos[kTiposEnemigo] = {0, 1, 2, 3, 4, 5};   // indice en Datos::enemigos de cada hueco (los pesos de las oleadas van por hueco)
  std::vector<int> rompibles;           // indices en Datos::rompibles (vacio = sin rompibles)
  std::vector<std::string> ambiente;    // capas de tanda4/03_ambiente (lluvia, hojas, arena, niebla, chispas, nieve)
  std::vector<float> ambiente_alfa;
  float vida = 1, ritmo = 1;            // multiplicadores de la horda en este escenario
  float rayo_cada = 0;                  // s de media entre rayos (0 = sin rayos); avisados con la marca en el suelo
  float rayo_aviso = 1.4f, rayo_radio = 2.2f, rayo_dano = 22, rayo_dano_enemigos = 60;
  DefDecorado deco;
};
struct DefDificultad { float vida = 1, dano = 1, zeni = 1; };   // horda y jefe: vida y dano; zeni recogido
struct DefMejoraPerm {   // tienda de zeni entre partidas
  std::string id, icono;                // icono: ruta en res/survivors
  Textos txt;
  Efecto efecto = Efecto::VidaMax;
  float por_nivel = 0.05f;
  int precio[kNivelMax] = {100, 200, 400, 800, 1600};   // del nivel 1 al 5
};
// A08 (Config::jefes_v2; [ritmo] y [[evento]] de survivors.toml): minijefes, jefe a las 12:00, furia a las 15:00 y eventos con aviso de 5 s
struct DefEvento { float minuto = 0; int tipo = 0; int n = 40; };   // tipo: 0 enjambre, 1 cerco en anillo, 2 lluvia de meteoros; n = enemigos / marcas
constexpr int kMaxEventos2 = 16;
struct DefRitmo {
  float minijefe[2] = {5, 10};            // minutos (sobre 15) de los minijefes (un elite fuerte con cofre seguro)
  float jefe = 12, furia = 15, aviso = 5;   // minuto del jefe, minuto en que su furia empieza si sigue vivo, y s de aviso de cada evento
  float mini_vida = 14, mini_escala = 1.35f;   // un minijefe: vida x14 sobre la del elite y algo mas grande
  float furia_vel = 1.4f, furia_dano = 1.25f;   // furia del jefe: todo va x1,4 mas deprisa y pega x1,25
  std::vector<DefEvento> eventos;         // vacio = los de serie (Juego::EventosDeSerie)
};
// A05 (Config::formas; [formas] de survivors.toml): barra de ki y transformacion del luchador
struct DefFormas {
  float dura = 20;          // s que dura la transformacion (la barra se vacia a ese ritmo)
  float kills = 300;        // derrotados que llenan la barra solos
  float tiempo = 100;       // s que la llenan solos (los dos van sumando)
  float dano = 0.25f, velocidad = 0.10f;   // bonificacion mientras esta transformado (fraccion)
  int max = 2;              // formas por encima de la base (la pantalla recorta a las que tenga el luchador)
};
struct DefRacha { float dura = 3, exp = 0.002f, tope = 0.2f; };   // s sin derrotar que la apagan; exp extra por derrotado y su tope

// ---- A09 (ajuste dbz3_survivors_meta_v2, Config::meta_v2): rareza y decisiones en la subida de nivel; [subida] de survivors.toml
enum class Rareza : uint8_t { Comun, PocoComun, Rara, Epica, Legendaria };
constexpr int kRarezas = 5, kMaxPremios = 5;
const Textos& NombreRareza(Rareza r);   // "Comun/Poco comun/Rara/Epica/Legendaria" (es/en)
uint32_t ColorRareza(Rareza r);         // 0xRRGGBB: gris, verde, azul, morado, dorado
struct DefSubida {
  float peso[kRarezas] = {60, 25, 10, 4, 1};   // probabilidad relativa de cada rareza (de la 2a en adelante, x(1 + suerte * rareza * suerte_k))
  int niveles[kRarezas] = {0, 0, 1, 1, 2};     // niveles que regala sobre el +1 normal (tope 5)
  float suerte_k = 2;
  int reroll_base = 25, reroll_paso = 25;       // coste en zeni del re-sorteo: base + paso * veces ya usado
  int saltar_zeni = 15;                         // saltar la subida: da estos zeni...
  float saltar_vida = 0.1f;                     // ...y esta fraccion de la vida maxima
  int desterros = 3;                            // opciones que se pueden desterrar en la partida
  float cofre_peso[3] = {70, 25, 5};            // tier 1 / 3 / 5 premios (el del jefe es siempre de 5)
};

struct Datos {
  float duracion = 900;                 // segundos de partida (toda la linea de tiempo se escala con esto)
  float minuto_jefe = 14;               // sobre 15
  int tope_pantalla = 600;
  float exp_base = 10, exp_paso = 5, exp_curva = 1.75f;   // base + paso * (L - 1) + curva * max(0, L - 6)^2
  float invulnerable = 0.5f;
  std::vector<DefPersonaje> personajes;
  std::vector<DefTecnica> tecnicas;     // primero las 12 base y despues las evoluciones (evoluciona_de >= 0)
  std::vector<DefPasiva> pasivas;
  std::vector<DefEnemigo> enemigos;     // todos (6..kMaxEnemigos); cada escenario elige 6
  std::vector<DefJefe> jefes;           // uno por escenario
  DefRecogibles recogibles;
  std::vector<Oleada> oleadas;          // ordenadas por minuto
  // tanda 4
  std::vector<DefEscenario> escenarios; // sin [[escenario]]: una llanura con los 6 primeros enemigos y el primer jefe
  std::vector<DefRompible> rompibles;
  float rompible_celda = 9, rompible_densidad = 0.2f;   // uno por celda de ese lado con esa probabilidad
  DefDificultad dificultad[kDificultades] = {{}, {}, {2.0f, 1.6f, 2.0f}};   // normal, dificil, extremo (los dos primeros: lo que diga el toml)
  std::vector<DefMejoraPerm> mejoras;   // kMejorasPerm como mucho
  DefRacha racha;
  DefSubida subida;                     // A09
  DefFormas formas;                     // A05
  DefRitmo ritmo;                       // A08
};
// Lee el toml. false si no se puede usar (falta, mal formado, listas vacias o referencias rotas); los campos que faltan toman su
// valor por defecto. 'error' recibe una linea por problema (tambien los avisos cuando devuelve true).
bool CargarDatos(const std::string& ruta, Datos* out, std::string* error);

// ---------------------------------------------------------------- estado de la partida (solo lectura para quien dibuja)
enum class Estado : uint8_t { Jugando, Eligiendo, Victoria, Derrota };
enum class Forma : uint8_t {
  Bola,         // circulo con brillo en (x, y) de radio 'radio'
  Disco,        // disco plano (Kienzan) en (x, y), radio; 'ang' = giro para dibujarlo rodando
  Rayo,         // de (x, y) a (x2, y2) con 'ancho'
  Anillo,       // circunferencia en (x, y) de radio 'radio' y grosor 'ancho'
  Cono,         // desde (x, y), radio 'radio', direccion 'ang' (rad) y media apertura 'apertura'
  Aura,         // circulo relleno suave en (x, y) de radio 'radio'
  Explosion,    // estallido en (x, y) que crece hasta 'radio' y se apaga con Vida01
  AvisoLinea,   // aviso del jefe: franja de (x, y) a (x2, y2) con 'ancho'; Vida01 baja hasta el ataque
  AvisoAnillo,  // aviso del jefe: anillo de radio 'radio' alrededor de (x, y)
  // tanda 4 (todos hostiles y sin golpe propio: el dano lo hace la logica al acabar su vida)
  AvisoZona,    // zona rectangular de la lluvia del jefe: centro (x, y), medias medidas (x2, y2)
  Sombra,       // sombra de un proyectil que cae en (x, y) con 'radio': crece hasta el impacto (Vida01 llega a 0)
  AvisoRayo,    // marca del rayo en (x, y) con 'radio': cae al acabar
  RayoCae,      // el rayo cayendo (solo dibujo)
};
struct Efecto2D {   // proyectil o efecto visible (los de logica van aparte)
  uint64_t id_visual = 0;    // identidad estable aunque se compacte el array
  Forma forma = Forma::Bola;
  bool hostil = false;      // del jefe (dana al jugador)
  uint8_t def = 0;          // tecnica (indice en Datos::tecnicas); 255 = jefe
  uint32_t color = 0xFFFFFF;
  float x = 0, y = 0, x2 = 0, y2 = 0, radio = 0, ancho = 0, ang = 0, apertura = 0;
  float vida = 0, vida0 = 1;   // segundos restantes / iniciales
  float Vida01() const { return vida0 > 0 ? vida / vida0 : 0; }
  // logica
  float vx = 0, vy = 0, dano = 0, empuje = 0, t = 0;
  int perfora = 0, objetivo = -1;
  bool golpea = true;          // false: solo dibujo (el dano lo hizo la tecnica al crearlo)
};
struct Jugador {
  float x = 0, y = 0, dx = 0, dy = 1;   // posicion y direccion en la que mira (unitaria)
  bool moviendo = false;
  float vida = 100, vida_max = 100, invulnerable = 0, destello = 0;   // destello: 0..1 al recibir dano
  int nivel = 1;
  float exp = 0, exp_sig = 5;
  float velocidad = 5, recogida = 2.5f;
};
enum class EstadoJefe : uint8_t { Ausente, Persigue, AvisoCarga, Carga, AvisoAnillo, Muerto, AvisoLluvia,
                                  AvisoKit, Kit, Transforma };   // A07 (solo jefes_v2): avisando / disparando un ataque de su kit, cambiando de fase
// A07: los ataques del kit REAL de cada jefe (las capsulas de B3: Frieza Death Beam/Death Wave/Death Ball, Cell Kamehameha/Energy Field/Spirit Bomb,
// Super Buu Ill Flash/Ill Ball Attack, Cooler Destructive Ray/Sauzer Blade/Supernova; la Carga es su combo cuerpo a cuerpo)
enum class AtaqueJefe : uint8_t { Ninguno, Carga, Kamehameha, CampoEnergia, BombaEspiritual, RayoMortal, OndaMortal, BolaMuerte, IllFlash, IllBola,
                                  RayoDestructor, DiscoSauzer, Supernova,
                                  Taiyoken, InvocaCellJr, Rocas, Absorcion };   // SUR-04: los que no son capsulas de B3 sino movimientos de combo del jefe
const char* NombreAtaque(AtaqueJefe a);
int CartaJefe(AtaqueJefe a);   // fila #SKC; -1 para acciones propias del modo
int IndiceFormaJefe(const DefJefe& d, int fase, bool dos_fases);   // "kamehameha", "death_beam"... (registro y pruebas)
struct Jefe {
  EstadoJefe estado = EstadoJefe::Ausente;
  float x = 0, y = 0, dx = 0, dy = 1, vida = 0, vida_max = 1, destello = 0;
  float t = 0;   // segundos en el estado actual
  // A07 (jefes_v2): fase 1..2 (a media vida cambia de forma), ataque en curso, ataques lanzados y escudo (Energy Field: recibe una fraccion del dano)
  int fase = 1, n_ataques = 0;
  AtaqueJefe ataque = AtaqueJefe::Ninguno;
  float escudo = 0;
};
// fase 0..1 del ciclo de andar; destello 0..1 al recibir golpe; tipo = indice en Datos::enemigos; anillo = el de su def (elite)
struct VistaEnemigo {
  float x, y, fase, destello, escala;
  int tipo, anillo;
  float escudo = 0;   // A06: 0..1 del escudo que le queda (elite)
  float aviso = 0;    // A06: 1 mientras avisa de su ataque (la pantalla lo hace parpadear)
  bool mini = false;  // A08: es un minijefe
};
struct Poseida { int def = -1, nivel = 0; };                             // tecnica (o pasiva) con su nivel 1..5
enum class TipoOpcion : uint8_t { Tecnica, Evolucion, Pasiva, Curar };   // Curar: cuando no queda nada que mejorar
struct Opcion {
  TipoOpcion tipo = TipoOpcion::Curar;
  int def = -1;      // indice en Datos::tecnicas (Tecnica, Evolucion) o Datos::pasivas
  int nivel = 1;     // nivel que tendra al elegirla (1 = nueva); con rareza ya incluye los niveles de regalo
  Rareza rareza = Rareza::Comun;   // A09: siempre Comun con meta_v2 apagado; las evoluciones son Legendaria
};
enum class TipoEvento : uint8_t {
  Golpe,         // a = tecnica que golpea (-1 = bomba), v = dano (x, y = enemigo)
  Muerte,        // a = tipo de enemigo
  Recogida,      // gema recogida (a = valor)
  Subida,        // a = nivel nuevo
  DanoJugador,   // a = dano redondeado
  AvisoJefe,     // a = 0 carga, 1 anillo
  JefeAparece,
  JefeMuere,
  Evolucion,     // a = tecnica evolucionada
  Tecnica,       // tecnica lanzada: a = tecnica; x, y = DIRECCION unitaria hacia donde sale (no posicion; el jugador esta en jugador.x/y)
  Objeto,        // objeto recogido: a = TipoObjeto (x, y = donde estaba); el cofre abre ademas la eleccion de mejora
  // tanda 4
  Rompible,      // objeto roto: a = indice en Datos::rompibles (x, y)
  RayoCae,       // el rayo cae en (x, y)
  Racha,         // la racha llega a a (10, 25, 50, 100, 150...)
  Reanimado,     // vida extra gastada (mejora permanente Reanimar)
  // A09
  CofreAbre,     // cofre recogido con meta_v2: a = premios (1/3/5); x, y = el jugador. Los premios ya estan aplicados (Juego::premios)
  // A08 (solo con Config::jefes_v2)
  AvisoEvento,   // dentro de v segundos: a = 0 enjambre, 1 cerco, 2 meteoros, 3 minijefe
  EventoOleada,  // ocurre ahora: a = igual; x, y = donde (minijefe) o el jugador
  JefeFuria,     // el jefe sigue vivo al llegar el minuto de la furia
  // A05 (solo con Config::formas)
  Forma,         // el luchador cambia de forma: a = forma nueva (0 = vuelve a la base, 1.. = transformado), v = segundos que durara
  // A02 (solo con Config::jugo)
  Pausa,         // pausa de golpe: a = milisegundos que la simulacion se queda quieta (Paso no hace nada hasta que acaba); x, y = donde
  Temblor,       // temblor de camara: a = origen (1 bomba, 2 elite, 3 jefe), v = intensidad 0..1
  // A07 (solo con Config::jefes_v2)
  JefeFase,      // el jefe cambia de forma: a = fase nueva (2); x, y = donde. Dura Juego::kTransforma s (no recibe dano)
  JefeAtaque,    // el jefe empieza un ataque de su kit: a = AtaqueJefe, v = segundos de aviso; x, y = el jefe
};
struct Evento { TipoEvento tipo; int a; float x, y, v = 0; };
struct Objeto { float x, y; TipoObjeto tipo; uint8_t tier = 0; };   // tier (solo cofres con meta_v2): premios que dara, 1/3/5
struct Entrada { float x = 0, y = 0; };   // palanca o cruceta, -1..1 (y positiva = arriba en pantalla = +y del plano)
// Rompible en el mundo: sale de forma determinista por celda alrededor del jugador (la celda decide si hay, cual y donde)
struct Rompible {
  float x = 0, y = 0, vida = 0;
  float roto = -1;          // s desde que se rompio (< 0 = entero)
  int tipo = 0;             // indice en Datos::rompibles
  int ci = 0, cj = 0;       // su celda
  uint32_t ultimo = 0;      // paso del ultimo golpe (un golpe cada 0,25 s)
};
// Lo que se elige fuera de la partida: entra en Iniciar como parametro (el determinismo depende solo de esto y de la semilla)
struct Config {
  int escenario = 0, dificultad = 0;     // dificultad 0 normal, 1 dificil, 2 extremo
  int mejoras[kMejorasPerm] = {};        // nivel 0..5 de cada mejora permanente (orden de Datos::mejoras)
  bool meta_v2 = false;                  // A09: rareza, re-sorteo, saltar, desterrar y cofre por tiers (apagado = la subida de siempre)
  // P1 (mundo_p1.h: AjustesP1): lo que cambia la simulacion. Todo apagado = la partida de siempre, bit a bit con la misma semilla.
  bool jugo = false;                     // A02: pausa de golpe y retroceso de los golpeados
  bool formas = false;                   // A05: barra de ki y forma del luchador
  bool ia = false;                       // A06: conductas de enemigos
  bool jefes_v2 = false;                 // A07/A08: kits de jefe, 2 fases, minijefes y eventos
  bool jefes_dos_fases = false;           // usa las formas inicial y final, sin fase intermedia
  bool autoapuntar = false;              // SUR-8: los ataques del jugador apuntan solos (jefe a la vista, si no el mas cercano); AjustesP1 lo enciende de serie
};

class Juego {
 public:
  bool Iniciar(const Datos& d, int personaje, uint64_t semilla, float duracion = 0, const Config& c = {});   // duracion 0 = la de los datos
  void Paso(const Entrada& e);   // 1/60 s; no hace nada fuera de Estado::Jugando
  void Elegir(int i);            // con Estado::Eligiendo: aplica opciones[i]
  uint64_t Hash() const;

  // ---- lectura
  const Datos* datos = nullptr;
  Estado estado = Estado::Jugando;
  int personaje = 0;
  float tiempo = 0, duracion = 900;   // segundos jugados / de partida
  float Minuto() const { return tiempo / duracion * 15.0f; }   // minuto de la linea de tiempo (0..15)
  Jugador jugador;
  Jefe jefe;
  int NumEnemigos() const { return horda_.n; }
  VistaEnemigo Enemigo(int i) const;
  Efecto2D efectos[kMaxEfectos];
  int nEfectos = 0;
  float gx[kMaxGemas], gy[kMaxGemas], gvalor[kMaxGemas];
  int nGemas = 0;
  Objeto objetos[kMaxObjetos];
  int nObjetos = 0;
  int zeni = 0;                        // monedas recogidas (resultados; ya con la dificultad y la mejora Zeni extra)
  float iman = 0, congelado = 0;       // segundos que quedan de iman / reloj
  float cegado = 0;                    // SUR-04: segundos que quedan del Taiyoken (el jugador va a medio paso)
  // tanda 4
  Config config;
  const DefEscenario& Escenario() const { return datos->escenarios[size_t(config.escenario)]; }
  const DefJefe& DJ() const { return datos->jefes[size_t(Escenario().jefe)]; }
  int NFases() const;                 // SUR-8: 1 (sin kit), 2 o 3 fases por vida del jefe
  float UmbralFase_(int fase) const;  // fraccion de vida a la que acaba la fase 'fase'
  Rompible rompibles[kMaxRompibles];
  int nRompibles = 0, rotos = 0;
  int racha = 0, racha_max = 0;        // derrotados seguidos (se apaga tras racha.dura s sin derrotar)
  float racha_t = 0;                   // s desde el ultimo derrotado
  int vidas_extra = 0, jefes_derrotados = 0;
  float Mejora(Efecto e) const { return perm_[int(e)]; }   // bonificacion de las mejoras permanentes
  float Recarga01(int slot) const;     // 1 = recien lanzada .. 0 = lista (velo de recarga de la casilla)
  Poseida tecnicas[kMaxTecnicas], pasivas[kMaxPasivas];
  int nTecnicas = 0, nPasivas = 0;
  Opcion opciones[kOpciones];
  int nOpciones = 0;
  Evento eventos[kMaxEventos];   // los del ultimo Paso (o de Elegir); los que no caben se pierden
  int nEventos = 0;
  // contadores
  int derrotados = 0;
  double dano_tecnica[kMaxDefs] = {};   // por indice de tecnica
  int usos_tecnica[kMaxDefs] = {};      // veces que se ha lanzado
  uint64_t pasos = 0;
  uint64_t siguiente_fx_visual = 0;
  const char* Icono(const Opcion& o) const;
  const Textos& Texto(const Opcion& o) const;
  int NivelPasiva(int def) const;
  int NivelTecnica(int def) const;

  // pruebas: da (o sube) una tecnica / pasiva sin pasar por el sorteo
  void Dar(TipoOpcion tipo, int def, int nivel);

  // ---- A09: decisiones en la subida (todas devuelven false y no hacen nada si !config.meta_v2, si no hay una eleccion en curso o
  // si estas en un cofre). Estado::Eligiendo sigue siendo el estado de la subida Y del cofre: el cofre se distingue con CofreAbierto().
  int CosteResortear() const { return int(float(datos->subida.reroll_base + datos->subida.reroll_paso * resorteos) * (1.0f - (Mult(Efecto::Resorteo) < 0.75f ? Mult(Efecto::Resorteo) : 0.75f)) + 0.5f); }   // SUR-10: la mejora Resorteo lo abarata
  bool PuedeResortear() const { return PuedeDecidir_() && zeni >= CosteResortear(); }
  bool Resortear();                // paga CosteResortear() zeni y saca 3 opciones nuevas (las evoluciones listas siguen saliendo)
  bool Saltar();                   // renuncia a la subida: saltar_zeni zeni + saltar_vida de vida; sigue la siguiente subida pendiente
  bool Desterrar(int i);           // opciones[i] (tecnica o pasiva) no vuelve a salir en la partida; gasta 1 destierro y re-sortea gratis
  int DesterrosRestantes() const { return desterros_; }
  bool EsDesterrada(const Opcion& o) const;
  // Cofre con meta_v2: al recogerlo se sortean y APLICAN 'tier' premios (1/3/5; evoluciones listas primero) y la partida queda en
  // Estado::Eligiendo con CofreAbierto() == true hasta que la pantalla llama a CerrarCofre() (o a Elegir(cualquiera)). Mientras tanto
  // 'premios[0..nPremios)' son lo ganado (con su rareza y el nivel resultante) y opciones[] repite los 3 primeros para pantallas antiguas.
  // OpcionesCofre(tier) hace el sorteo y la aplicacion (publica para pruebas); devuelve nPremios.
  int OpcionesCofre(int tier);
  bool CofreAbierto() const { return cofre_; }
  void CerrarCofre();
  Opcion premios[kMaxPremios];
  int nivel_previo_premio[kMaxPremios] = {};
  int nPremios = 0, tierCofre = 0;
  int resorteos = 0;               // veces que se ha re-sorteado en la partida
  // ---- A08: ritmo. Con jefes_v2 el jefe sale en ritmo.jefe, hay minijefes y eventos avisados, y si el jefe sigue vivo a los 15 min la partida
  // sigue (furia) hasta que cae o caes. 'furia' lo lee la pantalla (rotulo, tinte).
  bool furia = false;
  // pruebas (DBZ3_TEST_SURVIVORS_SOLO_JEFE): quita la horda y no deja que salga mas (ni oleadas ni eventos): el jefe solo, para verlo bien
  void SoloJefe() { solo_jefe_ = true, horda_.PonerN(0); }
  AtaqueJefe ataque_prueba = AtaqueJefe::Ninguno;   // SUR-04 (pruebas): el jefe usa siempre este ataque (DBZ3_TEST_SURVIVORS_ATAQUE)
  static constexpr float kTransforma = 1.3f;   // A07: s que dura el cambio de fase del jefe (no recibe dano)
  // Para quien no pudo pasar los ajustes de P1 en Iniciar (modo_mundo.cpp, al primer cuadro de la partida): los enciende ya empezada.
  // No hace nada si ya estaban. Lo correcto es AjustesP1(&cfg) antes de Iniciar (mundo_p1.h): asi la partida es reproducible desde el paso 0.
  void AjustesTardios(bool jugo, bool formas, bool ia, bool jefes_v2);
  static const char* NombreEvento(int tipo);   // "enjambre", "cerco", "meteoros", "minijefe"
  int eventos_vistos = 0, minijefes_vistos = 0;
  // ---- A05 (Config::formas): barra de ki 0..1 (se llena con derrotados y tiempo; llena = transformacion de DefFormas::dura s) y forma actual.
  // Mientras esta transformado 'ki' baja de 1 a 0 y 'forma_t' son los s que quedan. Una evolucion de tecnica transforma al instante (o sube
  // a la forma 2 si ya lo estaba). La pantalla elige el modelo con la forma (modo_mundo.cpp: FormaDe); aqui solo hay numeros y bonos.
  int forma = 0;
  float ki = 0, forma_t = 0;
  void DarKi(float f);               // pruebas y pantallas: suma f (0..1) a la barra
  void ForzarForma(int f) { Transformar_(f); }   // pruebas (DBZ3_TEST_SURVIVORS_FORMA2)
  // ---- A02 (Config::jugo): pausa de golpe 3 cuadros en golpe fuerte (>= kGolpeFuerte), muerte de elite y evolucion; 6 en la caida del jefe
  static constexpr float kGolpeFuerte = 30.0f;
  bool EnPausa() const { return pausa_ > 0; }
  int pausas = 0, temblores = 0;   // cuantas ha habido (pruebas y registro)
  float pausa_total = 0;           // s de partida que se han ido en pausas
  // rareza (para dibujar: color, cinta): Opcion::rareza; Poseida no la guarda (el nivel ya la incluye)

 private:
  uint32_t Azar();
  float Azar01() { return float(Azar() >> 8) * (1.0f / 16777216.0f); }
  void Evento_(TipoEvento t, int a, float x, float y, float v = 0);
  void EventoFijo_(TipoEvento t, int a, float x, float y, float v = 0);   // el que no se puede perder: con la lista llena pisa el ultimo
  void Soltar(float x, float y, float cofre);    // al caer un enemigo: quiza un objeto (o el cofre, con su probabilidad)
  void Poner(float x, float y, TipoObjeto t, int tier = 0);
  void Recoger(const Objeto& o);
  Efecto2D* NuevoEfecto();
  float Mult(Efecto e) const;
  void Dano(int i, float d, int def, float empuje, float ox, float oy);
  void DanoJefe(float d, int def);
  bool JefeEn(float x, float y, float r) const;
  int Cercano(float x, float y, float rmax) const;
  template <class F> void EnCirculo(float x, float y, float r, F&& f);
  template <class F> void EnSegmento(float x0, float y0, float x1, float y1, float ancho, F&& f);
  void Disparar(int slot);
  void MoverEfectos();
  void PasoJefe();
  int KitJefe_() const;                               // A07: 0 = sin kit (ataques de siempre), 1 Cell, 2 Frieza, 3 Super Buu, 4 Cooler
  AtaqueJefe ElegirAtaque_(int kit);
  void IniciarKit_(AtaqueJefe a, float dano);         // aviso del ataque: marcas en el suelo, escudo, sombras que caen
  void DispararKit_(AtaqueJefe a, float dano);        // fin del aviso: rayos, bolas, disco, onda del escudo
  void RayosKit_(AtaqueJefe a, float* ang, int* n) const;   // direcciones (rad) de los rayos del ataque en la fase actual
  void CambiarFase_();
  void Sortear();
  void Aplicar(const Opcion& o);
  void Recalcular();
  void DanoJugador(float d);                          // con invulnerabilidad y la dificultad
  void PasoRompibles();                               // aparecen y desaparecen por celda alrededor del jugador
  void GolpesRompibles();                             // los efectos del jugador los rompen
  void Romper(int i);
  void RomperEn(float x, float y, float r);           // bomba y rayo
  void PasoRayo();
  void Impacto(const Efecto2D& e);                    // fin de un aviso hostil con dano (rayo, proyectil de la lluvia)
  void Jugo_(int cuadros, int temblor, float intensidad, float x, float y, bool especial = true);
  void Transformar_(int f);                           // A05
  // A06 / A08
  int Aparecer(int hueco, const float* pos = nullptr, bool mini = false, int tipo_forzado = -1);   // devuelve su indice (o -1); pos = x, y
  void PasoConductas_();
  void DispararEnemigo_(int i, const DefEnemigo& d);
  void PasoRitmo_();
  void ProgramarRitmo_();
  void Lanzar_(int tipo, int n, int k);
  float MinutoJefe_() const { return config.jefes_v2 ? datos->ritmo.jefe : datos->minuto_jefe; }
  bool JefeVivo_() const { return jefe.estado != EstadoJefe::Ausente && jefe.estado != EstadoJefe::Muerto; }
  int HuecoOleada_();
  struct Prog { float t = 0; int tipo = 0, n = 0; bool avisado = false, hecho = false; };
  Prog prog_[kMaxEventos2 + 2];
  int nProg_ = 0;
  uint8_t mini_[Sim::kMax], cond_est_[Sim::kMax];
  float cond_t_[Sim::kMax], escudo_[Sim::kMax];
  void PasoForma_();
  int derrotados_ki_ = 0;   // A02: pausa de golpe y/o temblor, con topes
  // A09
  bool PuedeDecidir_() const { return config.meta_v2 && estado == Estado::Eligiendo && !cofre_; }
  int Candidatas_(Opcion* c, int cap, Opcion* evo, int* nevo) const;   // mejoras posibles ahora (sin las desterradas); evo: evoluciones listas
  Rareza TirarRareza_();
  void Sazonar_(Opcion* o);                           // le tira la rareza y sube su nivel (solo tecnica y pasiva)
  void AbrirCofre_(int tier);
  void SiguienteEleccion_();                          // tras elegir/saltar: la siguiente subida pendiente o seguir jugando

  Sim horda_;
  uint8_t tipo_[Sim::kMax];
  float vida_[Sim::kMax], destello_[Sim::kMax];
  uint32_t ultimo_[Sim::kMax][kMaxDefs];   // paso del ultimo golpe de cada tecnica (tick)
  uint32_t ultimo_jefe_[kMaxDefs] = {};
  float recarga_[kMaxTecnicas] = {};
  float aparecer_ = 0, regen_ = 0, victoria_ = 0;   // victoria_: cuenta atras tras la caida del jefe
  int subidas_ = 0;
  float ultima_subida_ = -3;   // separa las elecciones de XP sin perder la experiencia sobrante
  uint64_t rng_ = 1;
  float escala_ = 1;   // 900 / duracion
  // tanda 4
  float perm_[kNumEfectos] = {};
  float mdano_ = 1, mvida_ = 1, mzeni_ = 1, zeni_resto_ = 0;   // dificultad (y escenario); zeni_resto_: fraccion de moneda pendiente
  float rayo_t_ = 0;
  uint32_t sal_ = 0;                  // de la semilla: donde caen los rompibles
  int rotos_[256][2] = {};            // celdas con su rompible ya roto (anillo: no vuelven a salir)
  int nRotos_ = 0;
  // A09
  float pausa_ = 0, pausa_cd_ = 0, temblor_cd_ = 0;   // A02: s de pausa que quedan y enfriamientos (los topes)
  bool cofre_ = false, solo_jefe_ = false;
  int desterros_ = 0;
  uint64_t desterr_tec_ = 0, desterr_pas_ = 0;   // mascaras por indice (hasta 64 tecnicas / pasivas)
};

}  // namespace dbz3::survivors
