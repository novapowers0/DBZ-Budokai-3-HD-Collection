"""Extensión focal: ejecuta fragmentos C++ literales y reutiliza el ELF de NB2C."""
PRELUDIO = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#define REXLOG_INFO(...) ((void)0)
struct ImDrawList {};
namespace S { struct Area {}; }
constexpr uint16_t kA=1, kX=2, kY=4, kLeft=8, kRight=16;
int stock, maximo, id_registro, pj_global, grupo_observado, desbloqueos;
uint64_t roster, mascara_observada;
std::vector<uint8_t> ska;
bool prepara=false;
std::vector<int> g_mensajes;
int g_premio_id=0;
float g_premio_t=-10;
float Lento() { return 1; }
bool IrisActivo(float) { return false; }
int PersonajeDe(int n) { assert(n == 1); return pj_global; }
namespace b2 { uint32_t B2Rng(int n) { assert(n == 32); return 0; } }
namespace dbz3::pasarela::nave {
struct Premio { int id; std::string nombre; };
int Tienes(int id) { assert(id == id_registro); return stock; }
int Maximo(int id) { assert(id == id_registro); return maximo; }
void Dar(const Premio& p, int n) { assert(p.id == id_registro); stock += n; }
const std::vector<uint8_t>& TablaCapsulas() { return ska; }
bool PersAbierto(int id) { return roster & (uint64_t(1) << id); }
void DesbloquearEvento(int) { ++desbloqueos; }
struct Anim { float dur_tarea; };
std::vector<Anim> g_animv;
int g_ficha_cara;
float g_ficha_giro_c, g_ficha_sale_c, g_cartel = 101;
std::vector<int> dibujados;
void Cargar() {}
int LienzoDe(const S::Area&) { return 0; }
void DibVentanaPremio(ImDrawList*, int, int id, const char*, float, bool) { dibujados.push_back(id); }
'''

MAIN = r'''
int main(int argc, char** v) {
  if (argc != 10) return 5;
  const int op=std::stoi(v[1]), grupo=std::stoi(v[2]);
  id_registro=std::stoi(v[3]); stock=std::stoi(v[4]); maximo=std::stoi(v[5]);
  roster=std::stoull(v[6]); pj_global=std::stoi(v[7]);
  const bool confirmar=std::stoi(v[8]) != 0;
  const bool candidato=std::stoi(v[9]) != 0;
  ska.resize(0x20 + 0x208 * 32);
  std::memcpy(ska.data(), "#SKA", 4);
  for (size_t at=0x20; at<ska.size(); at+=32) ska[at+0x14]=254;
  size_t at=0x20+32*size_t(id_registro);
  if (candidato) { ska[at+0x14]=uint8_t(grupo); ska[at+0xD]=uint8_t(maximo); ska[at]=1; }
  TomarOrden({8, op, grupo, 1, 0, 0});
  const int inscrito=g_premio_id, stock_abierto=stock;
  const S::Area a; ImDrawList dibujo; auto* dl=&dibujo;
  const auto cuadro = [&](float t, uint16_t pressed) { Avanzar(dl, a, t, pressed, false, false); };
  cuadro(0, 0);
  const int stock_espera=stock;
  if (inscrito) {
    cuadro(2, confirmar ? kA : 0); // tras el cartel
    cuadro(2.15f, 0);             // antes del fin de retirada (20 cuadros del fixture)
    assert(stock == stock_espera);
    cuadro(2.5f, 0); cuadro(3, 0); // después de la retirada y un cuadro extra
  }
  const auto& vistas=dbz3::pasarela::nave::dibujados;
  for (int id : vistas) assert(id == inscrito && id != 0);
  std::cout << grupo_observado << ' ' << mascara_observada << ' ' << inscrito << ' '
            << stock_abierto << ' ' << stock_espera << ' ' << stock << ' '
            << (vistas.empty() ? 0 : vistas[0]) << ' ' << !g_premio_id << ' ' << desbloqueos;
}
'''


def observar(texto, ancla, nueva):
    if texto.count(ancla) != 1:
        raise ValueError(f'ancla C++ no única: {ancla}')
    return texto.replace(ancla, nueva, 1)


def bloque(texto, inicio, fin):
    if texto.count(inicio) != 1:
        raise ValueError(f'ancla de bloque C++ no única: {inicio}')
    pos = texto.index(inicio)
    return texto[pos:texto.index(fin, pos)]


def construir(b2, nave, fragment):
    premio = bloque(b2, '    if (o[1] == 70 || o[1] == 8)', '\n    // EU 0x292EA0')
    avance = bloque(b2, '  if (!prepara && g_mensajes.empty() && g_premio_id > 0 && !IrisActivo(t))',
                     '\n  if (!prepara && g_mensajes.empty() && g_premio_id == 0')
    sorteo = fragment(nave, 'int CapsulaAlAzar(int grupo, int personaje_b2) {')
    # Solo observa la máscara efectiva; no cambia candidatos, RNG ni retorno.
    sorteo = observar(sorteo, '  std::vector<int> con_hueco, con_mascara;',
                      '  grupo_observado = grupo; mascara_observada = tengo;\n'
                      '  std::vector<int> con_hueco, con_mascara;')
    cerrado = bloque(nave, 'bool PremioCerrado(float c) {', '\nvoid CerrarFicha(float c) {')
    return '\n'.join((PRELUDIO, sorteo, *(fragment(nave, s) for s in (
        'void FichaGirar(float c) {', 'void FichaReset() {', 'float FinPremio() {',
        'void CerrarFicha(float c) {', 'void GanarCapsula(int id_b2) {')),
        cerrado, fragment(nave, 'bool VentanaPremio(ImDrawList* dl,'),
        '} // namespace', 'float TomarOrden(const std::array<int,6>& o) { float espera=0;', premio, 'return espera; }',
        'void Avanzar(ImDrawList* dl, const S::Area& a, float t, uint16_t pressed, bool auto_on, bool test_diez) {',
        avance, '}', MAIN))


def medir_premio(original, base, row, ejecutar):
    """Refuerza la salida inyectada de NB2C con un sorteo real del EE y la misma SKA de banco en C++."""
    inp, a = row['entrada'], row['valor']
    # NB2C fija roster=0x400000001, Goku=0, grupo=35. Para el caso vacío
    # se conserva el roster y se usa una fila de OTRO grupo, sin candidatos.
    roster, grupo, id_registro = 0x400000001, 35, 17
    op, elegido, stock, maximo = (inp[k] for k in ('op', 'elegido', 'stock', 'maximo'))
    # Para el premio vacío, el candidato pertenece a un grupo distinto del pedido.
    registros = [dict(id=id_registro, mascara=1, maximo=maximo)] if elegido else []
    _, id_pal, traza = original.capsules(base, grupo, a['mascara'], 1, registros, {id_registro: stock})
    if id_pal != elegido:
        raise ValueError(f'la SKA de banco no reproduce el sorteo inyectado: {id_pal} != {elegido}')
    argumentos = (op, grupo, id_registro, stock, maximo, roster, 0, 1, int(bool(elegido)))
    salida = ejecutar(argumentos)
    g, m, id_cpp, inicio, espera, fin, ventana, termina, desbloqueos = salida
    valores = dict(grupo=g, mascara=m, stock=[inicio, espera, fin] if id_cpp else [inicio],
                   stock_final=fin, ventana=[ventana] if ventana else [], termina=bool(termina))
    sin_confirmar = ejecutar((*argumentos[:7], 0, argumentos[-1])) if elegido else None
    if sin_confirmar and (sin_confirmar[5], sin_confirmar[7], sin_confirmar[8]) != (stock, 0, 0):
        raise ValueError('el control sin confirmación concedió o liberó un premio')
    if desbloqueos != int(bool(id_cpp)):
        raise ValueError('desbloqueo repetido o ausente tras la transición')
    return dict(entrada=inp, valor=valores, traza=dict(sorteo_pal=id_pal, sorteo_port=id_cpp,
                grupo_fixture=grupo, mascara_roster=roster, registros_ska=registros,
                control_sin_confirmar=sin_confirmar, ee_sorteo=traza))
