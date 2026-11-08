// Observa el par240 del robot con DBZ3_TEST_B2_ROBOT_CONSTANTES=1.
// Son argumentos/constantes CPU observados, no una captura del shader ejecutado.
// Ablacion separada y optativa del brillo HD; no se usa para aceptar escenas.
REX_HOOK_RAW(sub_82097290) {
  auto& f=dbz3::escenarios::g_b2_motor.fondo;
  const uint32_t anterior=f.robot_dentro_material;
  f.robot_dentro_material=f.robot_medir_constantes&&f.robot_pasada&&++f.robot_material<=64?f.robot_material:0;
  const uint32_t parte=ctx.r5.u32;
  const bool parte_valida=parte>=0x10000000u&&parte<0xefffff00u;
  const bool quitar_brillo=f.robot_pasada==1&&f.robot_sin_brillo&&parte_valida;
  const uint16_t borde=parte_valida&&(quitar_brillo||f.robot_dentro_material)?REX_LOAD_U16(parte+22):0;
  // 97290/82097978: parte+22==0 publica c39.x=1; distinto de0 publica0.
  // Usar la rama nativa y restaurar el descriptor, sin cambiar luz ni textura.
  if(quitar_brillo)REX_STORE_U16(parte+22,1);
  if(f.robot_dentro_material)
    REXLOG_INFO("dbz3 B2 robot material: pasada={} dibujo={} modelo={:08X} grupo={:08X} parte={:08X} sin_brillo_hd={} borde_anterior={}",
        f.robot_pasada,f.robot_dentro_material,ctx.r3.u32,ctx.r4.u32,parte,quitar_brillo,borde);
  __imp__sub_82097290(ctx,base);
  if(quitar_brillo)REX_STORE_U16(parte+22,borde);
  f.robot_dentro_material=anterior;
}
REX_HOOK_RAW(sub_820955C8) {
  __imp__sub_820955C8(ctx,base);
  auto& f=dbz3::escenarios::g_b2_motor.fondo;
  const uint32_t p=ctx.r3.u32;
  if(f.robot_dentro_material&&p>=0x10000000u&&p<0xefffff00u)
    REXLOG_INFO("dbz3 B2 robot estado: pasada={} dibujo={} descriptor={:08X} flags={:08X} contorno={} otro54={} otro55={} luz92={} textura112={}",
        f.robot_pasada,f.robot_dentro_material,p,REX_LOAD_U32(p),REX_LOAD_U8(p+53),
        REX_LOAD_U8(p+54),REX_LOAD_U8(p+55),REX_LOAD_U8(p+92),REX_LOAD_U8(p+112));
}
REX_HOOK_RAW(sub_820A57D8) {
  auto& f=dbz3::escenarios::g_b2_motor.fondo;
  const uint32_t p=ctx.r6.u32;
  if(f.robot_dentro_material&&ctx.r5.u32&&p>=0x10000000u&&p<0xefffff00u) {
    auto fl=[&](uint32_t q){return dbz3::b2vm::Float(REX_LOAD_U32(q));};
    REXLOG_INFO("dbz3 B2 robot constante: pasada={} dibujo={} funcion=820A57D8 indice={} cantidad={} datos=({:.9f},{:.9f},{:.9f},{:.9f}) lr={:08X}",
        f.robot_pasada,f.robot_dentro_material,ctx.r4.u32,ctx.r5.u32,
        fl(p),fl(p+4),fl(p+8),fl(p+12),uint32_t(ctx.lr));
  }
  __imp__sub_820A57D8(ctx,base);
}
REX_HOOK_RAW(sub_820A5798) {
  auto& f=dbz3::escenarios::g_b2_motor.fondo;
  const uint32_t p=ctx.r6.u32,n=ctx.r5.u32;
  if(f.robot_dentro_material&&n&&n<=16&&p>=0x10000000u&&p<0xefffff00u) {
    auto fl=[&](uint32_t q){return dbz3::b2vm::Float(REX_LOAD_U32(q));};
    for(uint32_t i=0;i<n;++i) {
      const uint32_t q=p+16*i;
      REXLOG_INFO("dbz3 B2 robot constante: pasada={} dibujo={} funcion=820A5798 indice={} cantidad={} fila={} datos=({:.9f},{:.9f},{:.9f},{:.9f}) lr={:08X}",
          f.robot_pasada,f.robot_dentro_material,ctx.r4.u32,n,i,
          fl(q),fl(q+4),fl(q+8),fl(q+12),uint32_t(ctx.lr));
    }
  }
  __imp__sub_820A5798(ctx,base);
}
REX_HOOK_RAW(sub_820A6148) {
  auto& f=dbz3::escenarios::g_b2_motor.fondo;
  const uint32_t p=ctx.r3.u32;
  if(f.robot_dentro_material&&p>=0x10000000u&&p<0xefffff00u) {
    const uint32_t v=REX_LOAD_U32(p);
    const uint32_t destino=v>=0x10000000u&&v<0xefffff00u?REX_LOAD_U32(v+168):0;
    REXLOG_INFO("dbz3 B2 robot programa: pasada={} dibujo={} arg4={} arg5={} arg6={} constantes168={:08X}",
        f.robot_pasada,f.robot_dentro_material,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,destino);
  }
  __imp__sub_820A6148(ctx,base);
}
