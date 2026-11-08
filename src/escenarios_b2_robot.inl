// Consumidor90 de laboratorio. Se integra en la lista REAL HD; no usa el
// callback C8068 del donante como sustituto de las dos pasadas PAL1B29D0.
// Incluido dentro de B2FondoHD.
  uint32_t robot_tarea=0,robot_mem=0,robot_pasos=0,robot_dibujos=0;
  // Contexto exclusivo del dibujo propio; sonda optativa limitada al par240.
  inline static thread_local uint32_t robot_pasada=0,robot_material=0,robot_dentro_material=0;
  inline static thread_local bool robot_medir_constantes=false,robot_sin_brillo=false;
  std::vector<std::array<uint8_t,176>> robot_antes;
  static float FloatLE(b2::Vista v,uint32_t p) {
    return b2vm::Float(v.U(p));
  }
  bool EsRobot(uint8_t* base,uint32_t t) const {
    if(!(t&&t==robot_tarea&&B2TareasHD::Direccion(t)&&
        REX_LOAD_U32(t+20)==0x820C8068&&REX_LOAD_U32(t+24)==0x820C7C68&&
        REX_LOAD_U32(t+172)==robot_mem&&!raices.empty()&&
        REX_LOAD_U32(t+16)==raices[0].registro))return false;
    for(unsigned i=0;i<256;++i)if(REX_LOAD_U32(B2TareasHD::tabla+4*i)==t)return true;
    return false;
  }
  bool CrearRobot(PPCContext& ctx,uint8_t* base,const b2::Recurso& r) {
    const auto env=r.Bloque(12);
    if(r.grupo!=0||!env.Rango(0,528)||env.U(4)!=1||env.U(8)!=4||
       int32_t(env.U(32))<0||!Vincular(base,0))return false;
    auto& a=raices[0];if(!a.control||!Pistas(base,r,a))return false;
    const auto tareas=B2TareasHD::Buscar(a.registro,[&](uint32_t p){return REX_LOAD_U32(p);});
    if(!tareas.valida)return false;
    if(!robot_mem)robot_mem=g_mem?g_mem->SystemHeapAlloc(768,32):0;
    if(!robot_mem)return false;
    robot_antes.clear();robot_pasos=robot_dibujos=0;
    for(unsigned i=0;i<tareas.cantidad;++i) {
      std::array<uint8_t,176> copia{};std::memcpy(copia.data(),base+tareas.tareas[i],176);
      robot_antes.push_back(copia);
    }
    for(unsigned i=0;i<tareas.cantidad;++i) {
      PPCContext c=ctx;c.r3.u64=tareas.tareas[i];sub_820BD570(c,base);
    }
    std::memset(base+robot_mem,0,768);std::memcpy(base+robot_mem+704,"B2RR",5);
    PPCContext c=ctx;c.r3.u64=0x82000000;c.r4.u64=robot_mem+704;
    c.r5.u64=a.registro;c.r6.u64=0x820C8068;c.r7.u64=0x820C7C68;
    sub_820BD448(c,base);robot_tarea=c.r3.u32;
    if(!B2TareasHD::Direccion(robot_tarea))return false;
    REX_STORE_U32(robot_tarea+172,robot_mem);
    REX_STORE_U32(robot_tarea+32,REX_LOAD_U32(robot_tarea+32)|0x100);
    for(uint32_t control:{a.control,a.uv})if(control) {
      REX_STORE_U32(control+4,0);REX_STORE_U32(control+24,1);
    }
    a.por_guion=true;
    REXLOG_INFO("dbz3 B2 robot: creada tarea={:08X} sustituidas={} usuario={:08X} modelo={:08X} perfil={} reorientacion_tabla={} (rama condicional pendiente)",robot_tarea,tareas.cantidad,a.registro,a.modelo,env.U(32),env.U(40));
    return true;
  }
  bool DibujarRobot(PPCContext& ctx,uint8_t* base,const b2::Recurso& r) {
    if(!EsRobot(base,ctx.r3.u32))return false;
    const auto& a=raices[0];const uint32_t matrices=REX_LOAD_U32(a.modelo+1032);
    if(matrices<0x10000000u||matrices>=0xf0000000u)return true;
    const auto env=r.Bloque(12);
    // Estado de material, escala y dos bancos de luces. Restauración exacta
    // incluso si el estado de entrada no era el valor por defecto del motor.
    struct Estado {
      uint32_t p;std::vector<uint8_t> b;
      Estado(uint8_t* base,uint32_t p_,size_t n):p(p_),b(base+p_,base+p_+n){}
      void Restaurar(uint8_t* base)const{std::memcpy(base+p,b.data(),b.size());}
    };
    std::vector<Estado> prev;
    for(const auto& rango:std::array<std::pair<uint32_t,size_t>,6>{{
        {0x823106b0,32},{0x82389418,4},{0x82375760,4},
        {0x82389260,480},{0x824e6820,128},{0x823993f0,4}}})
      prev.emplace_back(base,rango.first,rango.second);
    auto llamar=[&](auto fn,uint32_t r3=0,uint32_t r4=0,uint32_t r5=0,uint32_t r6=0,uint32_t r7=0) {
      PPCContext c=ctx;c.r3.u64=r3;c.r4.u64=r4;c.r5.u64=r5;c.r6.u64=r6;c.r7.u64=r7;fn(c,base);
    };
    auto fl=[&](uint32_t p,float f){REX_STORE_U32(p,b2vm::Bits(f));};
    // PAL: punto de luz/distancia = traslación del hueso10, no matriz raíz.
    const uint32_t punto=matrices+10*64+48;
    float distancia=0;
    const uint32_t mgr=REX_LOAD_U32(kMgrPtr),cam=mgr?REX_LOAD_U32(mgr+264):0;
    for(unsigned i=0;i<3;++i) {
      fl(robot_mem+4*i,LeeFl(punto+4*i));
      const float d=LeeFl(punto+4*i)-(cam?LeeFl(cam+4*i):0);distancia+=d*d;
    }
    distancia=std::sqrt(distancia);
    const float factor=((cam?LeeFl(cam+52):0.785398163f)*360.0f/6.2831855f)/45.0f;
    const float seno=std::sin(((distancia-10.0f)/170.0f)*1.5707964f);
    const float escala=distancia<=10?factor*.015f:distancia>=180?factor*.35f:
        factor*seno*.35f+(1.0f-seno)*.015f;
    fl(0x82375760,escala); // no llamar139BD8: su fórmula es distinta
    std::memset(base+robot_mem+32,0,128);
    float norma=0;for(unsigned i=0;i<3;++i){const float x=FloatLE(env,64+4*i);norma+=x*x;}
    norma=std::sqrt(norma);
    if(!(norma>0)) {for(const auto& e:prev)e.Restaurar(base);return true;}
    for(unsigned i=0;i<3;++i) {
      fl(robot_mem+32+4*i,FloatLE(env,64+4*i)/norma);
      fl(robot_mem+96+4*i,FloatLE(env,80+4*i));
      fl(robot_mem+144+4*i,FloatLE(env,48+4*i));
    }
    fl(robot_mem+96+12,b2vm::Float(0x3d8f5c29));
    // 13AC10 PAL y886C0 HD dejan (0,0,0,1) en la cuarta dirección.
    // 85170 multiplica el color ambiental por ese W: con el cero del memset
    // C7CC8 sobrescribía con negro el ambiente recién preparado por85358.
    fl(robot_mem+32+48+12,1.0f);
    llamar(sub_82085058,1);llamar(sub_820C7CC8,robot_mem+32);
    const std::array<float,3> ambiente{{LeeFl(0x824e6860+48),LeeFl(0x824e6860+52),LeeFl(0x824e6860+56)}};
    REX_STORE_U32(0x823993f0,0);
    const uint32_t z=REX_LOAD_U32(kG+32),banco=z?REX_LOAD_U32(z+28):0;
    if(banco)llamar(sub_82092C58,banco,0xa0000000);
    const uint32_t primera_a=REX_LOAD_U32(0x823106b0),primera_b=REX_LOAD_U32(0x82389418);
    static const bool constantes=[] {const char* v=std::getenv("DBZ3_TEST_B2_ROBOT_CONSTANTES");
      return v&&std::strcmp(v,"1")==0;}();
    static const bool sin_brillo=[] {const char* v=std::getenv("DBZ3_TEST_B2_ROBOT_SIN_BRILLO");
      return v&&std::strcmp(v,"1")==0;}();
    const bool medir_constantes=constantes&&robot_dibujos+1==240;
    robot_medir_constantes=medir_constantes;robot_sin_brillo=sin_brillo;
    robot_material=0;robot_pasada=1;
    llamar(sub_8208F438,a.modelo);
    robot_pasada=0;
    PPCContext c=ctx;c.f1.f64=c.f2.f64=c.f3.f64=0;c.f4.f64=1;sub_820854F8(c,base);
    // PAL usa modo2, pero el contorno HD usa modo1 (C8068/C8208).
    // 94788 solo activa estado+53 para modo1;97290 sube entonces la
    // escala de expansión a la constante16. Copiar el2 dibujaba negro sin expansión.
    llamar(sub_820854C0,0x3c00000,0,0,0x400000,0);
    llamar(sub_820854C0,0x700,0,0,0x200,0);
    // PAL limpia20 (TME). HD94788 permite textura si !10000000 &&20:
    // para desactivarla se FIJA10000000, como C8068/C8238; no se limpia.
    llamar(sub_820854C0,0x10000000,0,0,0x10000000,0);
    // Ablación de laboratorio: aislar si las manos negras nacen en la segunda
    // pasada. No se usa para aceptar la escena ni modifica el guion/cámara.
    static const bool sin_segunda=[] {const char* v=std::getenv("DBZ3_TEST_B2_ROBOT_SIN_SEGUNDA");
      return v&&std::strcmp(v,"1")==0;}();
    const uint32_t segunda_a=REX_LOAD_U32(0x823106b0),segunda_b=REX_LOAD_U32(0x82389418);
    robot_material=0;robot_pasada=2;
    if(!sin_segunda)llamar(sub_8208F438,a.modelo);
    robot_pasada=0;robot_medir_constantes=false;robot_sin_brillo=false;
    for(const auto& e:prev)e.Restaurar(base);
    ++robot_dibujos;
    if(robot_dibujos==1||robot_dibujos%120==0)
      REXLOG_INFO("dbz3 B2 robot dibujo: par={} pasos={} tiempo={:.3f} hueso10=({:.6f},{:.6f},{:.6f}) distancia={:.6f} escala={:.9f} ambiente=({:.6f},{:.6f},{:.6f}) pasadas={} material_primera={:08X}/{:08X} material_segunda={:08X}/{:08X} rangos_capturados_restaurados=1",
          robot_dibujos,robot_pasos,LeeFl(a.control+4),LeeFl(punto),LeeFl(punto+4),LeeFl(punto+8),distancia,escala,
          ambiente[0],ambiente[1],ambiente[2],sin_segunda?1:2,primera_a,primera_b,segunda_a,segunda_b);
    return true;
  }
  void CerrarRobot(PPCContext& ctx,uint8_t* base,bool restaurar) {
    if(!robot_tarea&&robot_antes.empty())return;
    const bool sigue=EsRobot(base,robot_tarea);
    if(sigue){PPCContext c=ctx;c.r3.u64=robot_tarea;sub_820BD570(c,base);}
    // En un bloqueo reconstruir por el asignador; no copiar enlaces de la lista.
    if(restaurar)for(const auto& b:robot_antes) {
      std::memcpy(base+robot_mem+704,b.data(),16);REX_STORE_U8(robot_mem+720,0);
      auto u=[&](unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];};
      PPCContext c=ctx;c.r3.u64=u(28);c.r4.u64=robot_mem+704;c.r5.u64=u(16);c.r6.u64=u(20);c.r7.u64=u(24);
      sub_820BD448(c,base);const uint32_t t=c.r3.u32;
      if(B2TareasHD::Direccion(t)){std::memcpy(base+t+48,b.data()+48,128);REX_STORE_U32(t+32,u(32));}
    }
    REXLOG_INFO("dbz3 B2 robot cierre: pasos={} pares_dibujo={} seguia={} reconstruidas={}",robot_pasos,robot_dibujos,sigue,restaurar?robot_antes.size():0);
    robot_tarea=0;robot_antes.clear();
  }
