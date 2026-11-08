// PAL: 1C7A70 configura RGBA (argumentos en orden inverso); 1C8450
// despacha el evento37 a1BD8E0. 1BAAF0 dibuja ANTES de avanzar el reloj.
// El estado es propio del puente: no modifica el fundido del anfitrión B3.
struct B2FundidoEstado {
  std::array<float,4> proximo{0,0,0,1},color{0,0,0,1};
  float tiempo=0,inicio=0,fin=0;
  bool existe=false,entrando=false;
  constexpr bool Crear(bool entrada,float desde,float hasta) {
    if(!entrada&&!existe)return true; //1BD8E0: cierre sin tarea no crea nada.
    if(!(hasta>desde))return false; // No adivinar el caso original de división por cero.
    color=proximo;tiempo=0;inicio=desde;fin=hasta;
    existe=true;entrando=entrada;return true;
  }
  constexpr float Alfa() const {
    if(!existe)return 0;
    const float t=tiempo-inicio,d=fin-inicio;
    return color[3]*(t<0?(entrando?0.f:1.f):(entrando?t/d:(d-t)/d));
  }
  constexpr void Avanzar(float delta) {
    if(!existe||delta<=0)return;
    tiempo+=delta;
    if(tiempo>=fin){if(entrando)tiempo=fin;else existe=false;}
  }
};
static_assert([]{B2FundidoEstado s;return s.Crear(false,0,10)&&!s.existe;}());
static_assert([]{B2FundidoEstado s;s.Crear(true,2,6);s.Avanzar(2);
  if(s.Alfa()!=0)return false;s.Avanzar(2);return s.Alfa()==.5f;}());
static_assert([]{B2FundidoEstado s;s.Crear(true,0,4);s.Avanzar(20);
  return s.existe&&s.tiempo==4&&s.Alfa()==1;}());
static_assert([]{B2FundidoEstado s;s.Crear(true,0,4);s.Crear(false,2,6);
  if(s.Alfa()!=1)return false;s.Avanzar(4);if(s.Alfa()!=.5f)return false;
  s.Avanzar(2);return !s.existe;}());
static_assert([]{B2FundidoEstado s;s.Crear(true,0,4);s.Avanzar(0);
  s.proximo={1,1,1,.5f};return s.tiempo==0&&s.color[3]==1&&!s.Crear(true,2,2);}());

struct B2FundidoHD {
  B2FundidoEstado estado;
  std::mutex mutex;
  std::array<float,4> dibujado{};
  uint32_t ultimo=~0u,dibujos=0;
  bool Color(const b2::Orden& o) {
    std::array<float,4> c{};
    for(unsigned i=0;i<4;++i){const uint32_t bits=o.argumentos[3-i];
      std::memcpy(&c[i],&bits,4);if(!std::isfinite(c[i]))return false;}
    estado.proximo=c;return true;
  }
  bool Crear(const b2::Orden& o) {
    const float desde=float(int32_t(o.argumentos[1])),hasta=float(int32_t(o.argumentos[0]));
    const bool ok=estado.Crear(o.id==0x61,desde,hasta);
    REXLOG_INFO("dbz3 B2 fundido: id={:X} inicio={} fin={} creado={} rgba={}/{}/{}/{}",
      o.id,desde,hasta,ok&&estado.existe,estado.color[0],estado.color[1],estado.color[2],estado.color[3]);
    return ok;
  }
  void Dibujar(uint32_t cuadro,float delta) {
    if(cuadro==ultimo)return;ultimo=cuadro;
    std::lock_guard<std::mutex> l(mutex);
    dibujado=estado.color;dibujado[3]=estado.Alfa();
    if(estado.existe)++dibujos;
    estado.Avanzar(delta);
  }
  void Pintar(ImGuiIO& io) {
    std::array<float,4> c;{std::lock_guard<std::mutex> l(mutex);c=dibujado;}
    if(c[3]<=0)return;
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,
      ImGui::ColorConvertFloat4ToU32(ImVec4(c[0],c[1],c[2],c[3])));
  }
  void Limpiar() {
    std::lock_guard<std::mutex> l(mutex);estado=B2FundidoEstado{};
    dibujado={};ultimo=~0u;dibujos=0;
  }
};
