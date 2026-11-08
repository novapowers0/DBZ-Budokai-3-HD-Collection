  // Adquisición optativa: el arnés pide una foto DESPUÉS de preparar el combate.
  // Se detiene el siguiente paso guest; la UI conserva el último solar publicado.
  // No sincroniza la cola GPU ni acredita por sí sola fidelidad con PAL.
  std::atomic<bool> foto_detenida{false};
  bool foto_ui_guardada=false; // protegido por mutex; solo la UI escribe el JSON
  bool foto_sin_ui_guardada=false;
  bool foto_sin_z_guardada=false,foto_prueba_z_guardada=false;
#ifdef _WIN32
  struct FotoCanales {
    HANDLE pedir=nullptr,seguir=nullptr,sin_solar=nullptr,sin_z=nullptr,prueba_z=nullptr;
    ~FotoCanales(){if(pedir)CloseHandle(pedir);if(seguir)CloseHandle(seguir);if(sin_solar)CloseHandle(sin_solar);
      if(sin_z)CloseHandle(sin_z);if(prueba_z)CloseHandle(prueba_z);}
  } foto_canales;
#endif
  void EsperarFoto(uint8_t* base) {
#ifdef _WIN32
    const char* s=std::getenv("DBZ3_TEST_B2_SOLAR_FOTO");
    if(!s||std::strcmp(s,"1")||grupo!=4||tareas.empty())return;
    const auto id=std::to_wstring(GetCurrentProcessId());
    if(!foto_canales.pedir)foto_canales.pedir=CreateEventW(nullptr,FALSE,FALSE,
      (L"Local\\DBZ3_B2_Solar_Pedir_"+id).c_str());
    if(!foto_canales.seguir)foto_canales.seguir=CreateEventW(nullptr,TRUE,FALSE,
      (L"Local\\DBZ3_B2_Solar_Seguir_"+id).c_str());
    const char* capas=std::getenv("DBZ3_TEST_B2_SOLAR_CAPAS");
    if(capas&&!std::strcmp(capas,"1")&&!foto_canales.sin_solar)
      foto_canales.sin_solar=CreateEventW(nullptr,TRUE,FALSE,
        (L"Local\\DBZ3_B2_Solar_Sin_"+id).c_str());
    if(ProbarZ()&&!foto_canales.sin_z) {
      foto_canales.sin_z=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\DBZ3_B2_Solar_SinZ_"+id).c_str());
      foto_canales.prueba_z=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\DBZ3_B2_Solar_PruebaZ_"+id).c_str());
    }
    if(!foto_canales.pedir||!foto_canales.seguir||
       WaitForSingleObject(foto_canales.pedir,0)!=WAIT_OBJECT_0)return;
    ResetEvent(foto_canales.seguir);
    if(foto_canales.sin_solar)ResetEvent(foto_canales.sin_solar);
    if(foto_canales.sin_z)ResetEvent(foto_canales.sin_z);
    if(foto_canales.prueba_z)ResetEvent(foto_canales.prueba_z);
    {std::lock_guard<std::mutex> l(mutex);foto_ui_guardada=foto_sin_ui_guardada=false;
      foto_sin_z_guardada=foto_prueba_z_guardada=false;foto_detenida.store(true);}
    REXLOG_INFO("dbz3 B2 solar foto: detenida guest_siguiente={}",REX_LOAD_U32(0x8238D6B4));
    const DWORD r=WaitForSingleObject(foto_canales.seguir,30000);
    foto_detenida.store(false);
    REXLOG_INFO("dbz3 B2 solar foto: liberada resultado={}",r);
#endif
  }
  bool SinSolarFoto() const {
#ifdef _WIN32
    return foto_detenida.load()&&foto_canales.sin_solar&&
      WaitForSingleObject(foto_canales.sin_solar,0)==WAIT_OBJECT_0;
#else
    return false;
#endif
  }
  int ModoFotoZ() const {
#ifdef _WIN32
    if(foto_detenida.load()) {
      if(foto_canales.sin_z&&WaitForSingleObject(foto_canales.sin_z,0)==WAIT_OBJECT_0)return 1;
      if(foto_canales.prueba_z&&WaitForSingleObject(foto_canales.prueba_z,0)==WAIT_OBJECT_0)return 2;
    }
#endif
    return 0;
  }
  void GuardarFotoUI(const ingame::style::Imagen& r,bool sin_solar=false,unsigned emitidos=0) {
    // Llamado bajo mutex, tras añadir los quads a la lista de dibujo.
    const int modo=ModoFotoZ();
    bool& guardada=sin_solar?foto_sin_ui_guardada:modo==1?foto_sin_z_guardada:modo==2?foto_prueba_z_guardada:foto_ui_guardada;
    if(!foto_detenida.load()||guardada)return;
    std::ofstream out(sin_solar?"dbz3_b2_solar_foto_sin_ui.json":modo==1?"dbz3_b2_solar_foto_sinz_ui.json":
      modo==2?"dbz3_b2_solar_foto_pruebaz_ui.json":"dbz3_b2_solar_foto_ui.json");out.precision(9);
    out<<"{\"guest_publicado\":"<<ultimo_publicado
       <<",\"ui_frame\":"<<ImGui::GetFrameCount()<<",\"sin_solar\":"<<(sin_solar?"true":"false")
       <<",\"quads_emitidos\":"<<emitidos
       <<",\"z_solicitado\":"<<(ProbarZ()?"true":"false")<<",\"z_estado_sdk\":"<<EstadoZ()
       <<",\"z_equivalencia_pal\":false"
       <<",\"z_modo_foto\":"<<modo<<",\"z_gs\":16770000,\"z_d3d_normal\":"<<(modo==1?-1.f:modo==2?z_prueba:0.f)
       <<",\"guest_image\":["<<r.p0.x<<','<<r.p0.y<<','<<r.p1.x<<','<<r.p1.y
       <<"],\"quads\":[";
    for(size_t i=0;i<quads.size();++i) {
      if(i)out<<',';const auto& q=quads[i];
      out<<"{\"textura\":"<<(q.tex?9:8)<<",\"rgba_u32\":"<<q.color<<",\"xy\":[";
      for(unsigned j=0;j<4;++j){if(j)out<<',';out<<'['<<q.p[j].x<<','<<q.p[j].y<<']';}
      out<<"],\"uv\":[";
      for(unsigned j=0;j<4;++j){if(j)out<<',';out<<'['<<q.uv[j].x<<','<<q.uv[j].y<<']';}
      out<<"]}";
    }
    out<<"]}";out.close();
    guardada=true;
    REXLOG_INFO("dbz3 B2 solar foto: UI lista guest_publicado={} quads={} emitidos={} sin_solar={} json={}",
      ultimo_publicado,quads.size(),emitidos,sin_solar,!out.fail());
    if(!sin_solar&&modo)REXLOG_INFO("dbz3 B2 solar foto: Z modo={} estado={} json={}",modo,EstadoZ(),!out.fail());
  }
