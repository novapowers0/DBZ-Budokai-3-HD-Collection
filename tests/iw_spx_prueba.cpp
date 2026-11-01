// Standalone: clang++ -std=c++17 -Wall -Wextra -Werror -pedantic
//   tests/iw_spx_prueba.cpp -o D:/tmp/solIWV/iw_spx_prueba.exe
// Sin juego/SDK/ventanas. Opcional: argumento = SPX real993/994 para probar
// bloqueo ante primer builtin sin consumidor (no simula cierre de pantalla).
#include "../src/iw_spx.h"
#include <fstream>
#include <iostream>
#include <iterator>

namespace S = dbz3::iw::spx;
namespace {
int comprobaciones = 0, grupos = 0;
void Ver(bool ok, const std::string& mensaje) {
  ++comprobaciones;
  if (!ok) throw std::runtime_error(mensaje);
}
void Put(std::vector<uint8_t>& b, size_t p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) b.at(p + i) = uint8_t(n >> (i * 8));
}
struct Codigo {
  std::vector<uint8_t> b;
  uint32_t Pos() const { return uint32_t(b.size()); }
  void B(uint8_t n) { b.push_back(n); }
  void U(uint32_t n) { for (unsigned i = 0; i < 4; ++i) B(uint8_t(n >> (8 * i))); }
  void I(uint32_t n) { B(0x30); U(n); }
  void R(unsigned n) { B(uint8_t(n)); }
  void M(unsigned n) { B(uint8_t(0x40 | n)); }
  void Mov(uint32_t v, unsigned r) { B(1); I(v); R(r); }
  void Push(uint32_t v) { B(8); I(v); }
  void Pop(unsigned r) { B(0x0A); R(r); }
  void Native(uint32_t id) { B(0x80); I(id); }
  void Ceder() { B(0x92); }
  void Fin() { B(0x95); }
  size_t Ref(uint8_t op, uint32_t flags = 0) { B(op); B(0x30); size_t p = b.size(); U(flags); return p; }
  void Fijar(size_t p, uint32_t destino, uint32_t flags = 0) { Put(b, p, destino | flags); }
  std::vector<uint8_t> Guion(std::vector<uint32_t> entradas = {0}) const {
    const uint32_t base = 0x20 + 4 * uint32_t(entradas.size());
    std::vector<uint8_t> d(base, 0);
    std::memcpy(d.data(), "#SPX ver 0.01", 13);
    Put(d, 0x10, 0x20); Put(d, 0x14, base); Put(d, 0x18, uint32_t(entradas.size()));
    for (size_t i = 0; i < entradas.size(); ++i) Put(d, 0x20 + 4 * i, entradas[i]);
    d.insert(d.end(), b.begin(), b.end());
    return d;
  }
};
constexpr uint32_t tabla = 0xABCDEF;

void MemoriaOperandos() {
  Codigo c;
  c.B(1); c.B(0x10); c.B(0xFE); c.R(0);  // i8=-2
  c.B(1); c.B(0x20); c.B(0); c.B(0x80); c.R(1);  // i16=-32768
  c.Mov(0x260001, 2);
  c.B(1); c.I(0x78563412); c.M(2);  // escritura desalineada
  c.B(1); c.B(0x52); c.B(0xFF); c.R(3);  // r2-1, byte anterior A5
  c.B(1); c.B(0x62); c.B(1); c.B(0); c.R(4);  // r2+1
  c.Mov(2, 6);
  c.B(1); c.B(0x72); c.B(0x60); c.R(5);  // r2+r6
  c.Mov(0x260001, 7);
  c.B(0x10); c.M(7); c.B(0x11); c.M(7); // acarreo byte a byte
  // Memoria del propio guion y alias de registro, no un mapa separado de words.
  c.B(1); c.B(0x30); const size_t parche = c.b.size(); c.U(0); c.R(6);
  c.B(1); c.M(6); c.R(7);
  c.Ceder(); c.Fin();
  const uint32_t datos = c.Pos(); c.U(0xDEADBEEF);
  S::Instancia m;
  c.Fijar(parche, 0x100024 + datos);
  Ver(m.Cargar(c.Guion(), tabla), "cargar operandos");
  m.Mapear(0x260000, std::vector<uint8_t>(16, 0xA5));
  Ver(m.Iniciar() == 0 && m.Tick(), "tick operandos");
  const auto& h = m.HiloEn(0);
  Ver(S::Signo(h.r[0]) == -2 && S::Signo(h.r[1]) == -32768, "extension de signo LE");
  Ver(m.Leer8(0x260000) == 0xA5 && m.Leer32(0x260001) == 0x78563412, "memoria byte-addressable");
  Ver(h.r[3] == 0x563412A5 && h.r[4] == 0xA5785634 && h.r[5] == 0xA5A57856, "modos 5/6/7");
  Ver(h.r[7] == 0xDEADBEEF, "leer datos del guion");
  m.Escribir8(h.direccion + 1, 0x33);
  Ver(h.r[0] == 0xFFFF33FE, "alias de registros LE");
  Ver(m.Tick() && !m.Viva(), "fin operandos");
  ++grupos;
}

void AritmeticaYFloat() {
  Codigo c;
  c.Mov(0xFFFFFFFF, 0); c.B(0x12); c.I(2); c.R(0);
  c.Mov(uint32_t(-13), 1); c.B(0x15); c.I(5); c.R(1);
  c.Mov(uint32_t(-13), 2); c.B(0x16); c.I(5); c.R(2);
  c.Mov(0x80000000, 3); c.B(0x62); c.I(4); c.R(3);
  c.Mov(0x80000001, 4); c.B(0x63); c.I(1); c.R(4);
  c.Mov(0x00FFFFFF, 6); c.B(0x10); c.R(6);
  c.Mov(0x00010000, 7); c.B(0x11); c.R(7);
  c.B(3); c.I(S::Bits(2.5f)); c.R(1);
  c.B(0x22); c.I(S::Bits(1.25f)); c.R(1);
  c.B(7); c.R(1); c.R(5);
  c.Ceder(); c.Fin();
  S::Instancia m;
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "aritmetica");
  const auto& h = m.HiloEn(0);
  Ver(h.r[0] == 1 && S::Signo(h.r[1]) == -2 && S::Signo(h.r[2]) == -3, "wrap y division con signo");
  Ver(h.r[3] == 0xF8000000 && h.r[4] == 3, "shift aritmetico y rotacion");
  Ver(h.r[6] == 0x01000000 && h.r[7] == 0x0000FFFF, "acarreo y prestamo cruzan bytes");
  Ver(S::Float(h.f[1]) == 3.75f && h.r[5] == 3, "inmediato float son bits; conversion trunca");
  ++grupos;
}

void EsperasRetornosYCierre() {
  Codigo c;
  c.Mov(10, 0); c.B(0x93); c.I(3); c.Mov(20, 0); c.Ceder(); c.Fin();
  S::Instancia m;
  std::vector<std::string> eventos;
  m.ganchos.inicio = [&](S::Instancia&, uint32_t e) { Ver(e == 0, "entrada gancho48"); eventos.push_back("inicio"); };
  m.ganchos.crear = [&](S::Instancia&, S::Hilo& h, const S::Hilo* p) {
    Ver(!p, "padre inicial nulo"); eventos.push_back("crear");
    h.previo = [&](S::Instancia&, S::Hilo&) { eventos.push_back("pre"); };
    h.posterior = [&](S::Instancia&, S::Hilo&) { eventos.push_back("post"); };
  };
  m.ganchos.terminar = [&](S::Instancia&, S::Hilo&) { eventos.push_back("terminar"); };
  m.ganchos.fin = [&](S::Instancia&) { eventos.push_back("fin"); };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0, "iniciar espera");
  Ver(m.Tick() && m.HiloEn(0).r[0] == 10 && m.HiloEn(0).espera == 2, "93 duerme n-1");
  m.Pausar(true);
  Ver(m.Tick() && m.HiloEn(0).espera == 2, "pausa conserva contador");
  m.Pausar(false);
  Ver(m.Tick() && m.HiloEn(0).espera == 1, "espera primer cuadro");
  Ver(m.Tick() && m.HiloEn(0).espera == 0 && m.HiloEn(0).r[0] == 10, "espera segundo cuadro");
  Ver(m.Tick() && m.HiloEn(0).r[0] == 20 && m.Viva(), "continuacion temporal exacta");
  Ver(m.Tick() && !m.Viva() && m.Estado() == 0, "ret4 termina tarea");
  Ver(eventos.size() >= 3 && eventos[0] == "inicio" && eventos[1] == "crear", "orden ganchos de inicio");
  Ver(std::vector<std::string>(eventos.end() - 4, eventos.end()) ==
      std::vector<std::string>({"pre", "terminar", "fin", "post"}), "54/4C antes de post en ret4");
  ++grupos;
}

void ReinicioPilaYDestino28() {
  Codigo c;
  const size_t label = c.Ref(0x90);
  c.Push(0x11223344); c.B(0x94);
  const uint32_t destino = c.Pos(); c.Mov(77, 0); c.Ceder(); c.Fin();
  c.Fijar(label, destino, 0xF0000000);
  S::Instancia m;
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "ret2 reinicio");
  auto& h = m.HiloEn(0);
  Ver(h.r[9] == m.Base() + destino && h.r[11] == h.r[15] + 0x1000, "ret2 limpia SP y usa etiqueta28bits");
  Ver(m.Tick() && h.r[0] == 77, "reanuda etiqueta");
  // 1482D0 (ret a destino explicito) suma r8; B2 original local omitia base.
  Codigo d; const size_t llamar = d.Ref(0x73); d.Mov(999, 0); d.Fin();
  const uint32_t sub = d.Pos(); const size_t volver = d.Ref(0x77);
  const uint32_t salida = d.Pos(); d.Mov(88, 0); d.Ceder(); d.Fin();
  d.Fijar(llamar, sub, 0xA0000000); d.Fijar(volver, salida, 0xB0000000);
  S::Instancia n;
  Ver(n.Cargar(d.Guion(), tabla) && n.Iniciar() == 0 && n.Tick(), "call/ret explicito IW");
  Ver(n.HiloEn(0).r[0] == 88 && n.HiloEn(0).r[11] == n.HiloEn(0).r[15] + 0x1000, "77 suma base y consume retorno");
  Codigo sp; sp.Push(0x230000); sp.Pop(11); sp.Ceder(); sp.Fin();
  S::Instancia alias;
  Ver(alias.Cargar(sp.Guion(), tabla) && alias.Iniciar() == 0 && alias.Tick(), "pop con destino SP");
  Ver(alias.HiloEn(0).r[11] == 0x230004, "pop escribe destino ANTES de incrementar SP");
  ++grupos;
}

void RanurasYDatoOriginal() {
  Codigo c;
  // Los15 hijos se insertan tras el padre; todos pueden ejecutar este tick.
  std::vector<size_t> refs;
  for (int i = 0; i < 15; ++i) { refs.push_back(c.Ref(0x98)); c.I(uint32_t(i + 1)); }
  c.Ceder(); c.Fin();
  const uint32_t hijo = c.Pos(); c.Mov(123, 2); c.Ceder(); c.Fin();
  for (auto r : refs) c.Fijar(r, hijo, 0xF0000000);
  S::Instancia m;
  std::array<const S::Hilo*, 16> direcciones{};
  m.ganchos.crear = [&](S::Instancia&, S::Hilo& h, const S::Hilo* p) {
    direcciones[size_t(h.ranura)] = &h;
    if (p) Ver(p == direcciones[size_t(p->ranura)] && p->r[8] != 0, "padre estable durante Crear");
  };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "crear15 hijos durante ejecutar");
  Ver(m.Vivos() == 16, "16 ranuras activas");
  for (size_t i = 0; i < 16; ++i) {
    auto& h = m.HiloEn(i);
    Ver(&h == direcciones[i], "referencia estable de hilo");
    Ver(h.r[0] == 0 && h.r[1] == 0, "dato NO va a r0/r1: memset128B original");
    if (i) Ver(h.dato == i && h.prioridad == 0x3000 + i && h.r[2] == 123, "dato a prioridad task+18; hijo mismo tick");
  }
  Ver(m.Tick() && !m.Viva(), "todos los hilos terminan");
  // Las ranuras liberadas forman un stack, no vivos=indice.
  Ver(m.Iniciar() == 15 && m.Tick(), "reutilizar ranura real libre");
  Ver(&m.HiloEn(15) == direcciones[15], "referencia estable tras reutilizacion");
  ++grupos;
}

void HijoDeMenorPrioridad() {
  Codigo c;
  const size_t r = c.Ref(0x98); c.I(uint32_t(-1)); c.Ceder(); c.Fin();
  const uint32_t hijo = c.Pos(); c.Mov(42, 2); c.Ceder(); c.Fin(); c.Fijar(r, hijo);
  S::Instancia m;
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "crear prioridad anterior");
  Ver(m.HiloEn(1).prioridad == 0x2FFF && m.HiloEn(1).r[2] == 0, "insertado antes del cursor espera tick siguiente");
  Ver(m.Tick() && m.HiloEn(1).r[2] == 42, "planificador conserva orden original");
  ++grupos;
}

void SustitucionGrupoYRet6() {
  Codigo c;
  const size_t spawn = c.Ref(0x98); c.I(1);
  const size_t reemplazar = c.Ref(0x99); c.I(2);
  const uint32_t hijo = c.Pos(); c.Mov(999, 2); c.Ceder(); c.Fin();
  const uint32_t nuevo = c.Pos(); c.Mov(55, 2); c.Ceder(); c.B(0x97);
  c.Fijar(spawn, hijo); c.Fijar(reemplazar, nuevo, 0xF0000000);
  S::Instancia m;
  int fines = 0, muertos = 0, posteriores = 0, reemplazos = 0;
  m.ganchos.fin = [&](S::Instancia&) { ++fines; };
  m.ganchos.terminar = [&](S::Instancia&, S::Hilo&) { ++muertos; };
  m.ganchos.crear = [&](S::Instancia&, S::Hilo& h, const S::Hilo* p) {
    h.posterior = [&](S::Instancia&, S::Hilo&) { ++posteriores; };
    if (p && h.dato == 2) {
      ++reemplazos;
      // Este caso libera dos ranuras: se reutiliza la del hermano, y el
      // banco del padre no cambia. El caso alias lo cubre el ejecutor retail.
      Ver(p != &h && p->r[0] == h.r[9] && p->r[1] == 2,
          "gancho50 conserva banco padre cuando se reutiliza otra ranura");
    }
  };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "ret5 grupo");
  Ver(m.Vivos() == 1 && muertos == 2 && fines == 0 && reemplazos == 1, "5 mata grupo y suprime4C transitorio");
  Ver(m.HiloEn(1).r[2] == 55 && posteriores == 1, "nuevo mismo tick; ret5 no llama post");
  Ver(m.Tick() && !m.Viva() && fines == 1 && muertos == 3 && posteriores == 2, "ret6 cancela grupo y llama post");
  ++grupos;
}

void GrupoCompletoYDatoEnPila() {
  Codigo c;
  c.Push(7); c.Push(0x1234);  // [SP+4] es el dato, NO [SP].
  const size_t spawn = c.Ref(0x91); c.Ceder(); c.B(0x97);
  const uint32_t hijo = c.Pos(); c.Ceder(); c.Ceder(); c.Fin(); c.Fijar(spawn, hijo);
  S::Instancia m; int fines = 0;
  m.ganchos.fin = [&](S::Instancia&) { ++fines; };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "91 con dato en SP+4");
  Ver(m.Vivos() == 2 && m.HiloEn(1).dato == 7 && m.HiloEn(1).prioridad == 0x3007,
      "91 usa segundo argumento de pila como prioridad");
  Ver(m.Tick() && m.Vivos() == 0 && fines == 1, "97/ret6 mata los dos hilos, no solo el actual");
  // Ret4 debe conservar hermanos y no llamar fin prematuramente.
  Codigo d; const size_t s = d.Ref(0x98); d.I(0); d.Fin();
  const uint32_t h = d.Pos(); d.Ceder(); d.Fin(); d.Fijar(s, h);
  S::Instancia n; fines = 0; n.ganchos.fin = [&](S::Instancia&) { ++fines; };
  Ver(n.Cargar(d.Guion(), tabla) && n.Iniciar() == 0 && n.Tick(), "ret4 con hermano activo");
  Ver(n.Vivos() == 1 && n.HiloEn(1).activo && fines == 0, "95/ret4 termina solo actual");
  Ver(n.Tick() && !n.Viva() && fines == 1, "4C solo al terminar ultimo hermano");
  // 96 igual a99 pero lee dato de [SP+4].
  Codigo e; e.Push(11); e.Push(22); const size_t r = e.Ref(0x96);
  const uint32_t destino = e.Pos(); e.Ceder(); e.Fin(); e.Fijar(r, destino);
  S::Instancia reemplazo;
  Ver(reemplazo.Cargar(e.Guion(), tabla) && reemplazo.Iniciar() == 0 && reemplazo.Tick(), "96 sustituye grupo");
  Ver(reemplazo.HiloEn(0).dato == 11 && reemplazo.HiloEn(0).r[0] == 0, "96 traslada dato a prioridad y reinicia registros");
  ++grupos;
}

void ContextosSondeosYRet8() {
  Codigo c; c.Native(0x42); c.Mov(33, 0); c.Ceder(); c.Fin();
  S::Instancia m;
  int llamadas = 0, polls = 0, post = 0;
  m.Registrar(tabla, 0x42, [&](S::Instancia&, S::Hilo& h) {
    ++llamadas;
    h.sondeo = [&](S::Instancia&, S::Hilo& t) {
      ++polls;
      if (polls < 2) return 3u;
      t.sondeo = {}; return 1u;
    };
    return 3u;
  });
  m.ganchos.crear = [&](S::Instancia&, S::Hilo& h, const S::Hilo*) {
    h.posterior = [&](S::Instancia&, S::Hilo&) { ++post; };
  };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && m.Tick(), "builtin instala sondeo");
  Ver(m.Tick() && m.HiloEn(0).r[0] == 0 && polls == 1, "sondeo3 conserva espera");
  Ver(m.Tick() && m.HiloEn(0).r[0] == 33 && llamadas == 1 && polls == 2 && post == 3, "sondeo1 continua sin relanzar builtin");
  S::Instancia otra;
  otra.Registrar(tabla + 1, 0x42, [](S::Instancia&, S::Hilo&) { return 1u; });
  Ver(otra.Cargar(c.Guion(), tabla) && otra.Iniciar() == 0 && !otra.Tick(), "builtin requiere tabla correcta");
  Ver(otra.Bloqueada() && otra.Error().find("builtin desconocido") != std::string::npos && otra.HiloEn(0).r[0] == 0,
      "desconocido bloquea antes de continuar");
  Codigo d; d.Mov(9, 0); d.Native(7); d.Mov(99, 0); d.Fin();
  S::Instancia ocho;
  int veces = 0, post8 = 0;
  ocho.Registrar(tabla, 7, [&](S::Instancia&, S::Hilo&) { return ++veces == 1 ? 8u : 1u; });
  ocho.ganchos.crear = [&](S::Instancia&, S::Hilo& h, const S::Hilo*) {
    h.posterior = [&](S::Instancia&, S::Hilo&) { ++post8; };
  };
  Ver(ocho.Cargar(d.Guion(), tabla) && ocho.Iniciar() == 0 && ocho.Tick(), "ret8 sale");
  Ver(ocho.HiloEn(0).r[0] == 9 && post8 == 0 && ocho.Viva(), "8 conserva checkpoint nativo y omite post");
  Ver(ocho.Tick() && ocho.HiloEn(0).r[0] == 99 && veces == 1 && !ocho.Viva(), "8 reanuda checkpoint despues del nativo");
  ++grupos;
}

void CapsulasRealesYBoton() {
  Codigo c;
  c.Push(0); c.Native(0x116); c.Pop(1);
  c.Native(0x118); c.Native(0x119); c.Push(7); c.Native(0x75); c.Pop(1); c.Ceder(); c.Fin();
  S::Instancia m; S::Host host;
  int transicion = 0, listo = 0; uint32_t liberado = 0;
  host.botones = [] { return uint16_t(8); };
  host.transicion = [&](int t) { transicion = t; };
  host.capsulas_listas = [&] { ++listo; };
  host.parar_secuencia_iop = [&](uint32_t id) { liberado = id; };
  m.Conectar(host); m.InstalarComunesEU();
  Ver(m.Cargar(c.Guion(), S::kCapsulasEU) && m.Iniciar() == 0 && m.Tick(), "builtins de capsulas conectados al host");
  Ver(m.HiloEn(0).r[0] == 1 && transicion == 10 && listo == 1 && liberado == 7, "efectos observables de116/118/119/75");
  S::Instancia sinhost; sinhost.InstalarComunesEU();
  Ver(sinhost.Cargar(c.Guion(), S::kCapsulasEU) && sinhost.Iniciar() == 0 && !sinhost.Tick(), "host faltante bloquea");
  Ver(sinhost.Error().find("Host.botones") != std::string::npos, "diagnostico host ausente");
  Codigo d; d.Native(1); d.Mov(80, 0); d.Ceder(); d.Fin();
  S::Instancia boton; uint16_t pulsado = 0;
  host.botones = [&] { return pulsado; }; boton.Conectar(host);
  boton.Registrar(tabla, 1, [](S::Instancia& i, S::Hilo& h) { return i.EsperarBoton8(h); });
  Ver(boton.Cargar(d.Guion(), tabla) && boton.Iniciar() == 0 && boton.Tick() && boton.Tick(), "espera de boton persistente");
  Ver(boton.HiloEn(0).r[0] == 0 && bool(boton.HiloEn(0).sondeo), "sin boton no avanza");
  pulsado = 8;
  Ver(boton.Tick() && boton.HiloEn(0).r[0] == 80 && !boton.HiloEn(0).sondeo, "boton real desbloquea continuacion");
  ++grupos;
}

void AnimacionConHost() {
  Codigo c;
  c.Push(2); c.Push(0x123); c.Push(2); c.Native(0x178);
  c.Pop(0); c.Pop(0); c.Pop(0); c.Mov(99, 3); c.Ceder(); c.Fin();
  S::Instancia m; S::Host host; bool pausa = false; int avances = 0, entradas = 0;
  float valor = -1;
  host.paso = [] { return 1.f; }; host.pausado = [&] { return pausa; };
  host.entrada_b = [&](int slot, uint32_t entrada) { Ver(slot == 2 && entrada == 0x123, "entrada de animacion original"); ++entradas; };
  host.valor_b_relativo = [&](int slot, float v) { Ver(slot == 2, "slot valor relativo"); valor = v; };
  host.avanzar_b = [&](int slot, bool loop) { Ver(slot == 2 && loop, "avance usa slot/bucle reales"); ++avances; return avances % 2 == 0; };
  m.Conectar(host); m.InstalarComunesEU();
  Ver(m.Cargar(c.Guion(), S::kCombateEU) && m.Iniciar() == 0 && m.Tick(), "178 inicia anim y sondeo");
  Ver(entradas == 1 && avances == 1 && valor == 0 && m.HiloEn(0).r[3] == 0, "178 aplica entrada y cuadro inicial");
  pausa = true;
  Ver(m.Tick() && avances == 1, "pausa no avanza animacion");
  pausa = false;
  Ver(m.Tick() && avances == 2 && m.HiloEn(0).r[3] == 0, "tiempo1 sigue esperando");
  Ver(m.Tick() && avances == 2 && m.HiloEn(0).r[3] == 99 && !m.HiloEn(0).sondeo, "tiempo2 termina antes de avanzar otro cuadro");
  // 177 cuenta vueltas verdaderas del callback, no cuadros predeterminados.
  Codigo d; d.Push(2); d.Push(0x123); d.Push(2); d.Native(0x177); d.Mov(7, 3); d.Ceder(); d.Fin();
  S::Instancia vueltas; avances = 0; vueltas.Conectar(host); vueltas.InstalarComunesEU();
  Ver(vueltas.Cargar(d.Guion(), S::kCombateEU) && vueltas.Iniciar() == 0 && vueltas.Tick(), "177 arranque");
  Ver(vueltas.Tick() && vueltas.Tick() && vueltas.HiloEn(0).r[3] == 0, "dos primeras vueltas aun incompletas");
  Ver(vueltas.Tick() && avances == 4 && vueltas.HiloEn(0).r[3] == 7, "177 completa tras dos vueltas del host");
  // MismoID en otra tabla NO obtiene automaticamente nativo de combate.
  S::Instancia distinto; distinto.Conectar(host); distinto.InstalarComunesEU();
  Ver(distinto.Cargar(d.Guion(), S::kCapsulasEU) && distinto.Iniciar() == 0 && !distinto.Tick(), "177 no se confunde con capsulas");
  ++grupos;
}

void FallosYTraza() {
  Codigo c; for (int i = 0; i < 16; ++i) { const size_t p = c.Ref(0x98); c.I(0); c.Fijar(p, 0); } c.Fin();
  S::Instancia m;
  int errores = 0;
  m.traza = [&](const S::Traza& t) { if (t.fase == S::Traza::Fase::Error) ++errores; };
  Ver(m.Cargar(c.Guion(), tabla) && m.Iniciar() == 0 && !m.Tick(), "17a ranura rechazada");
  Ver(m.Vivos() == 16 && m.Bloqueada() && errores == 1, "overflow deja error explicito y ranuras intactas");
  Ver(!m.Tick() && errores == 1, "bloqueo no continua instrucciones");
  Codigo d; d.B(0xFF); S::Instancia op;
  Ver(op.Cargar(d.Guion(), tabla) && op.Iniciar() == 0 && !op.Tick() && op.Error().find("opcode") != std::string::npos,
      "opcode desconocido nunca devuelve exito");
  S::Configuracion cfg; cfg.limite_pasos = 20; S::Instancia bucle(cfg);
  Codigo loop; loop.Ref(0x70);
  Ver(bucle.Cargar(loop.Guion(), tabla) && bucle.Iniciar() == 0 && !bucle.Tick(), "loop sin yield bloquea");
  Codigo e; e.B(1); e.B(0x48); e.R(0); e.Fin(); S::Instancia tr;
  std::vector<S::Traza> trace; tr.traza = [&](const S::Traza& t) { trace.push_back(t); };
  Ver(tr.Cargar(e.Guion(), tabla) && tr.Iniciar() == 0 && tr.Tick(), "operando lee memoria de codigo por r8");
  Ver(trace.size() >= 3 && trace[0].fase == S::Traza::Fase::Crear && trace[1].codigo == 1 &&
      trace[1].pc == tr.Base() && trace[1].tabla == tabla && trace[1].generacion != 0, "traza con PC/contexto/ranura/generacion");
  Codigo ieee; ieee.B(3); ieee.I(S::Bits(std::numeric_limits<float>::infinity())); ieee.R(0);
  ieee.B(7); ieee.R(0); ieee.R(0); ieee.Fin(); S::Instancia limite;
  Ver(limite.Cargar(ieee.Guion(), tabla) && limite.Iniciar() == 0 && !limite.Tick(), "conversion no finita bloquea explicitamente");
  Ver(limite.Error().find("R5900") != std::string::npos, "no inventar saturacion PowerPC");
  Codigo falta; falta.B(1); falta.B(0x41); falta.R(0); falta.Fin(); S::Instancia unmapped;
  Ver(unmapped.Cargar(falta.Guion(), tabla) && unmapped.Iniciar() == 0 && !unmapped.Tick(), "memoria desconocida no devuelve cero");
  ++grupos;
}

void ActivoReal(const char* ruta) {
  std::ifstream f(ruta, std::ios::binary);
  Ver(bool(f), "abrir SPX real");
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const bool cmn993 = bytes.size() == 3088;
  Ver(cmn993 || bytes.size() == 2148, "fixture real debe ser CMN993/994");
  S::Instancia m; std::vector<uint32_t> ops;
  m.InstalarComunesEU();
  m.traza = [&](const S::Traza& t) { if (t.fase == S::Traza::Fase::Opcode) ops.push_back(t.codigo); };
  Ver(m.Cargar(std::move(bytes), S::kCapsulasEU) && m.Iniciar() == 0 && !m.Tick(), "SPX real ejecuta hasta builtin sin host");
  Ver(ops == (cmn993 ? std::vector<uint32_t>({8, 8, 8, 1, 0x80}) : std::vector<uint32_t>({8, 1, 0x80})),
      "SPX real decodifica instrucciones y operandos");
  Ver(m.Error().find(cmn993 ? "0x00000045" : "0x00000071") != std::string::npos &&
      m.HiloEn(0).r[2] == (cmn993 ? 0x45u : 0x71u), "SPX real bloquea builtin faltante; no fabrica cierre");
  Ver(m.Arg(m.HiloEn(0), 0) == 0 && (!cmn993 || m.Arg(m.HiloEn(0), 2) == 0), "argumentos reales en pila");
  ++grupos;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    MemoriaOperandos(); AritmeticaYFloat(); EsperasRetornosYCierre(); ReinicioPilaYDestino28();
    RanurasYDatoOriginal(); HijoDeMenorPrioridad(); SustitucionGrupoYRet6(); GrupoCompletoYDatoEnPila(); ContextosSondeosYRet8();
    CapsulasRealesYBoton(); AnimacionConHost(); FallosYTraza();
    if (argc == 2) ActivoReal(argv[1]);
    std::cout << "PASS " << grupos << " grupos, " << comprobaciones << " comprobaciones\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL tras " << comprobaciones << " comprobaciones: " << e.what() << '\n';
    return 1;
  }
}
