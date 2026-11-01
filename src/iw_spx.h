// IW #SPX, C++17, sin SDK ni estado global. Consumidor host pendiente de conectar.
// Oraculo: iw_sles55347.c:145390..1491F0 (lineas 51426..53844).
// B2: pasarela_b2.cpp::spx::Maquina, operandos/operaciones; NO sus nativos.
// ABI comprobada tambien en las instrucciones EU1490CC..1491C4:
// a2 (dato) se suma a instancia+10 -> prioridad task+18. NO inicializa r0/r1.
// 140200/140300 recorren una lista ordenada: hijos insertados despues del task
// actual pueden correr en el MISMO tick; ranuras y nodos muertos son distintos.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dbz3::iw::spx {

inline constexpr uint32_t kCombateEU = 0x4582C0;
inline constexpr uint32_t kCapsulasEU = 0x484930;
inline constexpr uint32_t kMascaraDestino = 0x0FFFFFFF;
inline constexpr size_t kRanuras = 16;
inline constexpr size_t kMaxRanuras = 32;  // iw_sles55347.c:330693, READ_GameFolders.

inline float Float(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
inline uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
inline int32_t Signo(uint32_t b) { int32_t i; std::memcpy(&i, &b, 4); return i; }

class Instancia;
struct Hilo;
using Funcion = std::function<uint32_t(Instancia&, Hilo&)>;
using Aviso = std::function<void(Instancia&, Hilo&)>;

struct AnimEspera {
  int slot = 0, vueltas = 0;
  float tiempo = 0, limite = 0;
};

struct Hilo {
  // Exactamente los 0x80 B de registros, accesibles tambien por memoria LE.
  std::array<uint32_t, 16> r{}, f{};
  uint32_t direccion = 0, prioridad = 0, dato = 0;
  uint64_t generacion = 0;
  int ranura = -1;
  int16_t espera = 0;  // task+38, signed short (no contador ilimitado).
  bool activo = false;
  Aviso previo, posterior;  // task+3C / +44, parametro = task+30.
  Funcion sondeo;          // task+40 = parametro+10.
  AnimEspera anim;
};

struct Ganchos {
  std::function<void(Instancia&, uint32_t)> inicio;           // instancia+48
  std::function<void(Instancia&)> fin;                       // instancia+4C
  std::function<void(Instancia&, Hilo&, const Hilo*)> crear;  // instancia+50
  Aviso terminar;                                           // instancia+54
};

struct Traza {
  enum class Fase { Crear, Previo, Sondeo, Opcode, Nativo, Posterior, Fin, Error };
  Fase fase = Fase::Opcode;
  uint64_t cuadro = 0, generacion = 0;
  int ranura = -1;
  uint32_t tabla = 0, pc = 0, codigo = 0, retorno = 0;
  std::array<uint32_t, 16> r{};
};

struct Configuracion {
  size_t ranuras = kRanuras;  // CapsulePick pide32; conservar16 por defecto.
  // Direcciones virtuales host, NO direcciones RAM del juego B3.
  uint32_t guion = 0x100000, compartida = 0x200000, locales = 0x210000;
  uint32_t temporal = 0x220000, pilas = 0x230000, registros = 0x250000;
  uint32_t bytes_compartida = 0x1000, bytes_locales = 0x1000;
  uint32_t bytes_temporal = 0x400, bytes_pila = 0x1000;
  uint32_t prioridad = 0x3000;
  size_t limite_pasos = 100000;  // diagnostico bloqueante, no fin simulado.
};

// Las operaciones del host deben actuar sobre SUS bancos/objetos reales.
// Ninguna ausencia se interpreta como exito. Paso/pausa se consultan al sondear.
struct Host {
  std::function<uint16_t()> botones;  // bits del original PS2, no enum de B3.
  std::function<void(int)> transicion;
  std::function<void()> capsulas_listas;
  std::function<void(uint32_t)> parar_secuencia_iop; // nombre antiguo: EU13FA20 RPC13 INICIA (AMSOUND3690), NO efecto.
  std::function<float()> paso;
  std::function<bool()> pausado;
  std::function<void(int, uint32_t)> entrada_b;
  std::function<void(int, float)> valor_b_relativo;
  std::function<bool(int, bool)> avanzar_b;
  // Memoria externa opcional. false = direccion desconocida, error explicito.
  std::function<bool(uint32_t, uint8_t&)> leer;
  std::function<bool(uint32_t, uint8_t)> escribir;
};

class Instancia {
 public:
  explicit Instancia(Configuracion config = {}) : cfg_(config) {}
  Instancia(const Instancia&) = delete;
  Instancia& operator=(const Instancia&) = delete;
  Ganchos ganchos;
  std::function<void(const Traza&)> traza;

  void Conectar(Host host) { host_ = std::move(host); }
  void Registrar(uint32_t tabla, uint32_t id, Funcion fn) {
    if (id > 0xFFFF || !fn) throw std::invalid_argument("builtin o callback invalido");
    nativos_[{tabla, id}] = std::move(fn);
  }

  // No instala numeros B2 en IW. Estas parejas tabla/ID fueron leidas del ELF
  // EU: 484930[11][6,8,9], [7][5]; 4582C0[17][7,8]. No hay 0x117 en capsulas.
  void InstalarComunesEU() {
    Registrar(kCapsulasEU, 0x116, [](Instancia& m, Hilo& h) {
      m.Exigir(bool(m.host_.botones), "Host.botones ausente");
      const uint32_t indice = m.Arg(h, 0);
      m.Exigir(indice < 2, "indice de mascara de botones fuera de tabla");
      // EU49C4E8 = {8,1}; 364EF0, lineas455561..455582.
      h.r[0] = (m.host_.botones() & (indice == 0 ? 8 : 1)) != 0;
      return 1u;
    });
    Registrar(kCapsulasEU, 0x118, [](Instancia& m, Hilo&) {
      m.Exigir(bool(m.host_.transicion), "Host.transicion ausente");
      m.host_.transicion(10);  // EU281E80, 301030..301041.
      return 1u;
    });
    Registrar(kCapsulasEU, 0x119, [](Instancia& m, Hilo&) {
      m.Exigir(bool(m.host_.capsulas_listas), "Host.capsulas_listas ausente");
      m.host_.capsulas_listas();  // EU364ED0 -> 281CA0, 455547..455557.
      return 1u;
    });
    Registrar(kCapsulasEU, 0x75, [](Instancia& m, Hilo& h) {
      m.Exigir(bool(m.host_.parar_secuencia_iop), "Host.parar_secuencia_iop ausente");
      m.host_.parar_secuencia_iop(m.Arg(h, 0));  // 283130 ->13FA20, RPC IOP0x13.
      return 1u;
    });
    Registrar(kCombateEU, 0x177, [](Instancia& m, Hilo& h) { return m.AnimVueltas(h); });
    Registrar(kCombateEU, 0x178, [](Instancia& m, Hilo& h) { return m.AnimTemporizada(h); });
  }

  // Helper EU364FC0/364F70, sin inventar tabla/ID: el consumidor debe enlazar
  // el simbolo en SU contexto verificado. Instala sondeo y comprueba de inmediato.
  uint32_t EsperarBoton8(Hilo& h) {
    h.sondeo = [](Instancia& m, Hilo& t) {
      m.Exigir(bool(m.host_.botones), "Host.botones ausente");
      if (!(m.host_.botones() & 8)) return 3u;
      t.sondeo = {};
      return 1u;
    };
    auto fn = h.sondeo;
    return fn(*this, h);
  }

  bool Cargar(std::vector<uint8_t> bytes, uint32_t tabla) {
    if (en_tick_ || vivos_) return FalloPublico("Cargar requiere instancia detenida");
    error_.clear(); bloqueada_ = false; estado_ = 1; cuadro_ = 0;
    regiones_.clear(); agenda_.clear(); tabla_ = tabla;
    for (size_t i = 0; i < kMaxRanuras; ++i) { hilos_[i] = {}; libres_[i] = int(i); }
    try {
      Exigir(cfg_.ranuras > 0 && cfg_.ranuras <= kMaxRanuras, "numero de ranuras invalido");
      Exigir(bytes.size() >= 0x24 && std::memcmp(bytes.data(), "#SPX", 4) == 0, "cabecera #SPX invalida");
      tam_guion_ = uint32_t(bytes.size());
      Exigir(uint64_t(bytes.size()) == tam_guion_, "guion demasiado grande");
      Mapear(cfg_.guion, std::move(bytes));
      const uint32_t relativo = Leer32(cfg_.guion + 0x14);
      Exigir(relativo >= 0x20 && relativo < tam_guion_, "base de codigo fuera de guion");
      base_ = cfg_.guion + relativo;
      entradas_ = Leer32(cfg_.guion + 0x18);
      Exigir(uint64_t(0x20) + 4ull * entradas_ <= relativo, "tabla de entradas fuera de cabecera");
      Exigir(cfg_.bytes_pila >= 8 && !(cfg_.bytes_pila & 3), "tamano de pila invalido");
      Mapear(cfg_.compartida, std::vector<uint8_t>(cfg_.bytes_compartida));
      Mapear(cfg_.locales, std::vector<uint8_t>(cfg_.bytes_locales));
      Mapear(cfg_.temporal, std::vector<uint8_t>(TamTotal(cfg_.bytes_temporal)));
      Mapear(cfg_.pilas, std::vector<uint8_t>(TamTotal(cfg_.bytes_pila)));
      // Registros son un mapeo vivo, no una copia desincronizada de Hilo::r/f.
      for (const auto& reg : regiones_)
        Exigir(!Solapan(cfg_.registros, cfg_.ranuras * 0x80, reg.base, reg.bytes.size()), "registros solapan memoria");
      Exigir(uint64_t(cfg_.registros) + cfg_.ranuras * 0x80 <= 0x100000000ull, "registros fuera de rango");
      return true;
    } catch (const std::exception& e) { return FalloPublico(e.what()); }
  }

  int Iniciar(uint32_t entrada = 0, uint32_t dato = 0) {
    if (bloqueada_) return -1;
    try {
      Exigir(estado_ != 0 || tam_guion_ != 0, "sin guion cargado");
      Exigir(entrada < entradas_, "entrada SPX fuera de tabla");
      auto gancho = ganchos.inicio;
      if (gancho) gancho(*this, entrada);
      const int slot = Crear(Destino(Leer32(cfg_.guion + 0x20 + 4 * entrada)), dato, nullptr);
      estado_ = 2;
      return slot;
    } catch (const std::exception& e) { FalloPublico(e.what()); return -1; }
  }

  // Un tick del planificador IW140200: orden de prioridad y de insercion,
  // incluyendo hijos posteriores. La lista conserva tombstones hasta terminar.
  bool Tick() {
    if (bloqueada_) return false;
    if (en_tick_) return FalloPublico("Tick reentrante");
    en_tick_ = true; ++cuadro_; pasos_ = 0;
    try {
      for (auto it = agenda_.begin(); it != agenda_.end(); ++it) {
        Hilo& h = hilos_[size_t(it->slot)];
        if (h.activo && h.generacion == it->generacion) Ejecutar(h);
      }
      agenda_.remove_if([&](const Tarea& t) {
        const Hilo& h = hilos_[size_t(t.slot)];
        return !h.activo || h.generacion != t.generacion;
      });
      en_tick_ = false;
      return true;
    } catch (const std::exception& e) {
      en_tick_ = false;
      return FalloPublico(e.what());
    }
  }

  void Parar() {
    try { CancelarGrupo(); } catch (const std::exception& e) { FalloPublico(e.what()); }
  }
  void Pausar(bool pausa) { if (!bloqueada_) estado_ = pausa ? 3 : (vivos_ ? 2 : 0); }
  bool Viva() const { return vivos_ != 0; }
  bool Bloqueada() const { return bloqueada_; }
  const std::string& Error() const { return error_; }
  int Estado() const { return estado_; }
  size_t Vivos() const { return vivos_; }
  uint64_t Cuadro() const { return cuadro_; }
  uint32_t Tabla() const { return tabla_; }
  void PonerTabla(uint32_t tabla) { tabla_ = tabla; }  // EU145B20, instancia+44.
  uint32_t Base() const { return base_; }
  Hilo& HiloEn(size_t slot) { return hilos_.at(slot); }
  const Hilo& HiloEn(size_t slot) const { return hilos_.at(slot); }
  // Orden de la lista140200, tambien para los pases de dibujo del host.
  // La ranura del pool no es prioridad; puede reutilizarse tras morir un task.
  std::vector<size_t> OrdenVivo() const {
    std::vector<size_t> orden; orden.reserve(vivos_);
    for(const auto& t:agenda_) {
      const auto& h=hilos_[size_t(t.slot)];
      if(h.activo && h.generacion==t.generacion) orden.push_back(size_t(t.slot));
    }
    return orden;
  }
  uint32_t Arg(const Hilo& h, uint32_t indice) {
    const uint64_t addr = uint64_t(h.r[11]) + 4ull * indice;
    Exigir(addr + 4 <= uint64_t(h.r[15]) + cfg_.bytes_pila, "argumento fuera de pila");
    return Leer32(uint32_t(addr));
  }

  // Todas las direcciones son bytes LE, incluso desalineadas. Las regiones
  // desconocidas fallan, salvo que Host.leer/escribir las resuelva realmente.
  void Mapear(uint32_t addr, std::vector<uint8_t> bytes, bool escribible = true) {
    Exigir(!bytes.empty() && uint64_t(addr) + bytes.size() <= 0x100000000ull, "mapeo invalido");
    for (const auto& r : regiones_)
      Exigir(!Solapan(addr, bytes.size(), r.base, r.bytes.size()), "regiones solapadas");
    if (tam_guion_ && addr != cfg_.guion)
      Exigir(!Solapan(addr, bytes.size(), cfg_.registros, cfg_.ranuras * 0x80), "mapeo solapa registros");
    regiones_.push_back({addr, std::move(bytes), escribible});
  }
  uint8_t Leer8(uint32_t a) {
    if (EsRegistro(a)) {
      const uint32_t d = a - cfg_.registros;
      Hilo& h = hilos_[d / 0x80];
      const uint32_t i = (d % 0x80) / 4;
      return uint8_t((i < 16 ? h.r[i] : h.f[i - 16]) >> ((d & 3) * 8));
    }
    for (const auto& r : regiones_)
      if (a >= r.base && uint64_t(a) - r.base < r.bytes.size()) return r.bytes[a - r.base];
    uint8_t value = 0;
    if (host_.leer && host_.leer(a, value)) return value;
    Fallar("lectura sin mapear en " + Hex(a));
  }
  uint16_t Leer16(uint32_t a) { Rango(a, 2); return uint16_t(Leer8(a) | uint16_t(Leer8(a + 1)) << 8); }
  uint32_t Leer32(uint32_t a) {
    Rango(a, 4); uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= uint32_t(Leer8(a + i)) << (8 * i);
    return v;
  }
  void Escribir8(uint32_t a, uint8_t v) {
    if (EsRegistro(a)) {
      const uint32_t d = a - cfg_.registros, i = (d % 0x80) / 4;
      Hilo& h = hilos_[d / 0x80];
      uint32_t& r = i < 16 ? h.r[i] : h.f[i - 16];
      const unsigned shift = (d & 3) * 8;
      r = (r & ~(0xFFu << shift)) | uint32_t(v) << shift;
      return;
    }
    for (auto& r : regiones_) {
      if (a < r.base || uint64_t(a) - r.base >= r.bytes.size()) continue;
      Exigir(r.escribible, "escritura en memoria de solo lectura");
      r.bytes[a - r.base] = v;
      return;
    }
    if (host_.escribir && host_.escribir(a, v)) return;
    Fallar("escritura sin mapear en " + Hex(a));
  }
  void Escribir16(uint32_t a, uint16_t v) { Rango(a, 2); for (unsigned i = 0; i < 2; ++i) Escribir8(a + i, uint8_t(v >> (8 * i))); }
  void Escribir32(uint32_t a, uint32_t v) { Rango(a, 4); for (unsigned i = 0; i < 4; ++i) Escribir8(a + i, uint8_t(v >> (8 * i))); }
  [[noreturn]] void Fallar(const std::string& mensaje) {
    bloqueada_ = true; error_ = mensaje;
    throw std::runtime_error(mensaje);
  }

 private:
  struct Region { uint32_t base; std::vector<uint8_t> bytes; bool escribible; };
  struct Tarea { int slot; uint64_t generacion; uint32_t prioridad; };
  Configuracion cfg_;
  Host host_;
  std::array<Hilo, kMaxRanuras> hilos_{};
  std::array<int, kMaxRanuras> libres_{};
  std::vector<Region> regiones_;
  std::list<Tarea> agenda_;
  std::map<std::pair<uint32_t, uint32_t>, Funcion> nativos_;
  uint32_t tabla_ = 0, base_ = 0, tam_guion_ = 0, entradas_ = 0;
  uint64_t cuadro_ = 0, generacion_ = 0;
  size_t vivos_ = 0, pasos_ = 0;
  int estado_ = 0;
  bool bloqueada_ = false, en_tick_ = false;
  std::string error_;
  int actual_ = -1;
  uint32_t pc_actual_ = 0;

  static std::string Hex(uint32_t v) {
    std::string s(8, '0');
    for (int i = 7; i >= 0; --i) { s[size_t(i)] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    return "0x" + s;
  }
  void Exigir(bool ok, const std::string& s) { if (!ok) Fallar(s); }
  void Rango(uint32_t a, uint32_t n) { Exigir(uint64_t(a) + n <= 0x100000000ull, "direccion desbordada"); }
  static bool Solapan(uint32_t a, uint64_t n, uint32_t b, uint64_t m) {
    return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
  }
  size_t TamTotal(uint32_t bytes) {
    Exigir(bytes > 0 && uint64_t(bytes) * cfg_.ranuras <= 0xFFFFFFFFull, "tamano por hilo invalido");
    return size_t(bytes) * cfg_.ranuras;
  }
  bool EsRegistro(uint32_t a) const { return a >= cfg_.registros && uint64_t(a) - cfg_.registros < cfg_.ranuras * 0x80; }
  bool FalloPublico(const std::string& s) {
    bloqueada_ = true; error_ = s;
    if (traza) {
      Traza t; t.fase = Traza::Fase::Error; t.cuadro = cuadro_; t.tabla = tabla_;
      t.ranura = actual_; t.pc = pc_actual_;
      traza(t);
    }
    return false;
  }
  void Emitir(Traza::Fase fase, const Hilo& h, uint32_t pc, uint32_t cod = 0, uint32_t ret = 0) {
    if (traza) traza({fase, cuadro_, h.generacion, h.ranura, tabla_, pc, cod, ret, h.r});
  }
  uint32_t Destino(uint32_t offset) const { return base_ + (offset & kMascaraDestino); }
  uint32_t Destino(const Hilo& h, uint32_t offset) const { return h.r[8] + (offset & kMascaraDestino); }
  void ValidarPC(uint32_t pc) { Exigir(pc >= base_ && uint64_t(pc) < uint64_t(cfg_.guion) + tam_guion_, "PC fuera del guion: " + Hex(pc)); }

  int Crear(uint32_t pc, uint32_t dato, const Hilo* padre) {
    Exigir(vivos_ < cfg_.ranuras, "sin ranuras IW (" + std::to_string(cfg_.ranuras) + " hilos activos)");
    ValidarPC(pc);
    const int slot = libres_[vivos_];
    Hilo& h = hilos_[size_t(slot)];
    h = {}; h.ranura = slot; h.generacion = ++generacion_; h.activo = true;
    h.direccion = cfg_.registros + uint32_t(slot) * 0x80;
    h.dato = dato; h.prioridad = cfg_.prioridad + dato;
    h.r[8] = base_; h.r[9] = pc; h.r[10] = pc;
    h.r[12] = cfg_.compartida; h.r[13] = cfg_.locales;
    h.r[14] = cfg_.temporal + uint32_t(slot) * cfg_.bytes_temporal;
    h.r[15] = cfg_.pilas + uint32_t(slot) * cfg_.bytes_pila;
    h.r[11] = h.r[15] + cfg_.bytes_pila;
    // EU1491B4..C4 limpia solo datos temporales por hilo; pila se reinicia por SP.
    for (uint32_t i = 0; i < cfg_.bytes_temporal; ++i) Escribir8(h.r[14] + i, 0);
    ++vivos_;
    auto pos = agenda_.begin();
    while (pos != agenda_.end() && pos->prioridad <= h.prioridad) ++pos;
    agenda_.insert(pos, {slot, h.generacion, h.prioridad});
    auto fn = ganchos.crear;
    if (fn) fn(*this, h, padre);
    Emitir(Traza::Fase::Crear, h, pc);
    return slot;
  }
  void Terminar(Hilo& h) {
    if (!h.activo) return;
    auto fn = ganchos.terminar;
    if (fn) fn(*this, h);
    Emitir(Traza::Fase::Fin, h, h.r[9]);
    h.activo = false;
    libres_[--vivos_] = h.ranura;  // mismo stack de ranuras que EU145FA0.
    if (!vivos_ && estado_ != 4) {
      auto fin = ganchos.fin;
      if (fin) fin(*this);
      estado_ = 0;
    }
  }
  void CancelarGrupo() {
    // Todos los tasks de ESTA instancia comparten instancia+14/+18.
    // 1404C0(a0=grupo,a1=flags,a2=1) elimina el grupo, no solo el hilo actual.
    std::array<uint64_t, kMaxRanuras> generaciones{};
    for (size_t i = 0; i < kMaxRanuras; ++i) generaciones[i] = hilos_[i].generacion;
    for (auto& h : hilos_) if (h.activo && h.generacion == generaciones[size_t(h.ranura)]) Terminar(h);
  }
  void Ejecutar(Hilo& h) {
    actual_ = h.ranura;
    pc_actual_ = h.r[9];
    const uint64_t gen = h.generacion;
    uint32_t pc = h.r[9];  // original captura PC ANTES del callback previo.
    auto pre = h.previo;
    if (pre) { pre(*this, h); Emitir(Traza::Fase::Previo, h, pc); }
    if (!h.activo || h.generacion != gen) return;
    uint32_t ret = 1;
    auto poll = h.sondeo;  // copia: el sondeo puede borrarse a si mismo.
    if (poll) { ret = poll(*this, h); Emitir(Traza::Fase::Sondeo, h, pc, 0, ret); }
    bool procesar = (ret & 0xFF) != 1;
    if (!procesar && (estado_ == 3 || h.espera > 0)) {
      if (estado_ != 3) --h.espera;
      Posterior(h, pc);
      return;
    }
    for (;;) {
      Exigir(++pasos_ <= cfg_.limite_pasos, "hilo sin ceder: limite de instrucciones");
      if (!procesar) {
        pc_actual_ = pc;
        const uint32_t pos = pc;
        const uint8_t op = Byte(pc);
        ret = Op(h, pc, op);
        Emitir(Traza::Fase::Opcode, h, pos, op, ret);
      }
      procesar = false;
      while (ret != 1) {
        const uint32_t c = ret & 0xFF;
        const int16_t demora = int16_t(uint16_t(ret >> 8));
        switch (c) {
          case 2:
            h.espera = demora; h.r[11] = h.r[15] + cfg_.bytes_pila;
            h.r[9] = h.r[10]; Posterior(h, pc); return;
          case 3:
            h.espera = demora; h.r[9] = pc; Posterior(h, pc); return;
          case 4: Terminar(h); Posterior(h, pc); return;
          case 5: {
            // 145E68..145ED0: el gancho50 recibe el BANCO del hilo viejo,
            // no una instantanea. Si la ranura se reutiliza, padre==nuevo y
            // sus registros ya estan reiniciados (confirmado ejecutando ELF).
            estado_ = 4; CancelarGrupo(); estado_ = 2;
            Crear(h.r[0], h.r[1], &h);
            return;  // 145ED0: no posterior.
          }
          case 6: CancelarGrupo(); Posterior(h, pc); return;
          case 7: {
            Exigir(++pasos_ <= cfg_.limite_pasos, "cadena de builtins sin ceder");
            h.r[9] = pc;
            const uint32_t id = (ret >> 8) & 0xFFFF;
            auto it = nativos_.find({tabla_, id});
            Exigir(it != nativos_.end(), "builtin desconocido tabla=" + Hex(tabla_) + " id=" + Hex(id));
            auto fn = it->second;
            ret = fn(*this, h);
            Emitir(Traza::Fase::Nativo, h, pc, id, ret);
            break;
          }
          case 8: return;  // NO guardar pc local ni llamar posterior.
          default: Fallar("retorno VM invalido: " + Hex(ret));
        }
        if (bloqueada_) Fallar(error_);
      }
      if (!h.activo || h.generacion != gen) return;
    }
  }
  void Posterior(Hilo& h, uint32_t pc) {
    auto fn = h.posterior;
    if (fn) { fn(*this, h); Emitir(Traza::Fase::Posterior, h, pc); }
  }

  uint8_t Byte(uint32_t& pc) { ValidarPC(pc); const auto b = Leer8(pc); ++pc; return b; }
  uint32_t Inmediato(uint32_t& pc, unsigned bytes) {
    uint32_t v = 0;
    for (unsigned i = 0; i < bytes; ++i) v |= uint32_t(Byte(pc)) << (i * 8);
    if (bytes == 1 && (v & 0x80)) v |= 0xFFFFFF00;
    if (bytes == 2 && (v & 0x8000)) v |= 0xFFFF0000;
    return v;
  }
  uint32_t Direccion(Hilo& h, uint32_t& pc, unsigned modo, unsigned n) {
    switch (modo) {
      case 4: return h.r[n];
      case 5: return h.r[n] + Inmediato(pc, 1);
      case 6: return h.r[n] + Inmediato(pc, 2);
      case 7: return h.r[n] + h.r[Byte(pc) >> 4];
      default: Fallar("modo de operando invalido");
    }
  }
  // 148970/148B30/148D10/148EE0: modos 0..7, inmediatos LE con signo;
  // los inmediatos flotantes se cargan como BITS, no conversion numerica.
  uint32_t Valor(Hilo& h, uint32_t& pc, bool flotante = false) {
    const unsigned b = Byte(pc), modo = b >> 4, n = b & 15;
    if (!modo) return flotante ? h.f[n] : h.r[n];
    if (modo <= 3) return Inmediato(pc, 1u << (modo - 1));
    return Leer32(Direccion(h, pc, modo, n));
  }
  uint32_t Dest(Hilo& h, uint32_t& pc, bool flotante = false) {
    const unsigned b = Byte(pc), modo = b >> 4, n = b & 15;
    if (!modo) return h.direccion + (flotante ? 0x40 : 0) + n * 4;
    if (modo <= 3) return Inmediato(pc, 1u << (modo - 1));
    return Direccion(h, pc, modo, n);
  }
  void Push(Hilo& h, uint32_t v) {
    Exigir(h.r[11] >= h.r[15] + 4 && uint64_t(h.r[11]) <= uint64_t(h.r[15]) + cfg_.bytes_pila, "pila desbordada");
    h.r[11] -= 4; Escribir32(h.r[11], v);
  }
  uint32_t Pop(Hilo& h) {
    Exigir(h.r[11] >= h.r[15] && uint64_t(h.r[11]) + 4 <= uint64_t(h.r[15]) + cfg_.bytes_pila, "pila vacia");
    const uint32_t v = Leer32(h.r[11]); h.r[11] += 4; return v;
  }
  bool Vacia(const Hilo& h) const { return uint64_t(h.r[11]) >= uint64_t(h.r[15]) + cfg_.bytes_pila; }
  uint32_t Entero(float f) {
    // Ghidra pierde tipos COP1. No inventar saturacion PowerPC para R5900:
    // los casos no finitos/fuera de rango se bloquean hasta tener oraculo EE.
    Exigir(std::isfinite(f) && double(f) >= -2147483648.0 && double(f) < 2147483648.0,
           "conversion EE fuera de rango: requiere oraculo R5900");
    return uint32_t(int32_t(f));
  }
  static int32_t Dividir(int32_t a, int32_t b, bool resto) {
    if (!b) return 0;
    if (a == std::numeric_limits<int32_t>::min() && b == -1) return resto ? 0 : a;
    return resto ? a % b : a / b;
  }

  uint32_t Op(Hilo& h, uint32_t& pc, uint8_t op) {
    switch (op) {
      case 0x00: return 1;
      case 0x01: case 0x03: {
        const uint32_t v = Valor(h, pc, op == 3), d = Dest(h, pc, op == 3);
        Escribir32(d, v); return 1;
      }
      case 0x02: case 0x04: case 0x05:
        Escribir32(Dest(h, pc, op != 2), op == 5 ? Bits(1.f) : 0); return 1;
      case 0x06: { const uint32_t v = Valor(h, pc); Escribir32(Dest(h, pc, true), Bits(float(Signo(v)))); return 1; }
      case 0x07: { const uint32_t v = Entero(Float(Valor(h, pc, true))); Escribir32(Dest(h, pc), v); return 1; }
      case 0x08: case 0x09: Push(h, Valor(h, pc, op == 9)); return 1;
      case 0x0A: case 0x0B: {
        // 1463B0/146410: copiar, DESPUES incrementar SP (importa con destino r11).
        Exigir(!Vacia(h) && h.r[11] >= h.r[15], "pila vacia");
        const uint32_t v = Leer32(h.r[11]), d = Dest(h, pc, op == 0x0B);
        Escribir32(d, v); h.r[11] += 4; return 1;
      }
      case 0x10: case 0x11: {
        uint32_t d = Dest(h, pc);
        // IW146470/1464E0: byte a byte, incluido acarreo sobre desalineados.
        for (unsigned i = 0; i < 4; ++i) {
          const uint8_t b = uint8_t(Leer8(d + i) + (op == 0x10 ? 1 : -1));
          Escribir8(d + i, b);
          if (op == 0x10 ? b != 0 : b != 0xFF) break;
        }
        return 1;
      }
      case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x18:
      case 0x30: case 0x31: case 0x32:
      case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: {
        const uint32_t b = Valor(h, pc), d = Dest(h, pc), a = Leer32(d);
        uint32_t v = 0;
        switch (op) {
          case 0x12: v = a + b; break;
          case 0x13: v = a - b; break;
          case 0x14: v = a * b; break;
          case 0x15: case 0x16: v = uint32_t(Dividir(Signo(a), Signo(b), op == 0x16)); break;
          case 0x18: v = Entero(std::pow(float(Signo(a)), float(Signo(b)))); break;
          case 0x30: v = a & b; break;
          case 0x31: v = a | b; break;
          case 0x32: v = a ^ b; break;
          case 0x60: v = a << (b & 31); break;
          case 0x61: v = a >> (b & 31); break;
          case 0x62: {
            const unsigned s = b & 31;
            v = s ? (a >> s) | ((a & 0x80000000) ? (~0u << (32 - s)) : 0) : a;
            break;
          }
          default: {
            const unsigned s = (Signo(b) < 0 ? 0u - b : b) & 31;
            const bool izq = (op == 0x63) == (Signo(b) > 0);
            v = !s ? a : izq ? (a << s) | (a >> (32 - s)) : (a >> s) | (a << (32 - s));
          }
        }
        Escribir32(d, v); return 1;
      }
      case 0x17: case 0x33: { const uint32_t d = Dest(h, pc), v = Leer32(d); Escribir32(d, op == 0x17 ? 0u - v : ~v); return 1; }
      case 0x20: case 0x21: case 0x27: case 0x29: {
        const uint32_t d = Dest(h, pc, true); const float a = Float(Leer32(d));
        const float v = op == 0x20 ? a + 1.f : op == 0x21 ? a - 1.f : op == 0x27 ? -a : std::sqrt(a);
        Escribir32(d, Bits(v)); return 1;
      }
      case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x28: {
        const float b = Float(Valor(h, pc, true)); const uint32_t d = Dest(h, pc, true);
        const float a = Float(Leer32(d)); float v = 0;
        switch (op) {
          case 0x22: v = b + a; break;
          case 0x23: v = a - b; break;
          case 0x24: v = b * a; break;
          case 0x25: v = b != 0.f ? a / b : 0.f; break;
          case 0x26: v = b != 0.f ? float(Dividir(Signo(Entero(a)), Signo(Entero(b)), true)) : 0.f; break;
          case 0x28: v = std::pow(a, b); break;
        }
        Escribir32(d, Bits(v)); return 1;
      }
      case 0x40: case 0x41: case 0x42: {
        constexpr float dos_pi = 6.2831855f;
        const float a = float(uint16_t(Valor(h, pc))) * dos_pi / 65536.f;
        const float v = op == 0x40 ? std::sin(a) : op == 0x41 ? std::cos(a) : std::tan(a);
        Escribir32(Dest(h, pc, true), Bits(v)); return 1;
      }
      case 0x43: case 0x44: case 0x45: {
        constexpr float dos_pi = 6.2831855f;
        const float a = Float(Valor(h, pc, true));
        const float v = op == 0x43 ? std::asin(a) : op == 0x44 ? std::acos(a) : std::atan(a);
        const uint32_t i = Entero(v * 65536.f / dos_pi) & 0xFFFF;
        Escribir32(Dest(h, pc), i); return 1;
      }
      case 0x46: {
        const float a = Float(Valor(h, pc, true)), b = Float(Valor(h, pc, true));
        h.r[0] = Entero(std::atan2(a, b) * 65536.f / 6.2831855f) & 0xFFFF; return 1;
      }
      case 0x50: case 0x52: {
        const uint32_t a = Valor(h, pc, op == 0x52), b = Valor(h, pc, op == 0x52);
        h.r[0] = 0u - uint32_t(op == 0x52 ? Float(b) < Float(a) : Signo(b) < Signo(a)); return 1;
      }
      case 0x51: case 0x53: {
        const uint32_t a = Valor(h, pc, op == 0x53);
        h.r[0] = 0u - uint32_t(op == 0x53 ? Float(a) != 0.f : a != 0); return 1;
      }
      case 0x70: pc = Destino(h, Valor(h, pc)); return 1;
      case 0x71: case 0x72: case 0x74: case 0x75: {
        const uint32_t c = Valor(h, pc), a = Valor(h, pc);
        if ((c != 0) == (op == 0x71 || op == 0x74)) {
          if (op >= 0x74) Push(h, pc);
          pc = Destino(h, a);
        }
        return 1;
      }
      case 0x73: { const uint32_t a = Valor(h, pc); Push(h, pc); pc = Destino(h, a); return 1; }
      case 0x76: if (Vacia(h)) return 2; pc = Pop(h); return 1;
      case 0x77: {
        const uint32_t a = Valor(h, pc);
        if (Vacia(h)) return 2;
        pc = Destino(h, a); Pop(h); return 1;  // IW1482D0 suma BASE (B2 omitia).
      }
      case 0x78: case 0x79: case 0x7A: case 0x7B: {
        const uint32_t c = Valor(h, pc);
        const uint32_t a = (op & 1) ? Valor(h, pc) : 0;
        if ((c != 0) == (op < 0x7A)) {
          if (Vacia(h)) return 2;
          const uint32_t pila = Pop(h);
          pc = (op & 1) ? Destino(h, a) : pila;
        }
        return 1;
      }
      case 0x80: return Valor(h, pc) * 0x100u + 7u;
      case 0x90: h.r[10] = Destino(h, Valor(h, pc)); return 1;
      case 0x91: case 0x98: {
        const uint32_t a = Valor(h, pc);
        const uint32_t dato = op == 0x98 ? Valor(h, pc) : Arg(h, 1);
        Crear(Destino(h, a), dato, &h); return 1;
      }
      case 0x92: return 3;
      case 0x93: {
        // 148818 ADDIU resta en32bits ANTES del BGEZ; INT_MIN envuelve a
        // INT_MAX. Evitar overflow C++ sin cambiar el retorno retail.
        uint32_t n = Valor(h, pc) - 1u;
        if (Signo(n) < 0) n = 0;
        return (n << 8) + 3u;
      }
      case 0x94: return 2;
      case 0x95: return 4;
      case 0x96: case 0x99:
        h.r[0] = Destino(h, Valor(h, pc));
        h.r[1] = op == 0x99 ? Valor(h, pc) : Arg(h, 1);
        return 5;
      case 0x97: return 6;
      default: Fallar("opcode desconocido " + Hex(op));
    }
  }

  void ExigirAnim() {
    Exigir(host_.paso && host_.pausado && host_.entrada_b && host_.valor_b_relativo && host_.avanzar_b,
           "Host de animacion incompleto");
  }
  uint32_t AnimTemporizada(Hilo& h) {
    // EU1C11A0/1C1100, 159881..159935 y159839..159877.
    ExigirAnim();
    h.anim.slot = int(int8_t(uint8_t(Arg(h, 2))));
    h.anim.tiempo = -host_.paso(); h.anim.limite = float(Signo(Arg(h, 0)));
    h.sondeo = [](Instancia& m, Hilo& t) {
      if (m.host_.pausado()) return 3u;
      if (t.anim.limite != -1.f) {
        t.anim.tiempo += m.host_.paso();
        if (t.anim.limite <= t.anim.tiempo) { t.sondeo = {}; return 1u; }
      }
      m.host_.avanzar_b(t.anim.slot, true);
      return 3u;
    };
    host_.entrada_b(h.anim.slot, Arg(h, 1)); host_.valor_b_relativo(h.anim.slot, 0.f);
    auto poll = h.sondeo; return poll(*this, h);
  }
  uint32_t AnimVueltas(Hilo& h) {
    // EU1C1320/1C12B0, 159970..160007 y159939..159965.
    ExigirAnim();
    h.anim.slot = int(int8_t(uint8_t(Arg(h, 2))));
    h.anim.vueltas = int(int8_t(uint8_t(Arg(h, 0))));
    h.sondeo = [](Instancia& m, Hilo& t) {
      if (!m.host_.avanzar_b(t.anim.slot, true) || t.anim.vueltas < 1) return 3u;
      if (--t.anim.vueltas) return 3u;
      t.sondeo = {}; return 1u;
    };
    host_.entrada_b(h.anim.slot, Arg(h, 1)); host_.valor_b_relativo(h.anim.slot, 0.f);
    auto poll = h.sondeo; return poll(*this, h);
  }
};

}  // namespace dbz3::iw::spx
