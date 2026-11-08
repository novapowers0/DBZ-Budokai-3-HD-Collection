  // Experimento optativo: C1 PAL1F9850 -> HD8211D6F0. La aplicación queda en
  // 82114B40, DESPUÉS de actualizar el modelo; no se congela el ajuste al suelo.
  bool trayectoria_nativa=false;
  uint32_t trayectoria_acc=0;
  int trayectoria_grupo=-1;
  std::array<std::array<uint8_t,18>,2> trayectoria_antes{};
  bool PrepararTrayectoria(PPCContext& ctx) {
    const char* s=std::getenv("DBZ3_TEST_B2_TRAYECTORIA_NATIVA");
    trayectoria_nativa=false;
    if(!s||std::strcmp(s,"1")||(g_b2.grupo!=7&&g_b2.grupo!=8))return true;
    const auto v=g_b2.Bloque(1);
    if(!v.Rango(0,32)||std::memcmp(v.p,"#ACC",4)||v.n>0x1000000u)return false;
    if(!trayectoria_acc||trayectoria_grupo!=int(g_b2.grupo)) {
      const uint32_t p=g_mem?g_mem->SystemHeapAlloc(uint32_t(v.n),32):0;
      if(!p)return false;
      std::memcpy(base+p,v.p,v.n);
      PPCContext c=ctx;c.r3.u64=p;sub_820800A8(c,base);
      if(REX_LOAD_U32(p+20)!=p+v.U(20,true)) {g_mem->SystemHeapFree(p);return false;}
      if(trayectoria_acc)g_mem->SystemHeapFree(trayectoria_acc);
      trayectoria_acc=p;trayectoria_grupo=int(g_b2.grupo);
    }
    for(unsigned i=0;i<2;++i) {
      std::memcpy(trayectoria_antes[i].data(),base+actor[i]+6750,18);
      REX_STORE_U8(actor[i]+6750,255);REX_STORE_U8(actor[i]+6756,0);
      REX_STORE_U32(actor[i]+6764,trayectoria_acc);
    }
    trayectoria_nativa=true;
    REXLOG_INFO("dbz3 B2 trayectoria nativa: experimento grupo={} ACC={:08X}; aplicar en82114B40",g_b2.grupo,trayectoria_acc);
    return true;
  }
  void RestaurarTrayectoria() {
    if(!trayectoria_nativa)return;
    bool coincide=true;
    for(unsigned i=0;i<2;++i) {
      std::memcpy(base+actor[i]+6750,trayectoria_antes[i].data(),18);
      coincide=coincide&&!std::memcmp(base+actor[i]+6750,trayectoria_antes[i].data(),18);
    }
    REXLOG_INFO("dbz3 B2 trayectoria nativa: restaurados18B ambos actores={}",coincide);
    trayectoria_nativa=false;
  }
