  // Sonda de pruebas: detener el siguiente paso guest mientras se captura
  // el último cuadro completo, con el estado lógico guardado en ese cuadro.
  std::vector<uint32_t> fotos;
  size_t foto_i=0;
  bool foto_pendiente=false;
  void PrepararFotos() {
    fotos.clear();foto_i=0;foto_pendiente=false;
    if(const char* s=std::getenv("DBZ3_TEST_B2_FOTOS"))while(*s) {
      char* fin=nullptr;const unsigned long f=std::strtoul(s,&fin,10);
      if(fin==s)break;if(f<10000)fotos.push_back(uint32_t(f));
      s=*fin==','?fin+1:fin;
    }
  }
  void EstadoFoto() {
    if(foto_i>=fotos.size()||!escena||escena->reloj.cuadro<float(fotos[foto_i]+1))return;
    const uint32_t f=fotos[foto_i];
    std::ofstream out("dbz3_b2_foto_"+std::to_string(f)+".json");
    out<<"{\"cuadro\":"<<f<<",\"guest\":"<<ultimo<<",\"zona\":"<<REX_LOAD_U32(kG+64)
       <<",\"camara\":"<<escena->camara<<",\"tiempo_camara\":"<<escena->tiempo_camara
       <<",\"fov\":"<<escena->fov<<",\"ojo\":["<<escena->ojo[0]<<','<<escena->ojo[1]<<','<<escena->ojo[2]
       <<"],\"objetivo\":["<<escena->objetivo[0]<<','<<escena->objetivo[1]<<','<<escena->objetivo[2]<<"],\"actores\":[";
    for(unsigned i=0;i<2;++i) {
      if(i)out<<',';const uint32_t x=actor[i];
      out<<"{\"pos\":["<<LeeFl(x+64)<<','<<LeeFl(x+68)<<','<<LeeFl(x+72)<<"],\"codigo\":"<<REX_LOAD_U32(x+2928)
         <<",\"giro\":"<<REX_LOAD_U32(x+6052)<<",\"vida\":"<<LeeFl(x+6080)<<",\"ki\":"<<LeeFl(x+6100)<<'}';
    }
    out<<"],\"raices\":[";
    for(size_t i=0;i<fondo.raices.size();++i) {
      if(i)out<<',';const auto& a=fondo.raices[i];
      out<<"{\"id\":"<<i<<",\"tiempo\":"<<(a.control?LeeFl(a.control+4):0)
         <<",\"modo\":"<<(a.control?REX_LOAD_U32(a.control+24):0)<<'}';
    }
    out<<"]}";foto_pendiente=true;
  }
  void EsperarFoto() {
#ifdef _WIN32
    if(!foto_pendiente)return;
    const uint32_t f=fotos[foto_i];
    const auto nombre=L"Local\\DBZ3_GP1_B2_Foto_"+std::to_wstring(GetCurrentProcessId());
    HANDLE evento=CreateEventW(nullptr,TRUE,FALSE,nombre.c_str());
    if(evento) {
      REXLOG_INFO("dbz3 B2 foto: detenida cuadro={} guest_siguiente={}",f,REX_LOAD_U32(0x8238D6B4));
      const DWORD r=WaitForSingleObject(evento,30000);
      REXLOG_INFO("dbz3 B2 foto: liberada cuadro={} resultado={}",f,r);CloseHandle(evento);
    }
#endif
    foto_pendiente=false;++foto_i;
  }
