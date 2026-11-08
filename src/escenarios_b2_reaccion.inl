  // Experimento de la reacción PAL18 (1DACA0/1DA7F0), no una congelación
  // de posición/estado. El evento B2 vive en nuestra VM: no se falsifica el
  // evento del gestor HD para saltar su rama de distancia de combate normal.
  bool reaccion_pal=false;
  void PrepararReaccion() {
    const char* s=std::getenv("DBZ3_TEST_B2_REACCION_PAL");
    reaccion_pal=((s&&!std::strcmp(s,"1"))||::dbz3::original::Arreglo("levantar_b2"))&&(g_b2.grupo==7||g_b2.grupo==8||g_b2.grupo==4||g_b2.grupo==0);   // eB2h: Llanura (4) y Base (0): misma fisica de reaccion PAL
  }
  static bool AnimB2Activa() {
    static const bool v=[]{const char* e=std::getenv("DBZ3_TEST_B2_ANIM_B2");return e&&*e=='1';}();
    return v;
  }
  bool PasoReaccionOriginal(PPCContext& ctx,uint8_t* b) {
    const uint32_t x=ctx.r3.u32;
    if(!reaccion_pal||!activo||!escena||dbz3::g_guest_eu||b!=base||
       REX_LOAD_U32(kMgrPtr)!=mgr||(x!=actor[0]&&x!=actor[1])||
       REX_LOAD_U16(x+3244)!=20||REX_LOAD_U8(x+6208)>1)return false;
    const float umbral=LeeFl(x+6192),tiempo=LeeFl(x+6196);
    const float delta=LeeFl(mgr+35120)*LeeFl(x+3228);
    if(!std::isfinite(umbral)||umbral<=0||!std::isfinite(tiempo)||
       !std::isfinite(delta)||delta<0)return false;
    MedirActores1450("reaccion_pal_antes",nullptr,x,uint32_t(ctx.lr));
    const float ratio=tiempo/umbral,cuadrado=ratio*ratio;
    // PAL mantiene el reloj real y usa 1-ratio^4, mínimo0,35 en altura.
    const float altura=LeeFl(x+68)-LeeFl(x+6216);
    float avance=1;
    if(REX_LOAD_U8(x+6209)!=1&&altura>15)
      avance=std::max(.35f,1-cuadrado*cuadrado);
    const float paso=avance*delta;
    auto llamar=[&](auto fn,int32_t r4) {
      PPCContext c=ctx;c.r3.u64=x;c.r4.s64=r4;fn(c,b);
    };
    bool congelada=false;
    if(AnimB2Activa()) {
      // DBZ3_TEST_B2_ANIM_B2 (apagado): PAL mantiene la ultima pose del clip de
      // reaccion B2; el motor HD envuelve el reloj al llegar a nf. Al llegar al
      // ultimo cuadro no se vuelve a reproducir: las matrices quedan en esa pose.
      const uint32_t mod=REX_LOAD_U32(x+2952);
      if(mod>=0x10000000u&&mod<0xeffff000u) {
        const uint32_t acm=REX_LOAD_U32(mod+0x46c),clip=REX_LOAD_U32(mod+1052)&0xffffu;
        for(const auto& bk:anim.bancos) if(bk.acm==acm&&clip>=bk.primer&&clip<bk.primer+uint32_t(std::size(B2AnimHD::mapas))) {
          const uint32_t tabla=REX_LOAD_U32(acm+20),nf=REX_LOAD_U32((bk.tabla<0x10000000u?acm+tabla:tabla)+16*clip+8);
          if(nf>1&&LeeFl(mod+1060)>=float(nf-1))congelada=true;
        }
      }
    }
    if(!congelada)llamar(sub_821114B8,0); // reproducción CSK, homóloga de1F6010
    Fl(b,x+6184,LeeFl(x+6180)*(1-cuadrado));
    for(unsigned j=0;j<3;++j)
      Fl(b,x+64+4*j,LeeFl(x+64+4*j)+LeeFl(x+208+4*j)*paso);
    llamar(sub_82110AB8,-1); //1F68C0: colisión/postura, no omitirla
    llamar(sub_82114568,0); //1F4AF0: suelo real
    const float suelo=LeeFl(x+6216),limite=suelo+LeeFl(x+6184);
    if(REX_LOAD_U8(x+6209)!=1&&LeeFl(x+68)-suelo<=15)
      Fl(b,x+68,std::min(LeeFl(x+68),limite));
    if(LeeFl(x+68)<=suelo+2||LeeFl(x+1652)<=suelo+2) {
      Fl(b,x+68,suelo);
      if(ratio>=.6f) {
        for(unsigned j=0;j<3;++j)Fl(b,x+208+4*j,LeeFl(x+208+4*j)*.75f);
        llamar(sub_82103908,17); // PAL39355C: aterrizaje, misma rama nativa
      } else llamar(sub_82103908,23); //PAL393570
    }
    // La segunda condición PAL exige evento==0: durante esta VM no procede.
    // No se inventa una distancia menor ni se escribe el evento HD35204.
    if(umbral-tiempo<.5f&&LeeFl(x+68)-suelo>15) {
      REX_STORE_U8(x+8185,1);
      if(int8_t(REX_LOAD_U8(x+8184))<0) {
        REX_STORE_U16(x+3252,1);REX_STORE_U16(x+6178,0);
        Fl(b,x+6164,0);Fl(b,x+6168,0);
        llamar(sub_82103908,16); //PAL393558: caída; sin teletransportar
      } else llamar(sub_82103908,21); //PAL39359C: reacción aérea
    }
    Fl(b,x+6196,tiempo+delta); //mismo orden que PAL1DA7F0, no reloj prefijado
    MedirActores1450("reaccion_pal_despues",nullptr,x,uint32_t(ctx.lr));
    return true;
  }
  bool OrdenReaccion(const b2::Orden& o) {
    const auto& a=o.argumentos;
    if(o.cuenta<4||a[3]>=2)return false;
    if(!reaccion_pal)return Llamar(sub_8211D108,o);
    // La equivalencia solo está contrastada para selector0/1. El CSK temporal
    // selecciona el descriptor B2 original al iniciar, sin cambiar el código
    // de estado ni dejar un alias que afecte a otra reacción HD posterior.
    if(a[2]>1){bloqueo="reaccion PAL: selector no contrastado";return false;}
    const uint32_t x=actor[a[3]],csk=REX_LOAD_U32(x+0x650+0x2c0);
    const auto it=std::find_if(anim.cuerpos.begin(),anim.cuerpos.end(),
                             [&](const auto& c){return c.nuevo==csk;});
    if(it==anim.cuerpos.end())return false;
    const uint32_t tabla=REX_LOAD_U32(csk+20),n=REX_LOAD_U32(csk+16);
    const uint32_t hd=154+a[2],pal=152+a[2];
    if(n<=hd||tabla<0x10000000u)return false;
    const uint32_t anterior=REX_LOAD_U32(tabla+4*hd),descriptor=REX_LOAD_U32(tabla+4*pal);
    if(!anterior||!descriptor)return false;
    REX_STORE_U32(tabla+4*hd,descriptor);
    const bool ok=Llamar(sub_8211D108,o);
    REX_STORE_U32(tabla+4*hd,anterior);
    REXLOG_INFO("dbz3 B2 reaccion PAL: lado={} descriptor {}->{} restaurado={} umbral={:.6f}",
                a[3],hd,pal,REX_LOAD_U32(tabla+4*hd)==anterior,LeeFl(x+6192));
    return ok;
  }
