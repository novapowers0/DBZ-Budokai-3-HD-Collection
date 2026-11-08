// #19/#61: hitos de la transicion nativa, sin cambiar peticiones ni guardados.
// La apertura .sfd ya la registra el VFS; esto separa una espera anterior del
// arranque/decodificacion. Parejas EU verificadas en eu_map_gen.h.
DBZ3_HOOK_EU(sub_821CC308) {
  REXLOG_INFO("dbz3 creditos: crear tarea escena 12 (entrada)");
  orig(ctx, base);
  REXLOG_INFO("dbz3 creditos: crear tarea escena 12 (retorno)");
}

// Windows ya tiene este gancho en video_hd.inl. Linux usa el reproductor
// original del guest, no el reproductor IW ni los videos HD de Windows.
#if !defined(_WIN32)
DBZ3_HOOK_EU(sub_820919D8) {
  REXLOG_INFO("dbz3 video nativo: empezar (entrada), reproductor={:08X}, nombre={:08X}",ctx.r3.u32,ctx.r4.u32);
  orig(ctx, base);
  REXLOG_INFO("dbz3 video nativo: empezar (retorno), r3={:08X}",ctx.r3.u32);
}
#endif
