// Puente de la VM B2 al combate HD. Candidato de laboratorio: ninguna orden
// desconocida acredita éxito; se conserva el motivo concreto del bloqueo.
#include "escenarios_b2_anim.inl"
#include "escenarios_b2_fondo.inl"
#include "escenarios_b2_fundido.inl"
#include "escenarios_b2_solar.inl"

struct B2MotorHD {
  std::unique_ptr<b2::Transicion> escena;
  B2AnimHD anim;
  B2FondoHD fondo;
  B2FundidoHD fundido;
  B2SolarHD solar;
  uint32_t solar_inicial_gestor=0,solar_inicial_zdd=0;
  int solar_inicial_alta=-1;
  uint32_t mgr=0, actor[2]{}, scratch=0, origen=0, ultimo=~0u;
  uint32_t vida[2]{},ki[2]{};
  std::array<uint8_t,480> camara_antes{};
  uint8_t oculto[2][2]{};
  bool activo=false;
  std::string bloqueo;
  PPCContext* contexto=nullptr;
  uint8_t* base=nullptr;
  std::array<float,3> fija_ojo{},fija_obj{};
  std::array<float,3> punto_contacto{};
  bool fija=false;
  std::array<bool,2> visible{true,true};
  std::array<bool,2> aterrizaje_b2{false,false};
  // Aterrizaje PAL de Ciudad (DBZ3_TEST_B2_ATERRIZAJE_15B2=2): ver Paso.
  std::array<uint32_t,2> aterrizaje_f0{0,0};
  std::array<std::array<float,3>,2> aterrizaje_p0{};
  std::map<int,int> pantallas_fin;
  uint32_t pantallas_dibujadas=0;
  uint16_t vibracion[2][2]{};
#include "escenarios_b2_foto.inl"
#include "escenarios_b2_trayectoria.inl"
#include "escenarios_b2_reaccion.inl"
  bool PrepararSolar(PPCContext& ctx,uint8_t* base) {
    const uint32_t m=REX_LOAD_U32(kMgrPtr),z=REX_LOAD_U32(kG+32);
    if(!m||!z||REX_LOAD_U32(m+kEstadoCombate)!=4) {
      solar_inicial_gestor=solar_inicial_zdd=0;solar_inicial_alta=-1;return true;
    }
    if(solar_inicial_gestor==m&&solar_inicial_zdd==z&&solar_inicial_alta==g_activa)return true;
    if(g_b2.version<2&&!g_b2_leido){std::vector<Clip> cam;(void)LeerClips(1,cam);}
    if(!solar.Crear(ctx,base,g_b2))return false;
    dbz3::launcher::DeferUi(&CrearClimaPanel);
    // Una vez al entrar en combate, no cada vez que79 vacía la lista.
    // PAL ya tiene el solar antes del primer contacto/lanzamiento.
    solar_inicial_gestor=m;solar_inicial_zdd=z;solar_inicial_alta=g_activa;
    return true;
  }
  bool Vibrar(unsigned lado,uint32_t tipo,uint32_t valor) {
    if(lado>=2||!scratch||!contexto)return false;
    auto& v=vibracion[lado];
    if(tipo==0xD2)v[0]=v[1]=0;
    else if(tipo==0xD0)v[0]=uint16_t(uint8_t(valor))*257u;
    else v[1]=valor?65535:0;
    const unsigned fisico=actor[lado]==mgr+2096?0:1;
    const int usuario=g_esc_mando[fisico];
    if(usuario>=0&&usuario<4) {
      REX_STORE_U16(scratch+224,v[0]);REX_STORE_U16(scratch+226,v[1]);
      PPCContext c=*contexto;c.r3.u64=uint32_t(usuario);c.r4.u64=0;c.r5.u64=scratch+224;
      __imp__XamInputSetState(c,base);
    }
    return true;
  }
  void CerrarPantalla(int slot) {
    PPCContext c=*contexto;c.r3.s64=slot;sub_82156560(c,base);
  }
  void PasarPantallas() {
    std::array<bool,4> hechas{};
    for(const auto& [t,slot]:pantallas_fin)if(!hechas[size_t(slot)]) {
      hechas[size_t(slot)]=true;PPCContext c=*contexto;c.r3.s64=slot;
      sub_82156600(c,base); //21DC70: paso de la instancia, no solo limpieza final
    }
  }
  void DibujarPantallas(PPCContext& ctx,uint8_t* b) {
    if(!activo)return;
    fundido.Dibujar(REX_LOAD_U32(0x8238D6B4),escena->reloj.delta);
    if(pantallas_fin.empty())return;
    std::array<bool,4> hechas{};
    PPCContext c=ctx;c.r3.u64=0;sub_8214F4F8(c,b);
    for(const auto& [t,slot]:pantallas_fin)if(!hechas[size_t(slot)]) {
      hechas[size_t(slot)]=true;c=ctx;c.r3.s64=slot;
      sub_82156688(c,b);++pantallas_dibujadas; //21DC30: dibujar en la pasada real
    }
    c=ctx;sub_8214F5D0(c,b);
  }
  static void Fl(uint8_t* base,uint32_t a,float f) {REX_STORE_U32(a,b2vm::Bits(f));}
  void Conservar() {
    if(!mgr||REX_LOAD_U32(kMgrPtr)!=mgr||REX_LOAD_U32(mgr+kEstadoCombate)!=4)return;
    for(unsigned i=0;i<2;++i){REX_STORE_U32(actor[i]+6080,vida[i]);REX_STORE_U32(actor[i]+6100,ki[i]);}
  }
  bool Llamar(void(*fn)(PPCContext&,uint8_t*),const b2::Orden& o) {
    if(!scratch||!contexto||!mgr)return false;
    const uint32_t p0=REX_LOAD_U32(mgr+35188),p1=REX_LOAD_U32(mgr+35192);
    REX_STORE_U32(mgr+35188,actor[0]);REX_STORE_U32(mgr+35192,actor[1]);
    std::memset(base+scratch,0,256);REX_STORE_U32(scratch+44,scratch+128);
    for(size_t i=0;i<o.cuenta;++i)REX_STORE_U32(scratch+128+uint32_t(i)*4,o.argumentos[i]);
    PPCContext c=*contexto;c.r3.u64=scratch+64;c.r4.u64=scratch;
    c.r9.u64=c.r10.u64=0; // llamada adaptada, no opcode IW crudo del guest
    if(o.id==0x11||o.id==0x13||o.id==0x19||o.id==0xC1)MedirActores1450("orden_antes",&o);
    fn(c,base);
    if(o.id==0x11||o.id==0x13||o.id==0x19||o.id==0xC1)MedirActores1450("orden_despues",&o);
    REX_STORE_U32(mgr+35188,p0);REX_STORE_U32(mgr+35192,p1);
    return true;
  }
  bool Punto(unsigned lado,int ancla,std::array<float,3>& p,bool solo_local=false) {
    if(lado>=2)return false;
    const uint32_t x=actor[lado];
    if(ancla<0) {for(unsigned j=0;j<3;++j)p[j]=LeeFl(x+64+4*j);return true;}
    if(ancla>28)return false;
    const uint32_t mod=REX_LOAD_U32(x+2952),mapa=REX_LOAD_U32(x+2956);
    if(mod<0x10000000u||mapa<0x10000000u)return false;
    const uint32_t tabla=REX_LOAD_U32(mod+1032);
    const int idx=int16_t(REX_LOAD_U16(mapa+2u*uint32_t(ancla)));
    if(tabla<0x10000000u||idx<0||idx>255)return false;
    float v[3];for(unsigned j=0;j<3;++j)v[j]=LeeFl(tabla+64u*uint32_t(idx)+48+4*j);
    for(unsigned j=0;j<3;++j) {
      p[j]=LeeFl(x+16*j)*v[0]+LeeFl(x+16*j+4)*v[1]+LeeFl(x+16*j+8)*v[2];
      if(!solo_local)p[j]+=LeeFl(x+48+4*j);
      if(!std::isfinite(p[j]))return false;
    }
    return true;
  }
  bool Orden(const b2::Orden& o,b2vm::Maquina& vm,b2vm::Tarea& h) {
    const auto& a=o.argumentos;
    auto lado=[&](size_t i){return i<o.cuenta&&a[i]<2;};
#include "escenarios_b2_ordenes2.inl"
    switch(o.id) {
      case 0x25:case 0x26:case 0x27: {
        if(!lado(1))return false;
        std::array<float,3> p;if(!Punto(a[1],b2vm::Signo(a[0]),p))return false;
        h.f[0]=b2vm::Bits(p[o.id-0x25]);return true;
      }
      case 0x28: {
        if(!lado(0))return false;
        const uint32_t x=actor[a[0]];h.r[0]=REX_LOAD_U32(x+2928);
        // El estado B2 12 corresponde al20 de HD (caso especial E6430/E6D8).
        // 1DACA0 asigna98+selector9C24. Los códigos de reacción B3 no son
        // identificadores B2: devolver78 hacía tomar la rama girada180grados.
        const uint16_t accion=REX_LOAD_U16(x+3244);
        if(accion==20||accion==22) {
          const int selector=int8_t(REX_LOAD_U8(x+6208));
          //82105A78 y82105FE8 comparten154+selector. Sus homólogos B2
          //1DACA0/1DA650 usan152+selector; en el segundo solo0/1acreditados
          //(1DA7F0 normaliza9C24 yA05juntos al escoger frente/espalda).
          const auto perfil=g_b2.Bloque(7);
          //6248 NOesunpredicadopersistente: 82114428 consume3ylo borra
          //al volver (82114554..5C). En la consulta legítima ya vale0.
          if(selector<0||selector>7||(accion==22&&selector>1)||
             h.r[0]!=uint32_t(154+selector)||
             perfil.n<16||perfil.U(4)!=1) {
            bloqueo="reaccion HD sin equivalencia B2: estado="+std::to_string(accion)+
                     " codigo="+std::to_string(h.r[0])+" selector="+std::to_string(selector);
            return false;
          }
          h.r[0]=uint32_t(152+selector);
        }
        return true;
      }
      case 0x2A:if(!lado(0))return false;h.r[0]=uint32_t(int32_t(int8_t(REX_LOAD_U8(actor[a[0]]+8185))));return true;
      case 0xBD:if(!lado(0))return false;h.r[0]=REX_LOAD_U32(actor[a[0]]+2980);return true;
      case 0x124:if(!lado(0))return false;h.r[0]=REX_LOAD_U32(actor[a[0]]+4944);return true;
      case 0x10: {
        if(!lado(4)||(a[3]&~0x10fu)||(a[3]&15)>3)return false;
        b2::Orden hd=o;
        // B2: marco en nibble bajo, bit100=sin suelo; HD:16..19 y bit24.
        hd.argumentos[3]=((a[3]&15)<<16)|((a[3]&0x100)<<16);
        return Llamar(sub_8211CD68,hd);
      }
      case 0x11: {
        if(!lado(2)||(a[1]&~0x1ffu))return false;
        if(std::none_of(std::begin(B2AnimHD::mapas),std::end(B2AnimHD::mapas),
                        [&](const auto& m){return m.codigo==a[0];})) {
          bloqueo="codigo de movimiento B2 sin oraculo: "+std::to_string(a[0]);
          return false; // no resolver un código B2 accidentalmente contra el CSK de B3
        }
        b2::Orden hd=o;
        // 1FB770:80 selecciona cuerpo secundario,100 fija el tiempo y omite
        // suelo. En8211D2E0 esos bits son40,80 y01000000 respectivamente.
        // El40 ORIGINAL pide recalcular la matriz, no un cuerpo sin modelo.
        hd.argumentos[1]=(a[1]&0x3f)|((a[1]&0x80)>>1)|
                          ((a[1]&0x100)>>1)|((a[1]&0x100)<<16);
        REXLOG_INFO("dbz3 B2 movimiento: codigo={} flags={:X}->{:X} lado={}",a[0],a[1],hd.argumentos[1],a[2]);
        if(!Llamar(sub_8211D2E0,hd))return false;
        if(a[1]&0x40) {
          PPCContext c=*contexto;c.r3.u64=actor[a[2]];c.r4.u64=0;
          sub_821109D0(c,base); // homólogo de1F6AB0: matriz desde posición/giro
        }
        return true;
      }
      case 0x13: return lado(3)&&OrdenReaccion(o); //1FB650, adaptación optativa del paso PAL18
      case 0x16: return lado(1)&&Llamar(sub_8211D810,o); // aire y recálculo de postura
      case 0x19: return lado(2)&&Llamar(sub_8211D000,o); // desplazamiento respecto al rival
      case 0x1C: {
        if(!lado(1))return false;
        // La firma HD añade FASE entre tiempo y lado. Pasar dos palabras
        // trataba el lado como fase y actuaba SIEMPRE sobre el atacante.
        b2::Orden hd=o;hd.cuenta=3;hd.argumentos[1]=a[0]&1;hd.argumentos[2]=a[1];
        if(!Llamar(sub_8211D228,hd))return false;
        // B3 multiplica por3 el temporizador de recuperación; B2 lo guarda sin
        // ese factor (1FB380). Mantener el parámetro B2, no alargar la escena.
        Fl(base,actor[a[1]]+6312,float(b2vm::Signo(a[0])));return true;
      }
      case 0x1D:return lado(1)&&Llamar(sub_8211D918,o); // B29C64 -> HD6664, offset de suelo/IK (no6352: ángulo)
      case 0x15: {
        if(!lado(2)||(a[1]&~15u)||(a[1]&15)>3)return false;
        // 1FBC80 ->8211FE48: marco bajo pasa a16. El giro HD está en6052;
        // 4952 es un PUNTERO a otro actor y jamás debe recibir un ángulo.
        b2::Orden hd=o;hd.argumentos[1]=a[1]<<16;
        return Llamar(sub_8211FE48,hd);
      }
      case 0x24:
        if(!lado(1))return false;visible[a[1]]=a[0]!=0;return true;
      case 0x2D: {
        if(!lado(0)||!contexto)return false;
        PPCContext c=*contexto;c.r3.u64=actor[a[0]];sub_82116808(c,base);return true;
      }
      case 0xB3: {PPCContext c=*contexto;sub_821171B8(c,base);return true;}
      case 0x10C: {
        // 20C9C0 limpia los efectos antes de la escena. En HD la limpieza
        // global está enD3088: lista/callbacks, canales, pantalla y FX de combate.
        // Es una adaptación al ciclo de vida HD, no un return1 sin consumidor.
        PPCContext c=*contexto;sub_820D3088(c,base);return true;
      }
      case 0xA6: {
        // 1B77F0 -> 21BED0(02000001,0); mismo filtro/mark56 en8214F938.
        PPCContext c=*contexto;c.r3.u64=0x02000001;c.r4.u64=0;sub_8214F938(c,base);return true;
      }
      case 0x79: {
        // 1B7880/1ADB50: borra tareas de prioridad90000000 (sol).
        // B85C8 limpia auras del COMBATE: no es el homólogo de este filtro.
        for(uint32_t i=0;i<256;++i) {
          const uint32_t t=REX_LOAD_U32(0x82398940+4*i);
          if(t&&REX_LOAD_U32(t+28)==0x90000000u) {
            PPCContext c=*contexto;c.r3.u64=t;sub_820BD570(c,base);
          }
        }
        solar.Quitar(*contexto,base);
        return true;
      }
      case 0x78: {
        const bool ok=solar.Crear(*contexto,base,g_b2);
        if(!ok)bloqueo="solar B2: recurso ENV/AMT o tarea no disponible";
        return ok;
      }
      case 0x7E:return Llamar(sub_8211F760,o); // alpha=arg1, decremento=arg0; D53F0 / FC5xx dibujan el mismo blanco
      case 0x7F:Fl(base,mgr+35168,-1);return true; // mismo veto alpha>0 que142D8 del original
      case 0x160:
        // 21D3C0 y82155EA8 construyen las mismas16palabras para10005/20005.
        return a[13]<4&&Llamar(sub_82155EA8,o);
      case 0x163: {
        //21D240 instala el callback21D1F0 de paso/dibujo por tarea. También
        // se libera al terminar; no basta con registrar una limpieza final.
        auto it=pantallas_fin.find(h.ranura);
        if(!a[0]) {
          if(it!=pantallas_fin.end()){CerrarPantalla(it->second);pantallas_fin.erase(it);}
        } else {
          if(a[1]>=4)return false;
          pantallas_fin[h.ranura]=int(a[1]);
        }
        return true;
      }
      case 0xD0:case 0xD1:return lado(1)&&Vibrar(a[1],o.id,a[0]);
      case 0xD2:return lado(0)&&Vibrar(a[0],o.id,0);
      case 0xB8: // El bloqueo de ambos mandos se monta al iniciar; B2 los libera al salir.
        return lado(1);
      case 0x14: // Daño del suceso: el contrato HD conserva vida/ki/contador.
        h.r[0]=0;Conservar();return true;
      case 0xC1:
        if(!lado(3))return false;
        if(!trayectoria_nativa)return true; // Camino anterior, sin activar el experimento.
        if(a[2]!=~0u&&(a[2]>28||a[1]>=g_b2.trayectoria.size()))return false;
        return Llamar(sub_8211D6F0,o);
      case 0x31: {
        if(a[6]&~0xffu)return false;
        b2::Orden hd=o;
        // 1FA730 y8211E6F8: los dos marcos de cámara pasan de nibbles4/0
        // a20/16 en HD. XYZ y anclaje conservan orden de pila.
        hd.argumentos[6]=((a[6]&0xf0)<<16)|((a[6]&15)<<16);
        return Llamar(sub_8211E6F8,hd);
      }
      case 0x35:fija=false;return a[5]==2&&a[4]<g_b2.camara.size();
      case 0x95:return a[0]==0; // Banco de cámara explícito del B2TZ, no el del donante.
      case 0x94:return fondo.Controlar(*contexto,base,g_b2,o);
      case 0x90:return fondo.CrearRobot(*contexto,base,g_b2);
      case 0x96:return fondo.CrearFx(*contexto,base,g_b2,o);
      case 0x64:return fundido.Color(o);
      case 0x61:case 0x62:return fundido.Crear(o);
      case 0x40:CambiarZonaMotor(*contexto,base);return true;
      case 0x45:case 0x46:case 0x47:
        // ASM1B9260/1B9240/1B9220: f0=XYZ del contacto que lanzó la escena.
        h.f[0]=b2vm::Bits(punto_contacto[o.id-0x45]);return true;
      case 0x52:dbz3::combate::OcultarHud(true);return true;
      case 0x66:return true; // ELF1C7A10: return1 literal.
      case 0x98:return g_b2.grupo!=5; // 1B2040 solo trabaja en ELF2 (grupo5), fuera de estos cinco escenarios.
      case 0x4C: {
        const auto snd=g_b2.Bloque(7);
        if(snd.n<16||snd.U(4)!=1)return false;
        pasarela::OirSonidoPs2(pasarela::IsoPs2(1),"DATA_CMN.AFS",int(snd.U(8)),int(a[0]),1,1,0,
                              REXCVAR_QUERY(bool,audio_mute));return true;
      }
      case 0x84: {
        const auto snd=g_b2.Bloque(7);
        if(a[1]!=200||snd.n<16||snd.U(4)!=1||a[0]>=snd.U(12)||!snd.Rango(16+2*a[0],2))return false;
        const unsigned p=16+2*a[0];const int id=int(snd.p[p]|uint16_t(snd.p[p+1])<<8);
        pasarela::OirAdxPs2(pasarela::IsoPs2(1),"ADX_US.AFS",id,REXCVAR_QUERY(bool,audio_mute)?0:1);
        h.r[0]=3;return true; // FUN1F86F0 devuelve canal3 para banco200.
      }
      case 0x86:pasarela::CallarAdxPs2();return true;
      case 0x87:
        //1FA220/12BB70: detener canal. El banco200 de84 usa nuestro canal3.
        if(a[0]!=3&&a[0]!=~0u)return false;
        pasarela::CallarAdxPs2();return true;
      default:
        bloqueo="nativa B2 sin adaptar "+std::to_string(o.id)+" PC="+std::to_string(o.pc)+" cuadro="+std::to_string(o.cuadro);
        return false;
    }
  }
  bool Iniciar(PPCContext& ctx,uint8_t* b,const B2Contacto& contacto) {
    if(activo||!NieveB2Activa()||dbz3::g_guest_eu)return false;
    base=b;contexto=&ctx;mgr=REX_LOAD_U32(kMgrPtr);
    if(mgr!=contacto.gestor||!mgr)return false;
    punto_contacto=contacto.punto;
    pantallas_dibujadas=0;
    fundido.Limpiar();
    dbz3::launcher::DeferUi(&CrearClimaPanel);
    std::memcpy(camara_antes.data(),base+mgr+160,camara_antes.size());
    PrepararFotos();
    PrepararReaccion();
    if(!fondo.Cargar(base,g_b2)){bloqueo="raices B2 invalidas";return false;}
    actor[1]=contacto.actor;actor[0]=mgr+2096+(actor[1]==mgr+2096?8240:0);
    if(!anim.Preparar(base,g_b2)||!anim.Instalar(base,actor[0])||!anim.Instalar(base,actor[1])) {
      anim.Restaurar(base);bloqueo="no se pudieron vincular pistas B2 a los huesos HD";
      REXLOG_WARN("dbz3 B2 HD: inicio bloqueado: {}",bloqueo);return false;
    }
    if(!scratch)scratch=g_mem->SystemHeapAlloc(256,32);
    if(!scratch){anim.Restaurar(base);return false;}
    if(!PrepararTrayectoria(ctx)) {
      anim.Restaurar(base);bloqueo="trayectoria ACC nativa no disponible";return false;
    }
    for(unsigned i=0;i<2;++i){vida[i]=REX_LOAD_U32(actor[i]+6080);ki[i]=REX_LOAD_U32(actor[i]+6100);
      oculto[i][0]=REX_LOAD_U8(actor[i]+3261);oculto[i][1]=REX_LOAD_U8(actor[i]+4925);}
    QuietarLuchadores();dbz3::combate::OcultarHud(true);
    aterrizaje_b2={false,false};
    escena=std::make_unique<b2::Transicion>();
    b2::Puente p;
    p.ejecutar=[this](const auto& o,auto& m,auto& h){return Orden(o,m,h);};
    p.observar=[](const auto& o){REXLOG_INFO("dbz3 B2 HD orden: cuadro={:.1f} id={:X} PC={:X} tarea={}",o.cuadro,o.id,o.pc,o.tarea);};
    // El original ya tiene el solar de la zona inicial antes del evento.
    // La orden79 del guion lo retira;78 crea el de la zona de llegada.
    if(!PrepararSolar(ctx,base)) {
      RestaurarTrayectoria();anim.Restaurar(base);SoltarLuchadores();dbz3::combate::OcultarHud(false);
      bloqueo="solar B2 inicial no disponible";return false;
    }
    activo=escena->Iniciar(g_b2,uint32_t(contacto.entrada),std::move(p));
    escena->vm.ganchos.terminar=[this](auto&,auto& h) {
      const auto it=pantallas_fin.find(h.ranura);
      if(it!=pantallas_fin.end()){CerrarPantalla(it->second);pantallas_fin.erase(it);}
    };
    origen=REX_LOAD_U32(0x8238D6B4);ultimo=~0u;
    if(activo){PrepararTraza();MedirTraza("inicio");}
    REXLOG_INFO("dbz3 B2 HD: inicio por colision real entrada={} guest={} actores={:08X}/{:08X}",contacto.entrada,origen,actor[0],actor[1]);
    if(!activo)Terminar("fallo al iniciar");
    return activo;
  }
  void Terminar(const char* motivo) {
    MedirTraza("terminar");if(traza.is_open())traza.close();
    if(escena)escena->vm.Parar(); // ejecuta limpiezas de tareas también al bloquearse
    for(unsigned i=0;i<2;++i)(void)Vibrar(i,0xD2,0);
    Conservar();
    REXLOG_INFO("dbz3 B2 HD: {} cuadro={:.1f} bloqueo='{}' VM='{}'",motivo,escena?escena->reloj.cuadro:0,bloqueo,escena?escena->Error():"");
    REXLOG_INFO("dbz3 B2 FX resumen: instancias={} dibujos={} paso_global={:.3f}",fondo.instancias_fx.size(),fondo.dibujos_fx,LeeFl(0x824392B0+8));
    REXLOG_INFO("dbz3 B2 pantallas: dibujos={}",pantallas_dibujadas);
    REXLOG_INFO("dbz3 B2 fundido: dibujos={} tarea_viva={}",fundido.dibujos,fundido.estado.existe);
    fundido.Limpiar();
    if(mgr&&REX_LOAD_U32(kMgrPtr)==mgr) {
      fondo.CerrarRobot(*contexto,base,escena&&!escena->Error().empty());
      if(escena&&!escena->Error().empty())solar.Quitar(*contexto,base);
      if(escena&&!escena->Error().empty())fondo.Devolver(*contexto,base);
      for(unsigned i=0;i<2;++i){REX_STORE_U8(actor[i]+3261,oculto[i][0]);REX_STORE_U8(actor[i]+4925,oculto[i][1]);}
      RestaurarTrayectoria();anim.Restaurar(base);SoltarLuchadores();
      dbz3::escenarios::b2ik::RestaurarIkB3(base);   // eB2i (DBZ3_TEST_B2_SIN_IK_B3)
      if(contexto&&REX_LOAD_U32(mgr+kEstadoCombate)==4)for(unsigned i=0;i<2;++i) {
        PPCContext c=*contexto;c.r3.u64=actor[i];c.r4.u64=0;sub_8210E6D8(c,base);
      }
      std::memcpy(base+mgr+160,camara_antes.data(),camara_antes.size());
    }
    activo=false;g_esc_cam=false;dbz3::combate::OcultarHud(false);
    trayectoria_nativa=false;
  }
  bool Paso(PPCContext& ctx,uint8_t* b) {
    if(!activo)return false;
    base=b;contexto=&ctx;
    if(REX_LOAD_U32(kMgrPtr)!=mgr||REX_LOAD_U32(mgr+kEstadoCombate)!=4){Terminar("pelea terminada");return false;}
    const uint32_t f=REX_LOAD_U32(0x8238D6B4);if(f==ultimo){Conservar();return true;}
    EsperarFoto();ultimo=f;
    MedirActores1450("antes_vm");
    if(!escena->Paso(1)) {Terminar("VM bloqueada");return false;}
    MedirActores1450("despues_vm");
    PasarPantallas();
    Conservar();
    MedirActores1450("antes_trayectoria");
    for(unsigned i=0;i<2;++i) {
      REX_STORE_U8(actor[i]+3261,visible[i]?0:1);REX_STORE_U8(actor[i]+4925,visible[i]?0:1);
      if(aterrizaje_b2[i]) {
        // Medido en PAL (traza solB2X g8, victima): estado15/codigo137 con
        // vel (0.92,0,-6.94) durante 15.6 relojes y luego estado20/codigo168
        // con vel (0.34,.,-2.58) otros 15.6; luego quieta. Sin Y (suelo).
        const char* m=std::getenv("DBZ3_TEST_B2_ATERRIZAJE_15B2");
        if(m&&!std::strcmp(m,"2")) {
          const float dt=float(int32_t(f-aterrizaje_f0[i])),T=15.6f;
          const float t1=std::min(dt,T),t2=std::max(0.f,std::min(dt-T,T));
          const float d[3]={0.92f*t1+0.34f*t2,0.f,-6.94f*t1-2.58f*t2};
          for(unsigned j=0;j<3;j+=2){const float v=aterrizaje_p0[i][j]+d[j];Fl(base,actor[i]+64+4*j,v);Fl(base,actor[i]+48+4*j,v);}
        }
        continue;
      }
      if(trayectoria_nativa) {
        // El reloj sigue siendo el del guion original; el guest consume el
        // ACC reubicado al actualizar las posiciones, sin una segunda copia.
        Fl(base,actor[i]+6760,escena->actores[i].tiempo);continue;
      }
      std::array<float,3> p,hueso;
      if(escena->Trayectoria(i,p)&&Punto(i,escena->actores[i].hueso,hueso,true))
         for(unsigned j=0;j<3;++j){const float v=p[j]-hueso[j];Fl(base,actor[i]+64+4*j,v);Fl(base,actor[i]+48+4*j,v);}
    }
    MedirActores1450("despues_trayectoria");
    anim.Aterrizar(base,actor[1],escena->reloj.cuadro);   // eB2h: LEVANTAR_B2
    {   // eB2j: primer cuadro de cada cambio de estado de la victima (el cambio se hace despues de animar: la pose sale con el estado anterior un cuadro)
      static uint32_t prev_cod=~0u;static const char* rp=std::getenv("DBZ3_TEST_B2_REPOSE_VICTIMA");
      const uint32_t x1=actor[1],cod=REX_LOAD_U32(x1+2928),c1=x1+0x650,mod=REX_LOAD_U32(c1+0x538);
      if(escena->reloj.cuadro<2)prev_cod=~0u;
      if(cod!=prev_cod&&prev_cod!=~0u&&mod>=0x10000000u&&mod<0xeffff000u) {
        static unsigned nlog=0;
        if(nlog++<40)REXLOG_INFO("dbz3 B2 victima cambio {}->{} cuadro {:.1f} reloj3160 {:.3f} mod1060 {:.3f} clip {:X} +1540 {:X} +1580 {:X} +1620 {:.3f}",prev_cod,cod,escena->reloj.cuadro,
          LeeFl(x1+3160),LeeFl(mod+1060),REX_LOAD_U32(mod+1052),REX_LOAD_U32(c1+1540),REX_LOAD_U32(c1+1580),LeeFl(c1+1620));
        if(rp&&*rp&&(cod==141||cod==131)) {   // solo el inicio del aterrizaje (en 154 la repeticion alargo el estado 4 cuadros); valor = suma al reloj del actor antes de rehacer la pose
          const uint32_t ant=REX_LOAD_U32(x1+3160);const float t=LeeFl(x1+3160)+std::strtof(rp,nullptr);uint32_t u;std::memcpy(&u,&t,4);REX_STORE_U32(x1+3160,u);
          PPCContext c=ctx;c.r3.u64=c1;c.f1.f64=1.0;sub_820FAF40(c,base);REX_STORE_U32(x1+3160,ant);
        }
      }
      prev_cod=cod;
    }
    anim.BaseVictima(base,actor[1]);
    anim.Retener(base,actor[1]);   // eB2h: Base, DBZ3_TEST_B2_LEVANTAR_B2 (apagado)
    if(anim.ForzarAtaque(base,actor[0])&&anim.atq_primero&&anim.PoseYa()) {   // eB2g: DBZ3_TEST_B2_ATAQUE_B2 (apagado)
      // eB2i: el cuadro del cambio ya se animo con el clip nativo; se vuelve a llamar a la animacion del cuerpo (820FAF40: clip,
      // capas y matrices) con el clip B2 para no dejar 1-2 cuadros con la pose B3 (Kami cuadros 1-2, 17-35 grados). Peso 1 (f=1).
      PPCContext c=ctx;c.r3.u64=actor[0]+0x650;c.f1.f64=1.0;sub_820FAF40(c,base);
    }    dbz3::escenarios::b2ik::AplicarMirada(base,actor[0],actor[1],escena->reloj.cuadro); // eB2g: mirada PAL (DBZ3_TEST_B2_MIRADA, apagado)
    for(unsigned i=0;i<2;++i)dbz3::escenarios::b2ik::AplicarActor(base,actor[i]); // eB2f: IK de suelo PAL (DBZ3_TEST_B2_IK_SUELO, apagado)
    if(escena->camara>=0) {
      std::copy(escena->ojo.begin(),escena->ojo.end(),g_esc_ojo);
      std::copy(escena->objetivo.begin(),escena->objetivo.end(),g_esc_obj);
      g_esc_giro=escena->giro_camara;g_esc_fov=escena->fov;g_esc_cam=true;
    }
    MedirTraza("paso");
    if(!escena->Viva()){Terminar("guion finalizado");return false;}
    EstadoFoto();
    return true;
  }
};
B2MotorHD g_b2_motor;
bool B2ActualizarFx(PPCContext& ctx,uint8_t* base) {
  return g_b2_motor.activo&&g_b2_motor.fondo.ActualizarFx(ctx,base);
}

void B2ConsultarLanzamiento(PPCContext& ctx,uint8_t* base,uint32_t actor) {
  if(!NieveB2Activa()||g_b2_motor.activo||!B2MotorOn())return;
  const uint32_t mgr=REX_LOAD_U32(kMgrPtr);
  // B2: estados0x12/0x14; tablas393520/3935B0.18=1DACA0/1DA7F0;
  //20=1DA080/1D9F30.3935AC era una celda anterior, no la tabla de paso.
  // HD:20 especial deE6D8 y22(82105FE8/821060E8).25 es otra reacción;
  // aceptarla lanzaba al simple empujón con códigoB378, sin equivalente medido.
  if(!mgr||REX_LOAD_U32(mgr+kEstadoCombate)!=4||
     (actor!=mgr+2096&&actor!=mgr+2096+8240)||
      (REX_LOAD_U16(actor+3244)!=20&&REX_LOAD_U16(actor+3244)!=22)||int8_t(REX_LOAD_U8(actor+8184))==-1)return;
  if(g_b2.version<2&&!g_b2_leido){std::vector<Clip> c;(void)LeerClips(1,c);}
  if(g_b2.version<2)return;
  // Diagnóstico acotado de laboratorio: no cambia el resultado de la consulta.
  // Base20/22 cruzaba una pared válida pero no llegaba al registro REAL.
  static unsigned diagnosticos=0;
  const bool diagnostico=std::getenv("DBZ3_TEST_B2_CONSULTA")&&diagnosticos<128;
  if(diagnostico)++diagnosticos;
  const uint32_t cuadro_diagnostico=diagnostico?REX_LOAD_U32(0x8238D6B4):0;
  std::array<float,3> p,v,objetivo;
  float longitud=0;
  for(unsigned j=0;j<3;++j){p[j]=LeeFl(actor+1616+4*j);v[j]=LeeFl(actor+208+4*j);longitud+=v[j]*v[j];}
  longitud=std::sqrt(longitud);if(!std::isfinite(longitud)||longitud<.0001f)return;
  for(unsigned j=0;j<3;++j){v[j]/=longitud;objetivo[j]=p[j]+60*v[j];}
  const uint32_t zdd=REX_LOAD_U32(kG+32),variante=REX_LOAD_U32(kG+64);
  if(!zdd||variante>=REX_LOAD_U32(zdd+4))return;
  const uint32_t ob=REX_LOAD_U32(REX_LOAD_U32(zdd+32)+4*variante);
  const uint32_t rn=REX_LOAD_U32(ob+0x98),rt=REX_LOAD_U32(ob+0xb0);
  auto seleccionada=[&](uint32_t k) {
    if(k>=rn)return false;
    const uint32_t r=rt+48*k,bone=REX_LOAD_U32(r+24),obj=REX_LOAD_U32(r+32);
    if(bone<0x10000000u||obj<0x10000000u)return false;
    const uint32_t indice=REX_LOAD_U32(bone),lineas=REX_LOAD_U32(bone+4);
    if(indice>512||lineas<0x10000000u)return false;
    const int estado=int32_t(REX_LOAD_U32(obj+8*(indice+1)));
    if(estado<0||estado>4096)return false;
    uint32_t cab=REX_LOAD_U32(lineas+16u*uint32_t(estado)+12);
    if(!cab)cab=REX_LOAD_U32(lineas+12);
    return cab==REX_LOAD_U32(r+16);
  };
  for(unsigned intento=0;intento<4;++intento) {
    std::array<float,3> a,b;
    for(unsigned j=0;j<3;++j){a[j]=p[j]-20*v[j];b[j]=objetivo[j]+20*v[j];}
    const auto hit=b2::Rayo(g_b2,variante,a,b,seleccionada);
    if(!hit.hay) {
      if(diagnostico) {
        const auto bruto=b2::Rayo(g_b2,variante,a,b);
        REXLOG_INFO("dbz3 B2 filtro: guest={} actor={:X} intento={} sin-contacto bruto={} registro={} entrada={} pos=({:.3f},{:.3f},{:.3f})",
                    cuadro_diagnostico,actor,intento,bruto.hay,bruto.registro,bruto.entrada,p[0],p[1],p[2]);
      }
      return;
    }
    float dot=0;for(unsigned j=0;j<3;++j)dot+=hit.normal[j]*v[j];
    if(dot>=0)return;
    // 1B6520 filtra &1 del MAS original; CA2C0 usa &2/&8 de HD.
    const float suelo=b2::AlturaSuelo(g_b2,variante,hit.punto[0],hit.punto[2],seleccionada);
    if(diagnostico)
      REXLOG_INFO("dbz3 B2 filtro: guest={} actor={:X} intento={} registro={} entrada={} NdotV={:.5f} suelo={:.3f} y={:.3f} py={:.3f}",
                  cuadro_diagnostico,actor,intento,hit.registro,hit.entrada,dot,suelo,hit.punto[1],p[1]);
    auto punto=hit.punto;
    if(punto[1]<suelo+9) punto[1]=suelo+9;
    else {
      const uint32_t rival=actor==mgr+2096?mgr+2096+8240:mgr+2096;
      const float dx=hit.punto[0]-LeeFl(rival+1616),dz=hit.punto[2]-LeeFl(rival+1624);
      const float tolerancia=std::max(0.0f,.6f*std::sqrt(dx*dx+dz*dz)-15);
      const float dy=p[1]-hit.punto[1];
      if(std::fabs(dy)<=tolerancia+5) {
        if(hit.entrada<0)return;
        const uint32_t frame=REX_LOAD_U32(0x8238D6B4);
        g_b2_contacto={mgr,actor,frame,hit.superficie,hit.entrada,hit.punto};
        REXLOG_INFO("dbz3 B2 consulta REAL: guest={} registro={} superficie={} entrada={} NdotV={:.5f} impacto=({:.3f},{:.3f},{:.3f})",
                    frame,hit.registro,hit.superficie,hit.entrada,dot,hit.punto[0],hit.punto[1],hit.punto[2]);
        REXLOG_INFO("dbz3 B2 lanzamiento pendiente: accion={} umbral={:.3f} tiempo={:.3f}",REX_LOAD_U16(actor+3244),LeeFl(actor+6192),LeeFl(actor+3256));
        return;
      }
      punto[1]=p[1]+(dy>0?-tolerancia:tolerancia);
    }
    float l=0;for(unsigned j=0;j<3;++j){v[j]=punto[j]-p[j];l+=v[j]*v[j];}
    l=std::sqrt(l);if(l<.0001f)return;
    for(unsigned j=0;j<3;++j){v[j]/=l;objetivo[j]=p[j]+60*v[j];}
  }
}

bool B2PasoMotor(PPCContext& ctx,uint8_t* base) {
  if(g_b2_motor.activo)return g_b2_motor.Paso(ctx,base);
  // Solo laboratorio hasta aceptar NIEVE; no activar una escena con nativas
  // pendientes por haber aprobado el lector o una cámara parcial.
  if(!NieveB2Activa()||!B2MotorOn())return false;
  if(!g_b2_motor.bloqueo.empty())return false; // un fallo medido, no relanzar cada contacto
  if(!dbz3::g_guest_eu&&!g_b2_motor.PrepararSolar(ctx,base)) {
    g_b2_motor.bloqueo="solar B2 inicial no disponible";
    REXLOG_WARN("dbz3 B2 HD: inicio bloqueado: {}",g_b2_motor.bloqueo);return false;
  }
  g_b2_motor.solar.EsperarFoto(base);
  if(g_b2_contacto.entrada<0||!g_b2_contacto.cuadro)return false;
  const auto c=g_b2_contacto;
  const uint16_t accion=REX_LOAD_U16(c.actor+3244);
  if(REX_LOAD_U32(kMgrPtr)!=c.gestor||(accion!=20&&accion!=22)){g_b2_contacto={};return false;}
   // No lanzar al consultar la pared: esperar el umbral de reacción. HD22
   // compara3256 en821062A8; HD20 avanza6196 en82105CFC y cambia a21
   // antes de la comprobación global (que ya vería el reloj reiniciado).
   const float tiempo=LeeFl(c.actor+(accion==20?6196:3256));
   if(!(LeeFl(c.actor+6192)<tiempo))return false;
  g_b2_contacto={};
  if(g_esc.activa)TerminarEscena("VM B2 toma el control");
  if(!g_b2_motor.Iniciar(ctx,base,c))return false;
   return g_b2_motor.Paso(ctx,base);
}

bool B2ConsumirAntesDeReaccion(PPCContext& ctx,uint8_t* base) {
  if(dbz3::g_guest_eu||!NieveB2Activa()||!B2MotorOn()||
     g_b2_motor.activo||g_b2_contacto.entrada<0||g_b2_contacto.actor!=ctx.r3.u32||
     ctx.r4.u32!=21||REX_LOAD_U16(ctx.r3.u32+3244)!=20)return false;
  // 82105EC8: la reacción20 ya ha actualizado su movimiento y6196, pero
  // 82103908 aún no ha sustituido la acción ni reiniciado3256. Solo un
  // contacto real pendiente y el umbral superado permiten tomar el control.
  if(!(LeeFl(ctx.r3.u32+6192)<LeeFl(ctx.r3.u32+6196)))return false;
  REXLOG_INFO("dbz3 B2 consume pendiente: guest={} accion=20 siguiente=21 umbral={:.3f} tiempo={:.3f}",
              REX_LOAD_U32(0x8238D6B4),LeeFl(ctx.r3.u32+6192),LeeFl(ctx.r3.u32+6196));
  return B2PasoMotor(ctx,base);
}
