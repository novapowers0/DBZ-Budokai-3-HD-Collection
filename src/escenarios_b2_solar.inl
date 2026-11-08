// PAL1B78A0/1AF5D0: tarea propia de prioridad90000000, no el flare manual IW.
// 1AC3E0 proyecta incluso durante la transición; 1ABCD0 solo pinta fuera de ella.
struct B2SolarEstado {
  std::array<float,4> pos{},pantalla{};
  float profundidad=0,alfa=0,coseno=0;
  constexpr void Intensidad(float ancho,float alto) {
    if(coseno<0)alfa-=.15f;
    alfa+=(pantalla[0]>=0&&pantalla[0]<=ancho&&pantalla[1]>=0&&pantalla[1]<=alto)?.15f:-.15f;
    alfa=std::clamp(alfa,0.f,1.f);
  }
  bool Proyectar(const std::array<float,16>& m,float foco,float ancho=512,float alto=512) {
    std::array<float,3> v{},ojo{},frente{};
    for(unsigned j=0;j<3;++j) {
      v[j]=m[j]*pos[0]+m[4+j]*pos[1]+m[8+j]*pos[2]+m[12+j];
      ojo[j]=-(m[4*j]*m[12]+m[4*j+1]*m[13]+m[4*j+2]*m[14]);
      frente[j]=m[4*j+2];
    }
    const float z=v[2]!=0?v[2]:1;
    pantalla={ancho*.5f+v[0]*.8f*foco/z,alto*.5f+v[1]*(16.f/15.f)*foco/z,v[2],1};
    float norma=0,dot=0;
    // El VADD de1AC4DC suma el origen de la inversa; no sustituir por resta.
    for(unsigned j=0;j<3;++j){const float x=pos[j]+ojo[j];norma+=x*x;dot+=frente[j]*x;}
    coseno=norma>0?dot/std::sqrt(norma):0;
    const float d=std::fabs(m[8]*pos[0]+m[9]*pos[1]+m[10]*pos[2]);
    //1335A0: profundidad GS, distinta del z-buffer D3D; no se usa como tamaño.
    constexpr float a=16770000.f*.1f/(1.f-.1f/65535.f),b=1.f-a/65535.f;
    profundidad=d>0?b+a/d:16770000.f;
    return std::isfinite(pantalla[0])&&std::isfinite(pantalla[1])&&std::isfinite(coseno);
  }
};
static_assert([]{B2SolarEstado s;s.coseno=1;s.pantalla={20,20,1,1};
  for(int i=0;i<8;++i)s.Intensidad(512,512);return s.alfa==1;}());
static_assert([]{B2SolarEstado s;s.alfa=.75f;s.coseno=-1;s.pantalla={20,20,1,1};
  s.Intensidad(512,512);return s.alfa==.75f;}());
static_assert([]{B2SolarEstado s;s.alfa=.2f;s.coseno=-1;s.pantalla={-1,20,1,1};
  s.Intensidad(512,512);return s.alfa==0;}());

// RGBAQ del paquete PAL17688: VFT0I antes de escribir el canal de 8 bits,
// con128 como unidad GS. No redondear directamente el float original a255.
constexpr float B2CanalSolarGs(float x) {
  return float(int(std::clamp(x*128.f,0.f,255.f)))/128.f;
}
static_assert(B2CanalSolarGs(.2f)==25.f/128.f&&B2CanalSolarGs(.14f)==17.f/128.f);

struct B2SolarHD {
  struct Tarea {uint32_t p=0,pasos=0,dibujos=0,ultimo_dibujo=~0u;B2SolarEstado estado;};
  struct Quad {std::array<ImVec2,4> p,uv;ImU32 color=0;unsigned tex=0;};
  std::vector<Tarea> tareas;
  uint32_t gestor=0,zdd=0,nombre=0;
  int alta=-1,grupo=-1;
  std::array<float,20> colores{};
  std::array<float,5> escalas{};
  std::array<ImVec2,4> uv{};
  std::mutex mutex;
  std::vector<Quad> quads;
  std::vector<Quad> pendientes;
  uint32_t ultimo_publicado=~0u;
  std::array<pasarela::Imagen,2> imagenes;
  std::array<ps2::Tex,2> texturas;
  uint64_t generacion=0,subida=~uint64_t(0),publicado_ms=0;
  static constexpr uint32_t marca=0x4232534fu;
#include "escenarios_b2_solar_z.inl"
#include "escenarios_b2_solar_foto.inl"
  void CamaraPrueba(uint8_t* base) const {
    // Comparación aislada: repetir una vista PAL observada en el render real.
    // No cambia intensidad, relojes ni eventos; nunca habilita una escena.
    if(!std::getenv("DBZ3_TEST_B2_MOTOR")||grupo!=4||!gestor||
       REX_LOAD_U32(kMgrPtr)!=gestor||REX_LOAD_U32(gestor+kEstadoCombate)!=4)return;
    static const std::array<float,8> cam=[] {
      std::array<float,8> v{};
      const char* ruta=std::getenv("DBZ3_TEST_B2_SOLAR_CAMARA");
      if(!ruta)return v;
      std::ifstream f(ruta);
      for(float& x:v)if(!(f>>x)||!std::isfinite(x))return std::array<float,8>{};
      std::string sobra;if(f>>sobra)return std::array<float,8>{};
      float d=0;for(unsigned i=0;i<3;++i)d+=(v[i+3]-v[i])*(v[i+3]-v[i]);
      if(d<.0001f||v[7]<=.05f||v[7]>=3.1f)return std::array<float,8>{};
      REXLOG_INFO("dbz3 B2 solar: camara PAL de laboratorio ojo={}/{}/{} objetivo={}/{}/{} giro={} fov={}",
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);
      return v;
    }();
    if(cam[7]<=0)return;
    uint32_t p=REX_LOAD_U32(gestor+0x264);
    if(p<0x82000000u||p>=0x83000000u)p=REX_LOAD_U32(gestor+0x260);
    if(p<0x82000000u||p>=0x83000000u)p=gestor+0x190;
    for(unsigned i=0;i<3;++i) {
      REX_STORE_U32(p+4*i,b2vm::Bits(cam[i]));
      REX_STORE_U32(p+16+4*i,b2vm::Bits(cam[i+3]));
    }
    REX_STORE_U32(p+12,b2vm::Bits(1));REX_STORE_U32(p+28,b2vm::Bits(1));
    REX_STORE_U32(p+48,b2vm::Bits(cam[6]));REX_STORE_U32(p+52,b2vm::Bits(cam[7]));
    REX_STORE_U32(p+56,REX_LOAD_U32(p+56)|1u);
    REX_STORE_U32(gestor+452,b2vm::Bits(cam[7]));
  }
  static bool ComprobarOriginal() {
    // Estado PAL observado en campo17650, captura072456. No es una salida
    // fabricada con esta función: comprueba proyección, inversa y profundidad.
    B2SolarEstado s;s.pos={-820,450,-650,1};
    const std::array<float,16> m{.9054831862449646f,0,-.4243822395801544f,0,
      0,-1,0,0,-.4243822395801544f,0,-.9054831862449646f,0,
      6.623648643493652f,12,28.9573917388916f,1};
    return s.Proyectar(m,579.4153442382812f)&&
      std::fabs(s.pantalla[0]-35.14787292480469f)<.001f&&
      std::fabs(s.pantalla[1]+24.37152099609375f)<.001f&&
      std::fabs(s.pantalla[2]-965.5147705078125f)<.001f&&
      std::fabs(s.profundidad-1766.013427734375f)<.005f&&
      std::fabs(s.coseno-.8081798553466797f)<.00001f;
  }
  bool EsSolar(uint8_t* base,uint32_t p) const {
    if(p<0x82398d40u||p>=0x82398d40u+256*176u||(p-0x82398d40u)%176u)return false;
    return std::any_of(tareas.begin(),tareas.end(),[&](const Tarea& t){return t.p==p;})&&
      REX_LOAD_U32(p+172)==marca&&REX_LOAD_U32(p+20)==0x820c8068&&
      REX_LOAD_U32(p+24)==0x820c7c68&&REX_LOAD_U32(0x82398940+4*((p-0x82398d40u)/176u))==p;
  }
  void Quitar(PPCContext& ctx,uint8_t* base) {
    for(const auto& t:tareas)if(EsSolar(base,t.p)) {PPCContext c=ctx;c.r3.u64=t.p;sub_820BD570(c,base);}
    tareas.clear();pendientes.clear();ultimo_publicado=~0u;
    std::lock_guard<std::mutex> l(mutex);quads.clear();publicado_ms=0;
  }
  bool Crear(PPCContext& ctx,uint8_t* base,const b2::Recurso& r) {
    static const bool oraculo=ComprobarOriginal();
    if(!oraculo){REXLOG_WARN("dbz3 B2 solar: fallo del oraculo PAL17650");return false;}
    const auto e=r.Bloque(12);const uint32_t zona=REX_LOAD_U32(kG+64);
    if(e.n<528||e.U(4)!=1||zona>=2)return false;
    const uint32_t off=32+128*zona;
    if(int32_t(e.U(off+4))<0)return true;
    const uint32_t mgr=REX_LOAD_U32(kMgrPtr),z=REX_LOAD_U32(kG+32);
    if(!mgr||!z)return false;
    if(gestor!=mgr||zdd!=z||alta!=g_activa||grupo!=int(r.grupo)) {
      Quitar(ctx,base);gestor=mgr;zdd=z;alta=g_activa;grupo=int(r.grupo);
    }
    std::erase_if(tareas,[&](const Tarea& t){return !EsSolar(base,t.p);});
    if(tareas.size()>=256)return false;
    bool libre=false;for(unsigned i=0;i<256;++i)if(!REX_LOAD_U32(0x82398940+4*i)){libre=true;break;}
    if(!libre)return false; // BD448 espera una ranura libre: no llamarlo con el pool lleno.
    const auto fx=r.Bloque(4);if(!fx.Rango(0,32))return false;
    std::vector<uint8_t> amb(fx.p,fx.p+fx.n);
    const auto amt=pasarela::HijoAmb(amb,0);
    std::array<pasarela::Imagen,2> imgs{pasarela::TexturaAmt(amt,8),pasarela::TexturaAmt(amt,9)};
    if(!imgs[0]||!imgs[1])return false;
    auto fl=[&](size_t p){const uint32_t x=e.U(p);float f;std::memcpy(&f,&x,4);return f;};
    for(unsigned i=0;i<20;++i){colores[i]=fl(288+4*i);if(!std::isfinite(colores[i]))return false;}
    for(unsigned i=0;i<5;++i){escalas[i]=fl(368+4*i);if(!std::isfinite(escalas[i])||escalas[i]<=0)return false;}
    for(unsigned i=0;i<4;++i) {
      uv[i]={fl(464+16*i),fl(468+16*i)};
      if(!std::isfinite(uv[i].x)||!std::isfinite(uv[i].y))return false;
    }
    std::array<float,4> posicion;
    for(unsigned j=0;j<4;++j)if(!std::isfinite(posicion[j]=fl(off+96+4*j)))return false;
    if(!nombre) {nombre=g_mem?g_mem->SystemHeapAlloc(32,32):0;if(!nombre)return false;
      std::memset(base+nombre,0,32);std::memcpy(base+nombre,"B2SOL",5);}
    PPCContext c=ctx;c.r3.u64=0x90000000u;c.r4.u64=nombre;c.r5.u64=0;
    c.r6.u64=0x820c8068;c.r7.u64=0x820c7c68;sub_820BD448(c,base);
    const uint32_t p=c.r3.u32;if(!p)return false;
    std::memset(base+p+48,0,128);
    REX_STORE_U32(p+32,(REX_LOAD_U32(p+32)&~15u)|4u);REX_STORE_U32(p+172,marca);
    Tarea t;t.p=p;
    t.estado.pos=posicion;
    for(unsigned j=0;j<4;++j)REX_STORE_U32(p+48+4*j,b2vm::Bits(posicion[j]));
    tareas.push_back(t);
    {std::lock_guard<std::mutex> l(mutex);imagenes=std::move(imgs);++generacion;}
    REXLOG_INFO("dbz3 B2 solar: creada tarea={:08X} zona={} posicion={}/{}/{} texturas=8/9 oraculo_PAL=1",
      p,zona,t.estado.pos[0],t.estado.pos[1],t.estado.pos[2]);
    return true;
  }
  bool Paso(PPCContext& ctx,uint8_t* base) {
    const uint32_t p=ctx.r3.u32;if(!EsSolar(base,p))return false;
    if(REX_LOAD_U32(kMgrPtr)!=gestor||REX_LOAD_U32(kG+32)!=zdd||g_activa!=alta||
       REX_LOAD_U32(gestor+kEstadoCombate)!=4)return true;
    auto& t=*std::find_if(tareas.begin(),tareas.end(),[&](const Tarea& x){return x.p==p;});
    std::array<float,16> m;
    // Vista HD: +y arriba/-z delante. La vista PAL invierte ambas filas.
    for(unsigned i=0;i<16;++i)m[i]=LeeFl(gestor+96+4*i)*((i%4==1||i%4==2)?-1.f:1.f);
    const float fov=LeeFl(gestor+452);
    if(fov>.01f&&fov<3.14f&&t.estado.Proyectar(m,240.f/std::tan(fov*.5f))) {
      ++t.pasos;
      REX_STORE_U32(p+156,t.pasos);
      for(unsigned i=0;i<4;++i){REX_STORE_U32(p+48+4*i,b2vm::Bits(t.estado.pos[i]));
        REX_STORE_U32(p+64+4*i,b2vm::Bits(t.estado.pantalla[i]));}
      REX_STORE_U32(p+80,b2vm::Bits(t.estado.profundidad));REX_STORE_U32(p+88,b2vm::Bits(t.estado.coseno));
    }
    return true;
  }
  bool Dibujar(PPCContext& ctx,uint8_t* base,bool transicion) {
    const uint32_t p=ctx.r3.u32;if(!EsSolar(base,p))return false;
    auto& t=*std::find_if(tareas.begin(),tareas.end(),[&](const Tarea& x){return x.p==p;});
    const uint32_t cuadro=REX_LOAD_U32(0x8238D6B4);
    if(t.ultimo_dibujo==cuadro)return true;t.ultimo_dibujo=cuadro;
    t.estado.Intensidad(512,512);REX_STORE_U32(p+84,b2vm::Bits(t.estado.alfa));++t.dibujos;
    REX_STORE_U32(p+160,t.dibujos);REX_STORE_U32(p+168,transicion?1:0);
    if(t.dibujos==1||t.dibujos%120==0)
      REXLOG_INFO("dbz3 B2 solar paso/dibujo: tarea={:08X} pasos={} dibujos={} transicion={} xy={}/{} z={} alfa={} coseno={}",
        p,t.pasos,t.dibujos,transicion,t.estado.pantalla[0],t.estado.pantalla[1],t.estado.pantalla[2],t.estado.alfa,t.estado.coseno);
    if(transicion||t.estado.alfa<=0)return true;
    REX_STORE_U32(p+164,REX_LOAD_U32(p+164)+5);
    for(unsigned i=0;i<5;++i) {
      Quad q;q.tex=i?1:0;q.uv=uv;
      const float s=escalas[i],radio=s*125,ang=t.estado.coseno*6.2831855f*s;
      const float co=std::cos(ang),si=std::sin(ang),div=s!=0?s:1;
      const float x=t.estado.pantalla[0]+(i?float(i)*(320-t.estado.pantalla[0])/8/div:0);
      const float y=t.estado.pantalla[1]+(i?float(i)*(224-t.estado.pantalla[1])/8/div:0);
      for(unsigned j=0;j<4;++j) {
        const float vx=(j&1)?radio:-radio,vy=(j&2)?radio:-radio;
        const float ry=si*vx+co*vy;
        q.p[j]={x+co*vx-si*vy,i?(y+ry)*(448.f/480.f):y*(448.f/480.f)+ry};
      }
      q.color=ImGui::ColorConvertFloat4ToU32(ImVec4(B2CanalSolarGs(colores[4*i]),
        B2CanalSolarGs(colores[4*i+1]),B2CanalSolarGs(colores[4*i+2]),B2CanalSolarGs(t.estado.alfa*s*.7f)));
      pendientes.push_back(q);
    }
    return true;
  }
  static uint64_t Ahora() {return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());}
  void Publicar(PPCContext& ctx,uint8_t* base,bool transicion) {
    if(tareas.empty())return;
    // Eliminar DESPUÉS de BD328: hacerlo desde Paso invalidaría el siguiente
    // enlace guardado por el recorrido nativo cuando existen varios solares.
    if(REX_LOAD_U32(kMgrPtr)!=gestor||REX_LOAD_U32(kG+32)!=zdd||g_activa!=alta||
       REX_LOAD_U32(gestor+kEstadoCombate)!=4){Quitar(ctx,base);return;}
    const uint32_t cuadro=REX_LOAD_U32(0x8238D6B4);
    if(ultimo_publicado==cuadro)return;ultimo_publicado=cuadro;pendientes.clear();
    // PAL1ADEA0 tiene una pasada EXCLUSIVA para flags&4. La pasada normal
    // excluye esos bits; en HD publicamos esta misma selección para la UI.
    for(const auto& t:tareas)if(EsSolar(base,t.p)&&(REX_LOAD_U32(t.p+32)&4)) {
      PPCContext c=ctx;c.r3.u64=t.p;(void)Dibujar(c,base,transicion);
    }
    std::lock_guard<std::mutex> l(mutex);quads=pendientes;publicado_ms=Ahora();
  }
  void Pintar(ImGuiIO& io) {
    std::lock_guard<std::mutex> l(mutex);
    if(subida!=generacion) {
      for(unsigned i=0;i<2;++i)texturas[i]=ps2::Crear(imagenes[i],ProbarZ());
      subida=generacion;
      if(texturas[0]&&texturas[1])REXLOG_INFO("dbz3 B2 solar: texturas originales8/9 subidas a la interfaz");
    }
    if(!publicado_ms||(!foto_detenida.load()&&Ahora()-publicado_ms>250)||quads.empty())return;
    const auto r=ingame::style::GuestImage(io);auto* dl=ImGui::GetForegroundDrawList();
    if(SinSolarFoto()){GuardarFotoUI(r,true,0);return;}
    if(ProbarZ()) {
      rex::cvar::SetFlagByName("dbz3_ui_depth","true");
      if(EstadoZ()!=2)return; // No presentar una foto sin profundidad como si la hubiera probado.
    }
    const int modo_z=ModoFotoZ();
    const float z=modo_z==1?-1.f:modo_z==2?z_prueba:0.f;
    unsigned emitidos=0;
    dl->PushClipRect(r.p0,r.p1,true);dl->AddCallback(rex::ui::kImDrawBlendAdditive,nullptr);
    for(const auto& q:quads)if(texturas[q.tex]) {
      std::array<ImVec2,4> p;
      for(unsigned j=0;j<4;++j)p[j]={r.p0.x+q.p[j].x*r.Size().x/640.f,r.p0.y+q.p[j].y*r.Size().y/448.f};
      auto uv=q.uv;
      if(ProbarZ())for(auto& v:uv)v=UVZ(v,z);
      dl->AddImageQuad(texturas[q.tex].id(),p[0],p[1],p[3],p[2],uv[0],uv[1],uv[3],uv[2],q.color);
      ++emitidos;
    }
    dl->AddCallback(rex::ui::kImDrawBlendAlpha,nullptr);dl->PopClipRect();
    GuardarFotoUI(r,false,emitidos);
  }
};
