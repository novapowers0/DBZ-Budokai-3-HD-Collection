  // Sonda de pruebas: detener el siguiente paso guest mientras se captura
  // el último cuadro completo, con el estado lógico guardado en ese cuadro.
  std::vector<uint32_t> fotos;
  size_t foto_i=0;
  bool foto_pendiente=false;
  // Registro optativo de la VM y de la cámara escrita antes del dibujo.
  // Son dos puntos distintos: una muestra lógica no acredita un render.
  std::ofstream traza;
  uint32_t traza_dibujo=~0u;
  bool actores_1450=false;
  // Ampliación optativa de la ventana: cubre la transición entera para medir la
  // trayectoria de la víctima, no solo los tramos 0..60/440..460 y 265..305.
  bool actores_completo=false;
  bool VentanaActores1450() const {
    if(!actores_1450||!activo||!escena)return false;
    if(actores_completo)return true; // eB2d: tambien grupos 0/2/4 (solo prueba)
    const float t=escena->reloj.cuadro;
    return (g_b2.grupo==7&&((t>=0&&t<=60)||(t>=440&&t<=460)))||
           (g_b2.grupo==8&&t>=265&&t<=305);
  }
  void MedirActores1450(const char* tipo,const b2::Orden* orden=nullptr,uint32_t argumento=0,uint32_t retorno=0) {
    if(VentanaActores1450())MedirTraza(tipo,0,orden,argumento,retorno);
  }
  void PrepararTraza() {
    if(traza.is_open())traza.close();traza.clear();traza_dibujo=~0u;
    const char* ampliada=std::getenv("DBZ3_TEST_B2_ACTORES_1450");
    actores_1450=ampliada&&!std::strcmp(ampliada,"1")&&(g_b2.grupo==7||g_b2.grupo==8||(g_b2.grupo<=4&&std::getenv("DBZ3_TEST_B2_ACTORES_COMPLETO")));
    const char* todo=std::getenv("DBZ3_TEST_B2_ACTORES_COMPLETO");
    actores_completo=actores_1450&&todo&&!std::strcmp(todo,"1");
    const char* s=std::getenv("DBZ3_TEST_B2_CURVA");
    if(!s||std::strcmp(s,"1"))return;
    std::error_code ec;
    if(std::filesystem::exists("dbz3_b2_traza.jsonl",ec)||ec) {
      REXLOG_WARN("dbz3 B2 curva: conservar traza anterior antes de medir");return;
    }
    traza.open("dbz3_b2_traza.jsonl");traza.precision(9);
    if(!traza)REXLOG_WARN("dbz3 B2 curva: no se pudo abrir el registro");
  }
  void MedirTraza(const char* tipo,uint32_t camara=0,const b2::Orden* orden=nullptr,uint32_t argumento=0,uint32_t retorno=0) {
    if(!traza.is_open()||!escena)return;
    const uint32_t guest=REX_LOAD_U32(0x8238D6B4);
    if(camara) {if(traza_dibujo==guest)return;traza_dibujo=guest;}
    auto vector=[&](uint32_t p) {
      traza<<'['<<LeeFl(p)<<','<<LeeFl(p+4)<<','<<LeeFl(p+8)<<']';
    };
    traza<<"{\"tipo\":\""<<tipo<<"\",\"guest\":"<<guest<<",\"cuadro\":"<<escena->reloj.cuadro
         <<",\"grupo\":"<<g_b2.grupo<<",\"entrada\":"<<escena->entrada<<",\"zona\":"<<REX_LOAD_U32(kG+64)
         <<",\"tiempo_camara\":"<<escena->tiempo_camara<<",\"indice_camara\":"<<escena->camara
         <<",\"evento_hd\":"<<REX_LOAD_U16(mgr+35204)<<",\"reaccion_pal\":"<<(reaccion_pal?"true":"false")
           <<",\"contacto\":["<<punto_contacto[0]<<','<<punto_contacto[1]<<','<<punto_contacto[2]<<']';
    if(argumento&&actores_1450)traza<<",\"llamada\":{\"r3\":"<<argumento<<",\"retorno\":"<<retorno<<'}';
    if(orden&&actores_1450) {
      traza<<",\"orden\":{\"id\":"<<orden->id<<",\"pc\":"<<orden->pc<<",\"argumentos_bits\":[";
      for(size_t i=0;i<orden->cuenta;++i){if(i)traza<<',';traza<<orden->argumentos[i];}
      traza<<"]}";
    }
    traza<<",\"logica\":{\"ojo\":["<<escena->ojo[0]<<','<<escena->ojo[1]<<','<<escena->ojo[2]
         <<"],\"objetivo\":["<<escena->objetivo[0]<<','<<escena->objetivo[1]<<','<<escena->objetivo[2]
         <<"],\"giro\":"<<escena->giro_camara<<",\"fov\":"<<escena->fov<<'}';
    if(camara) {
      traza<<",\"aplicada\":{\"direccion\":"<<camara<<",\"ojo\":";vector(camara);
      traza<<",\"objetivo\":";vector(camara+16);
      traza<<",\"arriba\":";vector(camara+32);
      traza<<",\"giro\":"<<LeeFl(camara+48)<<",\"fov\":"<<LeeFl(camara+52)
           <<",\"flags\":"<<REX_LOAD_U32(camara+56)<<'}';
    }
    traza<<",\"actores\":[";
    for(unsigned i=0;i<2;++i) {
      if(i)traza<<',';const uint32_t x=actor[i];
      traza<<"{\"pos\":";vector(x+64);
      traza<<",\"codigo\":"<<REX_LOAD_U32(x+2928)<<",\"giro\":"<<REX_LOAD_U32(x+6052)
           <<",\"cuadro_anim\":"<<LeeFl(x+3160);
      if(VentanaActores1450()) {
        traza<<",\"direccion\":"<<x<<",\"papel\":"<<i<<",\"fisico\":"<<(x==mgr+2096?0:1);
        traza<<",\"traslacion\":";vector(x+48);
        traza<<",\"colision\":";vector(x+1616);
        traza<<",\"velocidad\":";vector(x+208);
        //8210AC00/ADBC forma la dirección con6064+6072 del rival,
        //no con el ángulo6052 que ya figuraba en la muestra visual.
        traza<<",\"giro_6064\":"<<REX_LOAD_U32(x+6064)
             <<",\"giro_extra_6072\":"<<REX_LOAD_U32(x+6072);
        traza<<",\"accion\":"<<REX_LOAD_U16(x+3244)<<",\"fase\":"<<REX_LOAD_U16(x+3252)
             <<",\"reloj_accion\":"<<LeeFl(x+3256)<<",\"modo_matrices\":"<<REX_LOAD_U32(x+6248)
              <<",\"aire_8184\":"<<int(int8_t(REX_LOAD_U8(x+8184)))
              <<",\"suelo_6216\":"<<LeeFl(x+6216)<<",\"flag_8185\":"<<unsigned(REX_LOAD_U8(x+8185))
              <<",\"trayectoria_hd\":{\"hueso\":"<<int(int8_t(REX_LOAD_U8(x+6750)))
              <<",\"activa\":"<<unsigned(REX_LOAD_U8(x+6756))<<",\"clip\":"<<int16_t(REX_LOAD_U16(x+6758))
              <<",\"tiempo\":"<<LeeFl(x+6760)<<",\"acc\":"<<REX_LOAD_U32(x+6764)<<'}'
              <<",\"trayectoria_vm\":{\"hueso\":"<<escena->actores[i].hueso
             <<",\"clip\":"<<escena->actores[i].clip<<",\"tiempo\":"<<escena->actores[i].tiempo<<'}';
        const uint32_t mod=REX_LOAD_U32(x+2952);
        traza<<",\"modelo\":"<<mod;
        if(mod>=0x10000000u&&mod<0xeffff000u)
          traza<<",\"clip_modelo\":"<<REX_LOAD_U32(mod+1052)<<",\"tiempo_modelo\":"<<LeeFl(mod+1060);
        // Solo medida: matrices de huesos (43 x 16 floats, mod+0x1480, paso 0x40) para comparar pose con PAL.
        static const bool huesos=[]{const char* v=std::getenv("DBZ3_TEST_B2_HUESOS");return v&&*v=='1';}();
        if(huesos&&mod>=0x10000000u&&mod<0xeffff000u) {
          static unsigned n_h=0;static bool vuelca=false; // ambos actores en el mismo muestreo
          if(i==0){vuelca=!std::strcmp(tipo,"paso")||(n_h<48||(n_h%8)==0);++n_h;} // eB2d: todos los 'paso'
          if(vuelca) {
            traza<<",\"modelo_raw\":[";
            for(uint32_t w=0;w<0xD00;++w){if(w)traza<<',';traza<<REX_LOAD_U32(mod+4*w);}
            traza<<']';
            // Nombres de hueso del ACM del cuerpo. OJO: NO es el orden de las matrices (ver tools/solb2x_huesos_cmp3.py).
            const uint32_t acm=REX_LOAD_U32(mod+0x46c);
            if(acm>=0x10000000u&&acm<0xeffff000u) {
              const uint32_t nb=REX_LOAD_U32(acm+24),nv=REX_LOAD_U32(acm+28),nm=nv<0x10000000u?acm+nv:nv;
              traza<<",\"acm_nombres\":[";
              for(uint32_t j=0;j<nb&&j<128;++j){
                if(j)traza<<',';traza<<'"';
                for(unsigned c=0;c<32;++c){const char ch=char(REX_LOAD_U8(nm+32*j+c));if(!ch)break;traza<<ch;}
                traza<<'"';
              }
              traza<<']';
            }
          }
        }
      }
      traza<<'}';
    }
    traza<<"],\"raices\":[";
    for(size_t i=0;i<fondo.raices.size();++i) {
      if(i)traza<<',';const auto& a=fondo.raices[i];
      traza<<"{\"id\":"<<i<<",\"control\":"<<a.control
           <<",\"tiempo\":"<<(a.control?LeeFl(a.control+4):0)
           <<",\"modo\":"<<(a.control?REX_LOAD_U32(a.control+24):0)<<'}';
    }
    traza<<"]}\n";
  }
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
