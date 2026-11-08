// Adaptación de las pistas B2 al almacén global HD. Mismo método validado de
// anim_iw.cpp::Bloque/InstalarCuerpo: sufijos de hueso, sin sustituir geometría.
struct B2AnimHD {
  struct Mapa { uint32_t codigo, banco, clip, ranura; };   // ranura: ranura CSK de HD si no es 'codigo' (indice del descriptor BSK B2); 0 = la misma
  // Medida del builtin11 en PCSX2 propio gp1, resolver_anim_vm.log. Los índices
  // NO se deducen por proximidad numérica a una animación de B3.
  static constexpr Mapa mapas[] = {{1285,0,167,0},{1286,0,168,0},{1287,0,169,0},{1288,0,170,0},
      {1289,0,171,0},{1290,0,172,0},{1291,0,173,0},{265,1,28,0},{520,1,74,0},{152,0,118,0},
      {160,0,137,0},{256,1,19,0},{36,0,107,0},{40,0,99,0},{137,0,94,0},
      // Segunda consulta del builtin11 original: resolver_reacciones_b2.log.
      //158/159/173..175 vuelven a estado0: NO se inventan pistas para ellos.
      {144,0,118,0},{146,0,118,0},{147,0,118,0},{153,0,119,0},{154,0,120,0},
      {155,0,121,0},{156,0,122,0},{157,0,240,0},{168,0,129,0},{169,0,130,0},
      {170,0,131,0},{171,0,131,0},{172,0,132,0},
      // eB2f: reposo B2 del atacante (descriptor 0 = banco1 clip0 = 0x20000 de PAL). Solo se instala con
      // DBZ3_TEST_B2_IDLE_B2=1 (ver IdleB2/Salta): sin el interruptor el cuerpo conserva su reposo B3.
      {0,1,0,0},
      // eB2g: la victima tras el vuelo. Los estados los elige el codigo nativo (medido: HD 141/166/42 llegan a los mismos
      // relojes 277/295/311 que PAL 137/168/40; el aterrizaje largo es el 44/accion 35, el 141 dura 1 cuadro); solo cambia el clip: ranura HD <- descriptor B2. DBZ3_TEST_B2_LEVANTAR_B2=1.
      {137,0,94,141},{137,0,94,44},{168,0,129,166},{40,0,99,42},
      // eB2h: Llanura, 2a fase del levantarse (PAL 176 = clip 133; en HD sigue al 166 el estado 172). DBZ3_TEST_B2_LEVANTAR_B2=1.
      {176,0,133,172},
      // eB2g: golpe del atacante. PAL codigo 573 = BSK 573 (banco1 clip 98 = 0x20062); en HD el golpe es el codigo 574. Ranura 574 (ver ForzarAtaque).
      {573,1,98,574},
      // eB2h: Base (captura PAL con lanzamiento 6/11). PAL: 152 (clip 118, mantiene la ultima pose) -> 137 (clip 94) -> 40; HD pasa por 157 -> 163 -> 256
      // (vuelo) y 131 -> 38 (aterrizaje). Solo clip (ranura HD <- descriptor B2); el flujo de estados lo sigue eligiendo el codigo nativo. LEVANTAR_B2=1.
      {152,0,118,157},{152,0,118,163},{152,0,118,256},{137,0,94,131},{137,0,94,38}};
  static bool IdleB2() {
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_IDLE_B2");return (e&&*e=='1')||::dbz3::original::Arreglo("idle_b2");}();
    return v;
  }
  static bool LevantarB2() {
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_LEVANTAR_B2");return (e&&*e=='1')||::dbz3::original::Arreglo("levantar_b2");}();
    return v;
  }
  static bool AtaqueB2() {
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_ATAQUE_B2");return (e&&*e=='1')||::dbz3::original::Arreglo("ataque_b2");}();
    return v;
  }
  static bool CapaB2() {
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_CAPA_B2");return (e&&*e=='1')||::dbz3::original::Arreglo("capa_b2");}();
    return v;
  }
  static float DesfaseAtaque() {   // eB2h: tiempo ABSOLUTO de arranque del clip B2 98 (medido: el golpe nativo arranca en 23,0 en Kami/Ciudad pero en 24,0 en Llanura; 25,7 = 23+2,7 de eB2g; env solo para el barrido)
    static const float v=[]{const char* e=std::getenv("DBZ3_TEST_B2_ATAQUE_DT");return e?std::strtof(e,nullptr):25.7f;}();
    return v;
  }
  static uint32_t Ranura(const Mapa& m) {return m.ranura?m.ranura:m.codigo;}
  // eB2h: las ranuras 157/163/256/131/38 son del vuelo y aterrizaje nativos HD de la Base (grupo 0); en Nieve la 256 es el clip B2 19 de PAL.
  static bool SoloBase(const Mapa& m) {return m.ranura==157||m.ranura==163||m.ranura==256||m.ranura==131||m.ranura==38;}
  static bool Salta(const Mapa& m) {return (m.codigo==0&&!IdleB2())||(m.ranura&&!(m.ranura==574?AtaqueB2():LevantarB2()))||(m.ranura&&SoloBase(m)&&g_b2.grupo!=0);}
  struct Banco { uint32_t acm=0, n=0, tabla=0, nueva=0, primer=0; bool rel=false; };
  struct Cuerpo { uint32_t a=0, antes=0, nuevo=0; };
  std::vector<Banco> bancos;
  std::vector<Cuerpo> cuerpos;
  std::array<std::array<uint32_t,8>,std::size(mapas)> metadatos{};
  std::array<uint32_t,std::size(mapas)> tablas_ap{},cuentas_ap{};
  uint32_t fuentes[2] = {};
  uint32_t Reservar(uint8_t* base, uint32_t n) {
    const uint32_t p = g_mem ? g_mem->SystemHeapAlloc(n, 32) : 0;
    if (p) std::memset(base+p,0,n);
    return p;
  }
  static uint32_t Ptr(uint32_t a,uint32_t v) { return v && v<0x10000000u ? a+v : v; }
  static std::string Sufijo(uint8_t* base,uint32_t p) {
    std::string n;
    for(unsigned i=0;i<32;++i) { const char c=char(REX_LOAD_U8(p+i));if(!c)break;n+=c; }
    const size_t j=n.find('_');return j==std::string::npos ? n : n.substr(j+1);
  }
  bool Preparar(uint8_t* base,const b2::Recurso& r) {
    const auto bsk=r.Bloque(11);
    if(!bsk.Rango(0,32)||std::memcmp(bsk.p,"#BSK",4))return false;
    const uint32_t n=bsk.U(16),tabla=bsk.U(20);
    if(!n||n>4096||!bsk.Rango(tabla,4u*n))return false;
    std::vector<uint8_t> sin_impactos(bsk.p,bsk.p+bsk.n);
    for(size_t i=0;i<std::size(mapas);++i) {
      const auto& m=mapas[i];if(Salta(m))continue;if(m.codigo>=n)return false;
      const uint32_t p=bsk.U(tabla+4*m.codigo);
      if(!p||!bsk.Rango(p,48)||(bsk.U(p)&65535)!=m.clip||(bsk.U(p)>>16)!=m.banco)return false;
      for(unsigned j=0;j<8;++j)metadatos[i][j]=bsk.U(p+4+4*j);
      // eB2i: desc+0x1C (metadatos[6]) es la capa superpuesta B2 (banco 3, FUN_001f4b40 -> actor+0x9d8, modo 1). PAL solo la aplica si el
      // indice es menor que el numero de clips del banco 3; el de Goku (AFS 175 hijo 1, volcado de la RAM PAL) tiene 6, y la tabla usa 10/32
      // (1285..1291): en PAL no se aplica (pose A = clip, medido 0,1 grados en 1290). HD lee la misma palabra como 0x80000|i (+1580) y SI
      // aplica la capa B3 nativa 0x8000a/0x80020 (los 49 grados de Nieve 1290 y los 30-60 de Ciudad/Base 1286/1287). DBZ3_TEST_B2_CAPA_B2=1.
      // ponytail: 6 clips medidos con Goku; con otro personaje leer el banco 3 de su AFS.
      if(CapaB2()&&metadatos[i][6]!=0xffffffffu) {
        if(metadatos[i][6]>=6u)metadatos[i][6]=0xffffffffu;
        else REXLOG_WARN("dbz3 B2 movimientos: codigo {} con capa B2 valida {} (no portada)",m.codigo,metadatos[i][6]);
      }
      const uint32_t np=bsk.U(p+40),tp=bsk.U(p+44);
      if(np>64||!bsk.Rango(tp,8u*np))return false;
      for(uint32_t j=0;j<np;++j) {
        const uint32_t q=tp+8*j;
        // AP1 referencia HR/impactos de B2; AP7 son eventos propios. El
        // conversor existente excluye7: marcar1como7 SOLOenesta copiafiltrada.
        if(uint16_t(bsk.p[q]|uint16_t(bsk.p[q+1])<<8)==1) {
          sin_impactos[q]=7;sin_impactos[q+1]=0;
        }
      }
    }
    for(size_t i=0;i<std::size(mapas);++i) {
      if(Salta(mapas[i]))continue;
      dbz3::anim::ap::Pistas ap;
      if(!dbz3::anim::ap::Convertir(sin_impactos,bsk.U(tabla+4*mapas[i].codigo),0,ap)||!ap.hr.empty())return false;
      cuentas_ap[i]=ap.n;
      tablas_ap[i]=dbz3::anim::ap::Instalar(ap,
          [&](uint32_t bytes){return Reservar(base,bytes);},
          [&](uint32_t p,uint8_t v){REX_STORE_U8(p,v);});
      if(ap.n&&!tablas_ap[i])return false;
    }
    for(unsigned k=0;k<2;++k) if(!fuentes[k]) {
      const auto v=r.Bloque(8+k);
      if(v.n<32||v.U(24,true)!=42)return false;
      fuentes[k]=Reservar(base,uint32_t(v.n));if(!fuentes[k])return false;
      std::memcpy(base+fuentes[k],v.p,v.n);
    }
    uint32_t total_ap=0;for(const uint32_t n:cuentas_ap)total_ap+=n;
    REXLOG_INFO("dbz3 B2 movimientos: {} descriptores originales, {} grupos AP0/2..6; AP1/7 pendientes",std::size(mapas),total_ap);
    return true;
  }
  bool Instalar(uint8_t* base,uint32_t x) {
    auto fallo=[&](const char* motivo,uint32_t p,uint32_t q=0) {
      REXLOG_WARN("dbz3 B2 movimientos: cuerpo {:08X} sin vincular: {} ({:08X}, {:08X})",x,motivo,p,q);
      return false;
    };
    const uint32_t a=x+0x650,mod=REX_LOAD_U32(a+0x538);
    if(mod<0x10000000u)return fallo("modelo",mod);
    const uint32_t acm=REX_LOAD_U32(mod+0x46c);
    if(acm<0x10000000u||(REX_LOAD_U32(acm)&0xffffffu)!=0x41434du)return fallo("ACM",acm);
    Banco* banco=nullptr;
    for(auto& b:bancos)if(b.acm==acm)banco=&b;
    if(!banco) {
      Banco b;b.acm=acm;b.n=REX_LOAD_U32(acm+16);b.tabla=REX_LOAD_U32(acm+20);b.primer=b.n;
      const uint32_t nb=REX_LOAD_U32(acm+24),names=Ptr(acm,REX_LOAD_U32(acm+28));
      if(!nb||nb>128||!b.n||b.n>4096)return false;
      const uint32_t viejo=Ptr(acm,REX_LOAD_U32(Ptr(acm,b.tabla)+12));
      b.rel=false;
      for(uint32_t j=0;j<nb*2;++j)if(const uint32_t p=REX_LOAD_U32(viejo+4*j)) {b.rel=p<0x10000000u;break;}
      const uint32_t count=uint32_t(std::size(mapas));
      b.nueva=Reservar(base,16*(b.n+count));if(!b.nueva)return false;
      std::memcpy(base+b.nueva,base+Ptr(acm,b.tabla),16*b.n);
      for(uint32_t i=0;i<count;++i) {
        const Mapa& m=mapas[i];if(Salta(m))continue;const uint32_t src=fuentes[m.banco];
        if(!src||m.clip>=REX_LOAD_U32(src+16))return false;
        const uint32_t st=src+REX_LOAD_U32(src+20)+16*m.clip,sb=src+REX_LOAD_U32(st+12);
        const uint32_t sn=REX_LOAD_U32(src+24),names_src=src+REX_LOAD_U32(src+28);
        const uint32_t block=Reservar(base,8*nb);if(!block)return false;
        for(uint32_t j=0;j<nb;++j) {
          const auto nom=Sufijo(base,names+32*j);
          uint32_t k=0;while(k<sn&&Sufijo(base,names_src+32*k)!=nom)++k;
          if(k==sn)continue;
          for(uint32_t z=0;z<2;++z) if(const uint32_t rel=REX_LOAD_U32(sb+4*(2*k+z))) {
            const uint32_t ptr=src+rel;REX_STORE_U32(block+4*(2*j+z),b.rel ? ptr-acm : ptr);
          }
        }
        const uint32_t d=b.nueva+16*(b.n+i);
        for(uint32_t j=0;j<3;++j)REX_STORE_U32(d+4*j,REX_LOAD_U32(st+4*j));
        REX_STORE_U32(d+12,b.rel ? block-acm : block);
      }
      REX_STORE_U32(acm+16,b.n+count);
      REX_STORE_U32(acm+20,b.tabla<0x10000000u ? b.nueva-acm : b.nueva);
      bancos.push_back(b);banco=&bancos.back();
      REXLOG_INFO("dbz3 B2 movimientos: pistas originales añadidas al ACM HD {:08X}, {}..{}",acm,b.primer,b.primer+count-1);
    }
    const uint32_t prev=REX_LOAD_U32(a+0x2c0);
    // Como InstalarCuerpo de anim_iw: el primer hueco puede estar vacío;
    // se crea ahí el CSK privado sin perder los otros dos bancos del cuerpo.
    if(prev&&(prev<0x10000000u||prev>=0xf0000000u))return fallo("CSK",prev);
    const uint32_t n0=prev?REX_LOAD_U32(prev+16):0,t0=prev?Ptr(prev,REX_LOAD_U32(prev+20)):0;
    if(n0>4096||(n0&&(t0<0x10000000u||t0>=0xf0000000u)))return fallo("tabla CSK",n0,t0);
    const uint32_t n=std::max(n0,1292u),count=uint32_t(std::size(mapas));
    const uint32_t csk=Reservar(base,32+4*n+48*count);if(!csk)return false;
    if(prev)std::memcpy(base+csk,base+prev,32);
    else {REX_STORE_U32(csk+4,32);REX_STORE_U32(csk+12,4);}
    REX_STORE_U32(csk,0x2143534b);REX_STORE_U32(csk+16,n);REX_STORE_U32(csk+20,csk+32);
    if(prev)REX_STORE_U32(csk+28,Ptr(prev,REX_LOAD_U32(prev+28)));
    for(uint32_t i=0;i<n0;++i) {
      const uint32_t v=REX_LOAD_U32(t0+4*i);REX_STORE_U32(csk+32+4*i,Ptr(prev,v));
    }
    for(uint32_t i=0;i<count;++i) {
      if(Salta(mapas[i]))continue;
      const uint32_t bl=csk+32+4*n+48*i;
      REX_STORE_U16(bl,uint16_t(banco->primer+i));REX_STORE_U16(bl+2,0);
      // Misma cabecera escalar de reproducción del BSK original: no sustituir
      // bucles/centinelas por0. AP0/2..6 usan el conversor existente; AP1/7
      // siguen pendientes, sin ejecutar HR/funciones ajenas de B3.
      for(unsigned j=0;j<8;++j)REX_STORE_U32(bl+4+4*j,metadatos[i][j]);
      REX_STORE_U32(bl+40,cuentas_ap[i]);REX_STORE_U32(bl+44,tablas_ap[i]);
      REX_STORE_U32(csk+32+4*Ranura(mapas[i]),bl);
    }
    REX_STORE_U32(a+0x2c0,csk);cuerpos.push_back({a,prev,csk});
    REXLOG_INFO("dbz3 B2 movimientos: CSK privado cuerpo={:08X} anterior={:08X} n={} nuevo={:08X}",x,prev,n0,csk);
    return true;
  }
  // eB2g: medido con un vigia de escritura (tools/solb2x_lanzamiento.py EB2G_WATCH): el id de clip del cuerpo lo escribe cada cuadro
  // sub_820FAF40(controlador x+0x650) con sub_820B0EE8: primero el campo +1540 y, si +1580>=0, 0x80000|+1580 (el clip nativo 0x80002 del
  // golpe). Aqui se pone +1580=-1 y +1540=clip B2 (ranura 574 del CSK privado) mientras dure el golpe (codigo 574).
  // eB2h: aterrizaje de la victima. PAL reproduce el clip B2 94 (estado 137) de corrido y luego mantiene la ultima pose; HD pasa por 141 (1-2 cuadros) y
  // 44 (15 cuadros), y 44 arranca el clip de nuevo (en Llanura dos veces). Se escribe el cuadro de animacion del actor (x+3160, el que lee la reproduccion CSK;
  // mod+1060 no basta, medido) como cuadros desde el primer 44/38 mas 1,1 (optimo medido: Ciudad 0,6, Base 1,1, Llanura 1,6-2,0; los 2 cuadros de 141 varian por escena), parado en el ultimo cuadro.
  double aterr_ini=-1;uint32_t aterr_prev=0;int aterr_run=0;
  static float AterrizajeDt() {
    static const float v=[]{const char* e=std::getenv("DBZ3_TEST_B2_ATERRIZAJE_DT");return e?std::strtof(e,nullptr):1.1f;}();
    return v;
  }
  void Aterrizar(uint8_t* base,uint32_t x,double cuadro) {
    if(!LevantarB2())return;
    const uint32_t cod=REX_LOAD_U32(x+2928);
    if(cod==141||cod==131) {if(aterr_ini<0||cuadro<aterr_ini){aterr_ini=cuadro;aterr_run=0;}aterr_prev=cod;return;}   // 141/44 (Ciudad, Llanura) y 131/38 (Base)
    if((cod!=44&&cod!=38)||aterr_ini<0) {if(cod!=44&&cod!=38)aterr_ini=-1;aterr_prev=cod;return;}
    // eB2j DBZ3_TEST_B2_REBOTE_RELOJ=1: el clip B2 94 de PAL se repite (137 = 2 vueltas de ~16 relojes; HD 141/44 dos veces, medido: la 2a tanda de HD
    // coincide con la 2a vuelta de PAL con error 0,5): la 2a tanda recibe el mismo tratamiento de reloj (ancla propia) en vez del reloj nativo.
    static const bool reloj2=[]{const char* e=std::getenv("DBZ3_TEST_B2_REBOTE_RELOJ");return e&&*e=='1';}();
    if(aterr_prev!=44&&aterr_prev!=38) {if(++aterr_run==1||(reloj2&&cod==44))aterr_ini=cuadro;}   // ancla: primer cuadro del 44/38 (anclar en el 141 dejaba 2 cuadros de diferencia entre escenas)
    aterr_prev=cod;
    if(aterr_run>1&&!(reloj2&&cod==44))return;   // en la Base (38) la 2a tanda sigue nativa: con el reloj propio bajo de 188 a 162   // solo la 1a tanda 44/38: la 2a (rebote) la deja el codigo nativo, que sale del estado cuando el reloj propio llega al final del clip
    const uint32_t c=x+0x650,mod=REX_LOAD_U32(c+0x538);
    if(mod<0x10000000u||mod>=0xeffff000u)return;
    const uint32_t acm=REX_LOAD_U32(mod+0x46c),id=REX_LOAD_U32(mod+1052);
    for(const auto& b:bancos) if(b.acm==acm&&(id>>16)==1&&(id&0xffffu)>=b.primer&&(id&0xffffu)<b.primer+uint32_t(std::size(mapas))) {
      if(mapas[(id&0xffffu)-b.primer].ranura!=cod)return;
      const uint32_t tb=REX_LOAD_U32(acm+20),nf=REX_LOAD_U32((b.tabla<0x10000000u?acm+tb:tb)+16*(id&0xffffu)+8);
      // tope nf-1,5: con nf-1 el codigo nativo da el clip por terminado y sale del estado antes de tiempo (medido en la Base)
      float t=std::max(0.0f,std::min(float(cuadro-aterr_ini)+AterrizajeDt(),float(nf>2?nf-1.5:0)));uint32_t u;std::memcpy(&u,&t,4);
      REX_STORE_U32(x+3160,u);return;
    }
  }
  // eB2h Base: el codigo nativo HD termina el vuelo 9 cuadros antes que PAL y pasa al reposo (0) tanto ahi como al final (PAL: 152 mantiene la pose de
  // vuelo hasta 144 y despues 40 = tumbado hasta el fin). Con la escena activa y la victima en 0 se fuerza el clip como en ForzarAtaque
  // (+1580=-1, +1540=id): vuelo (clip 118 en su ultima pose) o tumbado (clip 99). LEVANTAR_B2=1.
  int base_fase=0;
  void BaseVictima(uint8_t* base,uint32_t x) {
    if(!LevantarB2()||g_b2.grupo!=0)return;
    const uint32_t cod=REX_LOAD_U32(x+2928);
    if(cod==157||cod==163||cod==256) {base_fase=1;return;}
    if((cod==131||cod==38)&&base_fase>=1) {base_fase=2;return;}
    if(cod!=0||base_fase==0)return;
    const uint32_t c=x+0x650,mod=REX_LOAD_U32(c+0x538);
    if(mod<0x10000000u||mod>=0xeffff000u)return;
    const uint32_t acm=REX_LOAD_U32(mod+0x46c);
    const uint32_t ranura=base_fase==1?256:42;
    for(const auto& b:bancos) if(b.acm==acm) for(size_t i=0;i<std::size(mapas);++i) if(mapas[i].ranura==ranura) {
      const uint32_t id=0x10000u|uint32_t(b.primer+i),tb=REX_LOAD_U32(acm+20);
      const uint32_t nf=REX_LOAD_U32((b.tabla<0x10000000u?acm+tb:tb)+16*(b.primer+uint32_t(i))+8);
      float t=0;   // probado arrancar el tumbado adelantado (2,5 y 4,5): sin efecto en la pose, la caida de los primeros cuadros no sigue este tiempo
      if(base_fase==1&&nf>1)t=float(nf-1);
      if(REX_LOAD_U32(c+1580)!=0xFFFFFFFFu||REX_LOAD_U32(c+1540)!=id) {
        uint32_t u;std::memcpy(&u,&t,4);REX_STORE_U32(c+1544,u);
        // eB2i: PAL entra en 40 (tumbado) con f=1 (actor+0x9f8, sin fundido; trazas g0/g7/g8). El 0 nativo de HD funde desde la pose
        // anterior (+1620, ritmo +1624; recomp.7 ~7160): era la caida de 63 -> 5 grados en 20 cuadros de la Base. DBZ3_TEST_B2_CAIDA_PAL=1.
        static const bool caida=[]{const char* e=std::getenv("DBZ3_TEST_B2_CAIDA_PAL");return (e&&*e=='1')||::dbz3::original::Arreglo("caida_pal");}();
        if(caida){const float f=1.0f;std::memcpy(&u,&f,4);REX_STORE_U32(c+1620,u);}
      }
      REX_STORE_U32(c+1580,0xFFFFFFFFu);REX_STORE_U32(c+1540,id);
      if(base_fase==1) {uint32_t u;std::memcpy(&u,&t,4);REX_STORE_U32(x+3160,u);}
      return;
    }
  }
  // eB2h: PAL mantiene la ultima pose del clip 118 (10 cuadros) durante todo el vuelo (144 relojes); HD envuelve el reloj. Se limita el cuadro de
  // animacion del actor (x+3160, el que lee la reproduccion CSK; mod+1060 no basta, medido) a nf-1. Solo estados 157/163/256 y con LEVANTAR_B2.
  void Retener(uint8_t* base,uint32_t x) {
    if(!LevantarB2()||g_b2.grupo!=0)return;
    const uint32_t cod=REX_LOAD_U32(x+2928);
    if(cod!=157&&cod!=163&&cod!=256)return;
    const uint32_t c=x+0x650,mod=REX_LOAD_U32(c+0x538);
    if(mod<0x10000000u||mod>=0xeffff000u)return;
    const uint32_t acm=REX_LOAD_U32(mod+0x46c),id=REX_LOAD_U32(mod+1052);
    for(const auto& b:bancos) if(b.acm==acm&&(id>>16)==1&&(id&0xffffu)>=b.primer&&(id&0xffffu)<b.primer+uint32_t(std::size(mapas))) {
      const uint32_t i=(id&0xffffu)-b.primer;
      if(mapas[i].clip!=118)return;
      const uint32_t tb=REX_LOAD_U32(acm+20),nf=REX_LOAD_U32((b.tabla<0x10000000u?acm+tb:tb)+16*(id&0xffffu)+8);
      if(nf>1&&dbz3::escenarios::LeeFl(x+3160)>float(nf-1)) {float t=float(nf-1);uint32_t u;std::memcpy(&u,&t,4);REX_STORE_U32(x+3160,u);}
      return;
    }
  }
  bool atq_primero=false;   // eB2i: ForzarAtaque acaba de cambiar el clip (el motor puede rehacer la pose en el mismo cuadro)
  static bool PoseYa() {   // eB2i DBZ3_TEST_B2_POSE_YA=1: rehace la pose (sub_820FAF40) en el cuadro en que se fuerza el clip B2 del golpe
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_POSE_YA");return (e&&*e=='1')||::dbz3::original::Arreglo("pose_ya");}();
    return v;
  }
  bool ForzarAtaque(uint8_t* base,uint32_t x) {
    atq_primero=false;
    if(!AtaqueB2()||REX_LOAD_U32(x+2928)!=574)return false;
    const uint32_t c=x+0x650,mod=REX_LOAD_U32(c+0x538);
    if(mod<0x10000000u||mod>=0xeffff000u)return false;
    const uint32_t acm=REX_LOAD_U32(mod+0x46c);
    for(const auto& b:bancos) if(b.acm==acm) for(size_t i=0;i<std::size(mapas);++i) if(mapas[i].ranura==574) {
      const uint32_t id=0x10000u|uint32_t(b.primer+i);
      static unsigned n=0;
      if(n++<3)REXLOG_INFO("dbz3 B2 ataque: controlador +1540={:X} +1580={:X} clip cuerpo {:X} -> {:X}",REX_LOAD_U32(c+1540),REX_LOAD_U32(c+1580),REX_LOAD_U32(mod+1052),id);
      atq_primero=REX_LOAD_U32(c+1580)!=0xFFFFFFFFu;
      if(atq_primero) {   // primer cuadro: tiempo de arranque absoluto del clip B2 98 (medido contra PAL en 4 escenas)
        float t=DesfaseAtaque();
        // eB2j DBZ3_TEST_B2_ATAQUE_REL=<k>: el arranque sale del propio juego: tiempo del clip nativo del golpe al empezar la escena (el golpe
        // empezo antes de la colision, medido 23,0/24,0) + k, en vez del 25,7 fijo. Sin la variable no cambia nada.
        static const char* rel=std::getenv("DBZ3_TEST_B2_ATAQUE_REL");
        const float nat=dbz3::escenarios::LeeFl(mod+1060);
        if(rel&&*rel)t=nat+std::strtof(rel,nullptr);
        REXLOG_INFO("dbz3 B2 ataque: tiempo nativo {:.3f} (x+3160 {:.3f}) -> arranque {:.3f}",nat,dbz3::escenarios::LeeFl(x+3160),t);
        uint32_t u;std::memcpy(&u,&t,4);REX_STORE_U32(c+1544,u);   // +1544 = tiempo de arranque que 820FAF40 copia a mod+1060 al cambiar de clip
      }
      REX_STORE_U32(c+1580,0xFFFFFFFFu);
      REX_STORE_U32(c+1540,id);
      return true;
    }
    return false;
  }
  void Restaurar(uint8_t* base) {
    for(const auto& c:cuerpos)if(REX_LOAD_U32(c.a+0x2c0)==c.nuevo)REX_STORE_U32(c.a+0x2c0,c.antes);
    for(const auto& b:bancos)if(Ptr(b.acm,REX_LOAD_U32(b.acm+20))==b.nueva) {
      REX_STORE_U32(b.acm+16,b.n);REX_STORE_U32(b.acm+20,b.tabla);
    }
    cuerpos.clear();bancos.clear();
  }
};
