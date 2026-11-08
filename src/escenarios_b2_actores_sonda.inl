// Revisión posterior a14:50: solo observación, interruptor optativo y ventanas
// acotadas. No sustituye física, trayectoria, estado ni resultado del guest.
// Las envolturas de E700/114F8/14B40/14650/109D0 ya tienen propietario.
// Observar sus llamadas interiores evita duplicar o sustituir esos ganchos.
REX_HOOK_RAW(sub_82105BF0) {
  auto& m=dbz3::escenarios::g_b2_motor;
  if(!m.PasoReaccionOriginal(ctx,base))__imp__sub_82105BF0(ctx,base);
}
REX_HOOK_RAW(sub_820FB0D0) {
  auto& m=dbz3::escenarios::g_b2_motor;
  const uint32_t argumento=ctx.r3.u32,retorno=uint32_t(ctx.lr);
  m.MedirActores1450("colision_actualizar_antes",nullptr,argumento,retorno);
  __imp__sub_820FB0D0(ctx,base);
  m.MedirActores1450("colision_actualizar_despues",nullptr,argumento,retorno);
}
REX_HOOK_RAW(sub_82110A38) {
  auto& m=dbz3::escenarios::g_b2_motor;
  const uint32_t argumento=ctx.r3.u32,retorno=uint32_t(ctx.lr);
  m.MedirActores1450("posicion_actualizar_antes",nullptr,argumento,retorno);
  __imp__sub_82110A38(ctx,base);
  m.MedirActores1450("posicion_actualizar_despues",nullptr,argumento,retorno);
}
REX_HOOK_RAW(sub_82114568) {
  auto& m=dbz3::escenarios::g_b2_motor;
  const uint32_t argumento=ctx.r3.u32,retorno=uint32_t(ctx.lr);
  m.MedirActores1450("suelo_consultar_antes",nullptr,argumento,retorno);
  __imp__sub_82114568(ctx,base);
  m.MedirActores1450("suelo_consultar_despues",nullptr,argumento,retorno);
}
REX_HOOK_RAW(sub_82092D70) {
  auto& m=dbz3::escenarios::g_b2_motor;
  const uint32_t argumento=ctx.r3.u32,retorno=uint32_t(ctx.lr);
  if(retorno==0x82114CC8)m.MedirActores1450("trayectoria_restar_antes",nullptr,argumento,retorno);
  __imp__sub_82092D70(ctx,base);
  if(retorno==0x82114CC8)m.MedirActores1450("trayectoria_restar_despues",nullptr,argumento,retorno);
}
// Sonda optativa del ORIGEN de la velocidad de la accion 22. sub_8210AC00
// (resolucion de golpe nativa) escribe x+208 con el vector rival->victima y
// fija la accion 22 antes de instalar la CSK B2. Solo registra: no toca x+208,
// accion, evento ni resultado.
REX_HOOK_RAW(sub_8210AC00) {
  const char* s=std::getenv("DBZ3_TEST_B2_HIT_SONDA");
  const bool sonda=s&&!std::strcmp(s,"1")&&dbz3::escenarios::NieveB2Activa()&&(dbz3::escenarios::g_b2.grupo==7||dbz3::escenarios::g_b2.grupo==8);
  const uint32_t x=ctx.r3.u32,mgr=REX_LOAD_U32(dbz3::escenarios::kMgrPtr);
  const bool actor=sonda&&mgr&&(x==mgr+2096||x==mgr+2096+8240);
  const uint16_t antes=actor?REX_LOAD_U16(x+3244):0;
  const uint32_t modo=ctx.r4.u32;
  __imp__sub_8210AC00(ctx,base);
  if(!actor)return;
  static unsigned n=0;
  const uint16_t ahora=REX_LOAD_U16(x+3244);
  if(antes==22||ahora!=22||n>=64)return;
  ++n;
  const uint32_t rival=REX_LOAD_U32(x+4948);
  const uint32_t giro=rival?uint32_t(REX_LOAD_U32(rival+6064)+REX_LOAD_U32(rival+6072))&0xffffu:0;
  REXLOG_INFO("dbz3 B2 hit sonda: accion22 modo={} victima={:08X} pos=({:.4f},{:.4f},{:.4f}) "
              "vel=({:.5f},{:.5f},{:.5f}) rival={:08X} giro6064+6072={} rival_pos=({:.4f},{:.4f},{:.4f})",
              modo,x,dbz3::escenarios::LeeFl(x+64),dbz3::escenarios::LeeFl(x+68),dbz3::escenarios::LeeFl(x+72),
              dbz3::escenarios::LeeFl(x+208),dbz3::escenarios::LeeFl(x+212),dbz3::escenarios::LeeFl(x+216),rival,giro,
              rival?dbz3::escenarios::LeeFl(rival+64):0,rival?dbz3::escenarios::LeeFl(rival+68):0,rival?dbz3::escenarios::LeeFl(rival+72):0);
}
// Sonda optativa del escritor de x+2928 en el aterrizaje (accion 35). El
// valor 44 (o 38, o +256 si 8185) lo elige sub_82107998. Solo registra.
REX_HOOK_RAW(sub_82107998) {
  {
    const char* b2=std::getenv("DBZ3_TEST_B2_ATERRIZAJE_15B2");
    const uint32_t x0=ctx.r3.u32,m0=REX_LOAD_U32(dbz3::escenarios::kMgrPtr);
    if(b2&&(!std::strcmp(b2,"1")||!std::strcmp(b2,"2"))&&dbz3::escenarios::NieveB2Activa()&&dbz3::escenarios::g_b2.grupo==8&&
       m0&&(x0==m0+2096||x0==m0+2096+8240)&&dbz3::escenarios::g_b2_motor.activo) {
      int i=(x0==dbz3::escenarios::g_b2_motor.actor[1])?1:0;
      auto& mo=dbz3::escenarios::g_b2_motor;
      if(!mo.aterrizaje_b2[i]) {
        mo.aterrizaje_b2[i]=true;mo.aterrizaje_f0[i]=REX_LOAD_U32(0x8238D6B4);
        for(unsigned j=0;j<3;++j)mo.aterrizaje_p0[i][j]=dbz3::escenarios::LeeFl(x0+64+4*j);
      }
      REX_STORE_U16(x0+3244,20);REX_STORE_U16(x0+3252,0);REX_STORE_U32(x0+2928,137);
      static unsigned mm=0;
      if(mm<64){++mm;REXLOG_INFO("dbz3 B2 aterrizaje 15b2-skip: actor={:08X} pos=({:.3f},{:.3f},{:.3f})",
        x0,dbz3::escenarios::LeeFl(x0+64),dbz3::escenarios::LeeFl(x0+68),dbz3::escenarios::LeeFl(x0+72));}
      return; // no ejecutar el aterrizaje nativo: sigue la reaccion 20/CSK
    }
  }
  const char* s=std::getenv("DBZ3_TEST_B2_HIT_SONDA");
  const bool sonda=s&&!std::strcmp(s,"1")&&dbz3::escenarios::NieveB2Activa()&&(dbz3::escenarios::g_b2.grupo==7||dbz3::escenarios::g_b2.grupo==8);
  const uint32_t x=ctx.r3.u32,mgr=REX_LOAD_U32(dbz3::escenarios::kMgrPtr);
  const bool actor=sonda&&mgr&&(x==mgr+2096||x==mgr+2096+8240);
  const uint16_t antes=actor?REX_LOAD_U16(x+3244):0;
  __imp__sub_82107998(ctx,base);
  if(!actor)return;
  static unsigned n=0;
  const uint16_t ahora=REX_LOAD_U16(x+3244);
  if(antes==35||ahora!=35||n>=64)return;
  ++n;
  REXLOG_INFO("dbz3 B2 aterrizaje sonda: accion35 actor={:08X} codigo={} fase3252={} agua8185={} "
              "vm={} cuadro={} pos=({:.4f},{:.4f},{:.4f})",
              x,REX_LOAD_U32(x+2928),unsigned(REX_LOAD_U16(x+3252)),
              unsigned(REX_LOAD_U8(x+8185)),dbz3::escenarios::g_b2_motor.activo?"1":"0",
              REX_LOAD_U32(0x8238D6B4),dbz3::escenarios::LeeFl(x+64),dbz3::escenarios::LeeFl(x+68),dbz3::escenarios::LeeFl(x+72));
}
