// Órdenes adicionales B2. Incluido ANTES del switch común de B2MotorHD::Orden.
// Lo no consumido aquí continúa por el despacho común, sin duplicar sus case.
// D0/D1/D2 quedaron en la máquina común de solGP1; no duplicar sus case.
    if(o.id==0x28&&lado(0)&&contexto&&base&&std::getenv("DBZ3_TEST_B2_MOTOR")&&!dbz3::g_guest_eu&&
       !dbz3::net::Session::Get().active()) {
      const uint32_t x=actor[a[0]],codigo=REX_LOAD_U32(x+2928);
      const uint16_t accion=REX_LOAD_U16(x+3244);
      const int selector=int8_t(REX_LOAD_U8(x+6208));
      const uint32_t modo=REX_LOAD_U32(x+6248);const auto perfil=g_b2.Bloque(7);
      REXLOG_INFO("dbz3 CBT consulta28: grupo={} lado={} codigoHD={} accion={} selector={} modo={} perfil={} PC={:X} cuadro={:.1f}",
                  g_b2.grupo,a[0],codigo,accion,selector,modo,perfil.n>=16?perfil.U(4):0,o.pc,o.cuadro);
      // Solo observación: HD25 no es una proyección B2 válida. La hipótesis
      // 328/25->152 se retiró al contrastar E6430/E6D8 y la actualización
      // común. No inventar equivalencias por aspecto o proximidad numérica.
      // La equivalencia HD22/B2estado13 acreditada en HD22_FUENTES.md ya
      // está integrada en el consumidor común. Delegar, sin un segundo mapa.
      // Su veto por modo de matrices3 es más estricto que la consulta28 B2
      // (1FADA0 solo lee990). La preparación puede dejar ese modo a0 sin
      // cambiar el código de reacción. Resolver SOLO el dominio acreditado,
      // sin rearmar matrices ni escribir estado/evento/zona para pasar el veto.
      if((accion==20||accion==22)&&modo==0&&(selector==0||selector==1)&&
         codigo==uint32_t(154+selector)&&perfil.n>=16&&perfil.U(4)==1&&
         (g_b2.grupo==0||g_b2.grupo==2||g_b2.grupo==8)) {
        h.r[0]=uint32_t(152+selector);
        REXLOG_INFO("dbz3 CBT consulta28: código acreditado {} -> {} con matrices en modo0",codigo,h.r[0]);
        return true;
      }
    }
    if(o.id==0x19&&o.cuenta==3)
      REXLOG_INFO("dbz3 CBT movimiento19: grupo={} PC={:X} cuadro={:.1f} velocidad={:.4f} fase={} lado={}",
                  g_b2.grupo,o.pc,o.cuadro,b2vm::Float(a[0]),b2vm::Signo(a[1]),a[2]);
    if(o.id==0xB8&&B2MotorOn()&&base&&mgr&&
       !dbz3::net::Session::Get().active()) {
      // Lectura en el hilo guest ANTES de ceder los mandos, sin forzar evento,
      // zona ni pose. La conservación final común ocurre después del Tick:
      // se conservan por separado los bits leídos y los originales guardados.
      const auto nombre="dbz3_cbt_limite_"+std::to_string(g_b2.grupo)+"_"+
                        std::to_string(origen)+"_"+std::to_string(uint32_t(o.cuadro))+"_"+std::to_string(o.pc)+".json";
      std::ofstream out(nombre);
      out<<"{\"grupo\":"<<g_b2.grupo<<",\"cuadro\":"<<o.cuadro<<",\"PC\":"<<o.pc
         <<",\"guest\":"<<REX_LOAD_U32(0x8238D6B4)<<",\"zona\":"<<REX_LOAD_U32(kG+64)
         <<",\"estado\":"<<REX_LOAD_U32(mgr+kEstadoCombate)<<",\"resultado\":"<<REX_LOAD_U32(mgr+35132)
         <<",\"ganador\":"<<int32_t(REX_LOAD_U32(mgr+35136))<<",\"hud\":"<<REX_LOAD_U32(0x82324064)
         <<",\"actores\":[";
      for(unsigned i=0;i<2;++i) {
        if(i)out<<',';const uint32_t x=actor[i],mod=REX_LOAD_U32(x+2952);
        out<<"{\"pos\":["<<LeeFl(x+64)<<','<<LeeFl(x+68)<<','<<LeeFl(x+72)
           <<"],\"giro\":"<<REX_LOAD_U32(x+6052)<<",\"accion\":"<<REX_LOAD_U16(x+3244)
           <<",\"codigo\":"<<REX_LOAD_U32(x+2928)<<",\"vida_bits\":"<<REX_LOAD_U32(x+6080)
           <<",\"ki_bits\":"<<REX_LOAD_U32(x+6100)<<",\"vida_guardada_bits\":"<<vida[i]
           <<",\"ki_guardado_bits\":"<<ki[i];
        if(mod>=0x10000000u&&mod<0xeffff000u)
          out<<",\"clip_modelo\":"<<REX_LOAD_U32(mod+1052)<<",\"tiempo_modelo\":"<<LeeFl(mod+1060);
        out<<'}';
      }
      out<<"]}";
    }
    switch(o.id) {
      case 0x18: {
        // ELF375190 ->1FB610: E6430(actor,0). El homólogo HD E6D8
        // despacha el estado0 y reinicia su fase; no escribir acción/evento
        // manualmente ni sustituir esta orden por una espera de duración fija.
        if(!lado(0)||!contexto||!base||dbz3::g_guest_eu||
           dbz3::net::Session::Get().active())return false;
        PPCContext c=*contexto;c.r3.u64=actor[a[0]];c.r4.u64=0;
        sub_8210E6D8(c,base);
        REXLOG_INFO("dbz3 B2 reposo: lado={} accion={} fase={} reloj={:.3f}",
                    a[0],REX_LOAD_U16(actor[a[0]]+3244),
                    REX_LOAD_U16(actor[a[0]]+3252),LeeFl(actor[a[0]]+3256));
        return true;
      }
      case 0x41: case 0x97: {
        // B2: 1B9540 (objeto activo) /1B8F90 (raíz explícita). Ambos
        // escriben la primera palabra de TODOS los registros de8B del estado.
        // HD: BD5E0 selecciona el registro32B; BD678 hace ese mismo bucle.
        if(!contexto||!base||dbz3::g_guest_eu||dbz3::net::Session::Get().active())return false;
        uint32_t registro=0;
        if(o.id==0x97) {
          if(o.cuenta!=2||!fondo.Vincular(base,a[1]))return false;
          registro=fondo.raices[a[1]].registro;
        } else {
          const uint32_t zdd=REX_LOAD_U32(kG+32),zona=REX_LOAD_U32(kG+64);
          const uint32_t objetos=REX_LOAD_U32(kG+52);
          if(!zdd||zona>=REX_LOAD_U32(zdd+4)||objetos<0x10000000u||objetos>=0xefffff00u)return false;
          const uint32_t registros=REX_LOAD_U32(objetos+36*zona+12);
          if(registros<0x10000000u||registros>=0xefffff00u)return false;
          PPCContext c=*contexto;c.r3.u64=1;c.r4.u64=0;sub_820BD5E0(c,base);
          registro=c.r3.u32;
          if(registro!=registros)return false;
        }
        if(registro<0x10000000u||registro>=0xefffff00u)return false;
        const uint32_t estados=REX_LOAD_U32(registro+4);
        if(estados<0x10000000u||estados>=0xefff0000u)return false;
        const int32_t n=int32_t(REX_LOAD_U32(estados+4));
        if(n<0||n>4096)return false;
        // Las copias son del consumidor común y se devuelven al bloquear una
        // escena. No dejar un cambio parcial si falla una orden posterior.
        if(n&&std::none_of(fondo.copias.begin(),fondo.copias.end(),[&](const auto& copia){return copia.p==estados+8;}))
          fondo.copias.push_back({estados+8,std::vector<uint8_t>(base+estados+8,base+estados+8+8u*uint32_t(n))});
        PPCContext c=*contexto;c.r3.u64=estados;c.r4.u64=a[0];sub_820BD678(c,base);
        for(int32_t i=0;i<n;++i)if(REX_LOAD_U32(estados+8+8u*uint32_t(i))!=a[0])return false;
        REXLOG_INFO("dbz3 B2 estados: id={:X} raiz={} registro={:08X} tabla={:08X} n={} valor={:08X}",
                    o.id,o.id==0x97?int(a[1]):-1,registro,estados,n,a[0]);
        return true;
      }
      default:break;
    }
