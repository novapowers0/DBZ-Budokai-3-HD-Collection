  // Los cuatro envíos PAL medidos usan XYZ2.z=16770000, NO la profundidad
  // calculada para la posición del sol (estado.profundidad). Invertir 1335A0
  // sitúa ese Z a d~0.1, delante del plano cercano HD (mgr+0x20=1).
  // Candidato D3D: plano cercano, z=0 (LESS_EQUAL). No acredita equivalencia
  // de los buffers: quedan por resolver los Z GS altos y su descarte efectivo.
  static bool ProbarZ() {
    const char* s=std::getenv("DBZ3_TEST_B2_SOLAR_Z");return s&&!std::strcmp(s,"1");
  }
  static constexpr float z_prueba=.9995f; // Sonda de laboratorio, nunca Z del solar PAL.
  static int EstadoZ() {
    return std::atoi(rex::cvar::GetFlagByName("dbz3_solar_depth_estado").c_str());
  }
  static ImVec2 UVZ(ImVec2 uv,float z) {
    // Igual contrato de vértice que dbz3_ui_depth.vs.hlsl: dos half + float Z.
    const auto media=[](float x)->uint32_t {
      uint32_t b;std::memcpy(&b,&x,4);
      const uint32_t signo=(b>>16)&0x8000,m=b&0x7fffff;
      const int e=int((b>>23)&255)-112;
      if(e<=0)return signo;if(e>=31)return signo|0x7bff;
      return signo|std::min((uint32_t(e)<<10|m>>13)+((m>>12)&1),0x7bffu);
    };
    const uint32_t b=media(uv.x)|(media(uv.y)<<16);float packed;
    std::memcpy(&packed,&b,4);return {packed,z};
  }
