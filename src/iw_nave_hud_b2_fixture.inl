// Archivo de DATOS, solo arnes MISIONES5. No escribe memoria guest ni altera el flujo.
namespace fixture_b2 {
struct Datos {
  uint64_t secuencia = 0; uint32_t n_original = 0;
  int tipo = 1;
  MuestraMarcadorB2 m;
  original2p::Muestra dos;
};
bool Leer(Datos& d) {
  const char* path = std::getenv("DBZ3_TEST_GP2_HUD_FIXTURE_ARCHIVO");
  if (!path || !((path[0]=='D'||path[0]=='d') && path[1]==':')) return false;
  std::ifstream f{std::filesystem::path(path)};
  std::string cab; int version = 0;
  if (!(f>>cab>>version) || cab!="GP2HUD" || version!=1) return false;
  std::string key;
  while (f>>key) {
    if (key=="seq") f>>d.secuencia;
    else if (key=="tipo") f>>d.tipo;
    else if (key=="original") f>>d.n_original;
    else if (key=="planta") f>>d.m.planta;
    else if (key=="cuadros") f>>d.m.cuadros;
    else if (key=="encuentro") f>>d.m.encuentro;
    else if (key=="limite") f>>d.m.limite_cuadros;
    else if (key=="contadores") f>>d.m.vencidos>>d.m.golpes>>d.m.ondas;
    else if (key=="opciones") f>>d.m.prueba.caras>>d.m.prueba.medio_texel>>d.m.prueba.transitorios;
    else if (key=="pausa") f>>d.m.pausa;
    else if (key=="paso") f>>d.m.paso_hud_original;
    else if (key=="actor") {
      int i; if (!(f>>i) || i<0 || i>1) return false;
      f>>d.m.personaje_b3[i]>>d.m.forma_b3[i]>>d.m.personaje_b2[i]>>d.m.cara_b2[i]>>d.m.vida[i]>>d.m.ki[i]>>d.m.ki_max[i];
    } else if (key=="dano") {
      int i; if (!(f>>i) || i<0 || i>1) return false;
      auto& e=d.m.dano[i]; f>>e.valido>>e.secuencia>>e.inicio>>e.fin_recepcion;
    } else if (key=="modo2p") {
      f>>d.dos.modo>>d.dos.lado_activo>>d.dos.tick_modo;
    } else if (key=="lado2p") {
      int i; if (!(f>>i) || i<0 || i>1) return false;
      auto& l=d.dos.lado[i];
      f>>l.personaje_b2>>l.cara>>l.coronas>>l.vida_frente>>l.vida_estela>>l.porcentaje_objetivo>>l.porcentaje_presentado
       >>l.burbuja_nodo>>l.estado_popup>>l.flags_vista>>l.tiempo_ko>>l.tiempo_critico;
    } else if (key=="tabla11") f>>d.dos.marco_modo11[0]>>d.dos.marco_modo11[1]>>d.dos.vida_modo11[0]>>d.dos.vida_modo11[1];
    else if (key=="tabla12") f>>d.dos.ancla_modo12[0]>>d.dos.ancla_modo12[1]>>d.dos.alineacion_modo12[0]>>d.dos.alineacion_modo12[1];
    else return false;
    if (!f) return false;
  }
  if (d.secuencia==0 || (d.tipo!=1 && d.tipo!=2)) return false;
  if (d.tipo==2) {
    if (d.dos.modo<10 || d.dos.modo>13) return false;
    d.dos.productores_autoritativos=true; d.m.dos_jugadores=true;
    d.m.planta=d.dos.modo-10; d.m.prueba.cargar2p=true;
  }
  return true;
}
void Dibujar() {
  Datos d;
  static bool error_mostrado = false;
  if (!Leer(d)) {
    SoltarMarcadorB2();
    if (!error_mostrado) REXLOG_WARN("dbz3 nave fixture: archivo ausente/invalido; B3 conservado");
    error_mostrado=true; return;
  }
  error_mostrado=false;
  static noche::ContextoVisual contexto;
  d.m.contexto=&contexto;
  if (d.tipo==2) d.m.muestra_2p=&d.dos;
  const bool ok=PintarMarcadorB2(ImGui::GetForegroundDrawList(),d.m);
  if (ok) {
    auto& r=hud_b2_detalle::R();
    if (!r.propietario) { dbz3::combate::OcultarHud(true); r.propietario=true; }
  } else SoltarMarcadorB2();
  static uint64_t ultima=0; static bool ultimo_ok=false;
  if (ultima!=d.secuencia || ultimo_ok!=ok) {
    ultima=d.secuencia; ultimo_ok=ok;
    REXLOG_INFO("dbz3 nave FIXTURE: seq={} tipo={} modo={} nOriginal={} tiempo={} vida={}/{} forma={}/{} ok={} RAM_NO_ESCRITA=1",
                d.secuencia,d.tipo,d.tipo==2?d.dos.modo:d.m.planta+6,d.n_original,d.m.cuadros,
                d.m.vida[0],d.m.vida[1],d.m.forma_b3[0],d.m.forma_b3[1],ok);
  }
}
}  // namespace fixture_b2
