#pragma once
namespace dbz3::iwbubbles {
// Tabla ejecutada por C++ y por el arnes offline: orden de updates EU4995A0.
#define BB_REGLA(mask,cond,act) {mask,cond,act},
inline constexpr Regla kReglas[] = {
BB_REGLA(0x001,Timeout,Entrar1)
BB_REGLA(0x010,FinLibre,FrenarE0)
BB_REGLA(0x06F,Siempre,Seleccion)
BB_REGLA(0x010,Siempre,Seleccion)
BB_REGLA(0x002,FinLibre,Entrar2)
BB_REGLA(0x001,Distinto,EntrarDistinto)
BB_REGLA(0x002,Distinto,EntrarDistinto)
BB_REGLA(0x004,FinLibre,EntrarElegido)
BB_REGLA(0x004,Distinto,EntrarDistinto)
BB_REGLA(0x008,Distinto,EntrarDistinto)
BB_REGLA(0x010,SaleMovimiento,SalirMovimiento)
BB_REGLA(0x020,Fin,EntrarElegidoFin)
BB_REGLA(0x040,FinLibre,EntrarElegidoFin)
BB_REGLA(0x080,CercaFrente,Evadir4)
BB_REGLA(0x100,TareaActiva,Interrumpir4)
BB_REGLA(0x380,Fin,Volver0)
BB_REGLA(0x080,Evento7,Emitir7)
BB_REGLA(0x100,Evento8,Emitir8)
BB_REGLA(0x200,Evento9,Emitir9)
};
#undef BB_REGLA
inline void Maquina::Entrar(int nuevo) {
  static constexpr unsigned clips[10]={0xD2,0xD3,0xD4,0xDD,0xDF,0xE1,0xD8,0xD5,0xD6,0xE2};
  anterior=estado;estado=nuevo;codigo=clips[nuevo];cuadro=0;reloj=0;fin=false;
  mezcla=noche::Mezcla349(unsigned(nuevo),cc4);
  latch224=nuevo==7||nuevo==8||nuevo==9;
  if(nuevo==2)flag218=false;
  ++revision;Anotar(anterior);
}
inline int Maquina::Selector() const {
  if(flag210)return estado==4?9:8;
  if(stick_x==0&&stick_y==0) {
    if(flag200)return 6;
    if(flag214)return 7;
    if(flag218)return 2;
    return estado==1?1:estado==2?2:0;
  }
  return std::fabs(stick_x)>.9f||std::fabs(stick_y)>.9f?4:3;
}
template<class Rng> inline void Maquina::Crear(const Entrada& e,Rng& rng,bool lento,int forzado) {
  if(forzado==5){
    //Normal del SWEEP guardada antes del Ray, no la del jugador/suelo.
    const auto t=noche::PrepararTarea5({contacto_tarea5.normal.x,contacto_tarea5.normal.y,contacto_tarea5.normal.z},
        e.dificultad,1.f,1.f,e.factor_velocidad,rng);
    modo=5;fase=0;tiempo=0;duracion=t.duracion;stick_x=.1f;tarea=true;
    vel=t.velocidad;centro=t.vector30;tarea_actual=++siguiente_tarea;
    tareas.push_back({5,0,0,duracion,vel,centro,true,tarea_actual});return;
  }
  modo=int(rng.Memory(255)%3)+1;
  if(forzado)modo=forzado;
  tarea=true;fase=0;tiempo=0;stick_x=lento?.1f:1.f;
  const unsigned lim=unsigned(e.dificultad*20+100);
  if(modo==2)fase=int(rng.Memory(32)&1);
  duracion=modo==2?float(rng.Memory(32)%100)+100:
      modo==4?float(rng.Memory(32)%50)+100:
      modo==5?float(rng.Memory(32)%unsigned(200-e.dificultad*20)):
      float(rng.Memory(32)%lim)+(modo==1?35.f:60.f);
  const V origen=anclas.actual640;
  V dir=Normal({origen.x-e.jugador.x,0,origen.z-e.jugador.z});
  if(modo==4)dir=Normal({e.jugador.x-origen.x,0,e.jugador.z-origen.z});
  else {
    // EU3170D0: bit de lado ANTES del sorteo de mezcla, ambos Memory32.
    const bool lado=(rng.Memory(32)&1)!=0;
    const float mezcla=float(rng.Memory(32)%10)*.1f;
    const V perpendicular=lado?V{-dir.z,0,dir.x}:V{dir.z,0,-dir.x};
    dir=Normal(Suma(dir,Escala(Resta(perpendicular,dir),mezcla)));
  }
  vel=Escala(dir,(modo==4?1.f:4.5f)*e.factor_velocidad);
  if(modo==2)centro=Suma(origen,Escala(vel,25));
  // El creador IW no destruye una tarea previa. La IA puede crear dos en una pasada.
  tarea_actual=++siguiente_tarea;
  tareas.push_back({modo,fase,0,duracion,vel,centro,true,tarea_actual});
}
template<class Rng> inline void Maquina::IA(const Entrada& e,Rng& rng) {
  const int j=e.estado_jugador,d=e.dificultad;
  if((j>=0x2c&&j<0x32)||e.bloqueo248||e.objetivo180)return;
  auto azar=[&](){return int(rng.Memory(32)%1000);};
  if(tarea) {
    if(estado==4&&azar()<(d+1)*10&&j!=1)flag210=true;
    return;
  }
  const float distancia=Distancia(e),dot=Frente(e);
  if(j==0x1e) { if(distancia<=110)Crear(e,rng,false,1); }
  else if(j==1&&estado<3&&distancia>=35&&azar()<990-d*180)Crear(e,rng,true,4);
  if(dot>-.3f) {
    if(distancia<55)Crear(e,rng,false,0);
    else if(distancia>300) { if(estado!=7)flag214=true; }
    else {
      const int r=azar();
      if(distancia<=100) {
        if(r<10) { if(j!=1&&estado!=8)flag210=true; }
        else if(r<20) { Crear(e,rng,false,0);return; }
        else if(j==5&&azar()<(d+1)*20&&estado!=8)flag210=true;
      } else {
        if(r<10) { if(estado!=2)flag218=true; }
        else if(r<20) { if(estado!=7)flag214=true; }
        else if(r<30) { if(j!=1&&estado!=8)flag210=true; }
        else if(r<40) { Crear(e,rng,false,0);return; }
        else if(j==5&&azar()<(d+1)*20&&estado!=8)flag210=true;
      }
    }
  } else if(distancia<15) {
    Crear(e,rng,false,0);if(icono==0)icono=100;
  } else {
    int r=azar();
    if(j>=3&&j<0x15) {r=int(rng.Memory(32)%100);if(distancia<55)r=0;}
    if(r<d+1) {
      const bool bit=(rng.Memory(32)&1)!=0;
      if(icono==0)icono=100;
      if(bit) {if(estado!=2)flag218=true;} else if(estado!=6)flag200=true;
    }
    if(j==5&&azar()<(d+1)*20&&estado!=8)flag210=true;
  }
}
template<class Rng> inline void Maquina::Trayectoria(const Entrada& e,Rng& rng) {
  for (Tarea& t:tareas) {
  if(!t.activa)continue;
  if(t.duracion<t.tiempo) {t.activa=false;tarea=false;tarea_actual=0;stick_x=0;continue;}
  if(t.modo==2) {
    const V d=Normal({anclas.actual640.x-t.centro.x,0,anclas.actual640.z-t.centro.z});
    t.vel=Escala(t.fase==0?V{d.z,0,-d.x}:V{-d.z,0,d.x},4.5f*e.factor_velocidad);
  }
  if(t.modo==3) {
    bool giro=false;
    if(t.tiempo>t.duracion*.75f&&t.fase==2) {t.fase=3;giro=true;}
    else if(t.tiempo>t.duracion*.5f&&t.fase==1) {t.fase=2;giro=true;}
    else if(t.tiempo>t.duracion*.25f&&t.fase==0) {t.fase=1;giro=true;}
    V dir=Normal(t.vel);
    if(giro)dir=(rng.Memory(32)&1)?V{-dir.z,0,dir.x}:V{dir.z,0,-dir.x};
    t.vel=Escala(dir,4.5f*e.factor_velocidad);
  }
  p=Suma(p,t.vel); // original NO multiplica otra vez por delta del reloj.
  if(t.modo==4&&Distancia(e)<35) {t.activa=false;tarea=false;tarea_actual=0;stick_x=0;}
  t.tiempo=noche::ClockAdd(t.tiempo,e.paso);
  modo=t.modo;fase=t.fase;tiempo=t.tiempo;duracion=t.duracion;vel=t.vel;
  }
  for(auto it=tareas.begin();it!=tareas.end();) {
    if(!it->activa)it=tareas.erase(it);else ++it;
  }
}
inline void Maquina::Emitir(int tipo,const Entrada& e) {
  if(n_eventos<4)eventos[n_eventos++]={tipo,.1f,p};
  for(int i=0;i<e.dificultad-2&&i<3;++i)
    if(n_eventos<4)eventos[n_eventos++]={tipo==0?3:4,tipo==0?.1f+float(i)*.05f:.3f+float(i)*.2f,p};
  latch224=false;
}
inline void Maquina::ActualizarAncla(){
  const float yaw=std::atan2(frente.x,frente.z),c=std::cos(yaw),s=std::sin(yaw);
  raiz.rotacion.m={c,0,s,0,1,0,-s,0,c};raiz.posicion40=p;
  anclas.actual640=Suma(p,raiz.rotacion.Girar(raiz.local50));
}
template<class Pose> inline void Maquina::EvaluarPose(Pose& pose,float frame){
  raiz.local50=pose.Evaluar(codigo,frame);pose_codigo=codigo;pose_cuadro=frame;
}
template<class Pose> inline void Maquina::RootAndBones(Pose& pose,unsigned flags){
  ActualizarAncla();raiz.flags18e8=flags;
  if(flags){flags_consumidos=flags;ultima_raiz=raiz.local50;}
  noche::ConsumirRaiz(raiz);p=raiz.posicion40;
  pose.AplicarRaiz(raiz.local50);ActualizarAncla();
  anclas.prev60=anclas.actual640;anclas.prev60.y=p.y+(anclas.aire2061?anclas.alturaB84:0);
  anclas.prev70=anclas.actual640;anclas.prev70.y+=anclas.alturaB84;
}
template<class Rng,class Pose> inline void Maquina::Tick(const Entrada& e,Rng& rng,Pose& pose,wcf::Backend& colision){
  const bool mueve=estado==3||estado==4||estado==9;
  if(mueve){
    //299EE0 solo si no hay B8. El vector es el de movimiento, no otro azar.
    if(!tarea&&(stick_x!=0||stick_y!=0))p=Suma(p,Escala(frente,(estado==3?1.f:4.5f)*e.factor_velocidad));
    work.anterior90=anclas.actual640;
    if(Largo(vel)>0)frente=Normal(vel);
    ActualizarAncla();
    anclas.prev60=anclas.actual640;anclas.prev60.y=p.y;
    anclas.prev70=anclas.actual640;anclas.prev70.y+=anclas.alturaB84;
  }else if((estado==2||estado==6||estado==8)&&!tarea){
    frente=Normal({e.jugador.x-anclas.actual640.x,0,e.jugador.z-anclas.actual640.z});ActualizarAncla();
  }
  const V original=anclas.actual640;
  const V destino=noche::Canonica(original,grid),desde=noche::ReferenciaCercana(work.anterior90,destino,grid);
  barrido=colision.Sweep({desde.x,desde.y,desde.z},{destino.x,destino.y,destino.z},.5f,2);
  const V corregida=noche::Canonica({barrido.point.x,barrido.point.y,barrido.point.z},grid);
  work.tipoEsfera1f0=barrido.hit?barrido.contact.type:0;
  if(barrido.hit)work.contacto238={{barrido.contact.normal.x,barrido.contact.normal.y,barrido.contact.normal.z},barrido.contact.d,barrido.contact.material,barrido.contact.type};
  p=Suma(p,Resta(corregida,original));RootAndBones(pose);
  if(noche::DesviarPorChoque(barrido.hit,barrido.contact.type)&&tarea_actual){
    const auto id=tarea_actual;for(auto& t:tareas)if(t.id==id)t.activa=false;
    tarea_actual=0;tarea=false;contacto_tarea5=barrido.contact;
    Crear(e,rng,true,5);nueva_tarea5=true; //52FF nueva corre la proxima pasada, nunca aqui.
  }
  V arriba=corregida,abajo=corregida;arriba.y+=4;abajo.y-=500;
  rayo=colision.Ray({arriba.x,arriba.y,arriba.z},{abajo.x,abajo.y,abajo.z},7);
  if(rayo.hit){work.tipoSuelo1e8=rayo.contact.type;work.contacto238={{rayo.contact.normal.x,rayo.contact.normal.y,rayo.contact.normal.z},rayo.contact.d,rayo.contact.material,rayo.contact.type};}
  work.altura18d0=rayo.hit?rayo.point.y:abajo.y;
  if(!anclas.aire2061)p.y=work.altura18d0;
  work.factor184c=(mueve||estado==5)?.1f:0;
  work.tareaB8=tarea_actual;
  if(e.c0_valida)p=noche::EnvolverActorExterior(p,e.camaraC0,grid);
  bloqueado_c0=!e.c0_valida;
  RootAndBones(pose); //flush exterior con18E8 ya limpio, sin duplicar la raiz.
}
template<class Rng,class Pose> inline void Maquina::Paso(const Entrada& e,Rng& rng,Pose& pose,wcf::Backend& colision) {
  ++tick;n_eventos=0;n_cambios=0;flags_consumidos=0;nueva_tarea5=false;
  ultima_raiz=raiz.local50;
  flag200=false;flag210=false;flag214=false;
  cuadro_previo_fin=cuadro;
  const auto clock=noche::AvanzarClock(cuadro,e.paso,largos[codigo]);
  const float avanzado=clock.avanzado;
  const float largo=largos[codigo];
  fin=clock.fin;
  //Evaluar la instancia ANTES del reset/transicion; fin9 no consume AMM(frame0) por accidente.
  EvaluarPose(pose,fin?std::max(0.f,largo-1):avanzado);
  cuadro=clock.siguiente;
  auto entrar=[&](int n,std::int16_t previo=0){cc4=previo;Entrar(n);EvaluarPose(pose,0);};
  const unsigned origen=1u<<unsigned(estado);
  for(const Regla& r:kReglas) {
    if(!(r.estados&origen))continue;
    bool cumple=false;
    switch(r.condicion) {
      case Siempre:cumple=true;break;
      case Timeout:cumple=reloj>300-e.dificultad*30;break;
      case Fin:cumple=fin;break;
      case FinLibre:cumple=fin&&!cc4;break;
      case Distinto:cumple=elegido!=estado;break;
      case SaleMovimiento:cumple=elegido!=3&&elegido!=4;break;
      case TareaActiva:cumple=tarea;break;
      case CercaFrente:cumple=Frente(e)>-.3f&&Distancia(e)<=35;break;
      case Evento7:cumple=!fin&&cuadro>291&&latch224;break;
      case Evento8:cumple=!fin&&cuadro>36&&latch224;break;
      case Evento9:cumple=!fin&&cuadro>13&&latch224;break;
    }
    if(!cumple)continue;
    switch(r.accion) {
      case Seleccion:IA(e,rng);elegido=Selector();break;
      case Entrar1:entrar(1);break;
      case Entrar2:elegido=2;break;
      case EntrarElegido:RootAndBones(pose,1);entrar(elegido);break;
      case EntrarElegidoFin:RootAndBones(pose,1);entrar(elegido,elegido==0?1:0);break;
      case EntrarDistinto:entrar(elegido);break;
      case FrenarE0:codigo=0xE0;cuadro=0;fin=false;cc4=1;mezcla=noche::Mezcla349(4,cc4,true);++revision;Anotar(estado);EvaluarPose(pose,0);break;
      case SalirMovimiento:entrar(elegido==9?9:5);break;
      case Evadir4:Crear(e,rng,false,1);RootAndBones(pose,1);entrar(4);break;
      case Interrumpir4:RootAndBones(pose,1);entrar(4);break;
      case Volver0:RootAndBones(pose,1);entrar(0);break;
      case Emitir7:Emitir(2,e);break;
      case Emitir8:Emitir(0,e);break;
      case Emitir9:Emitir(1,e);break;
    }
  }
  //Si ningun update cambio de clip (DD/E0/reposo en bucle), ahora si evaluar0.
  //Las salidas que consumen raiz ya lo hicieron con la instancia previa.
  if(fin)EvaluarPose(pose,0);
  Trayectoria(e,rng); //52FF despues de update5100, antes de tick5300.
  Tick(e,rng,pose,colision);
  reloj=noche::ClockAdd(reloj,e.paso);
  if(icono>0)icono=icono>e.paso?icono-e.paso:0;
}
inline std::array<ResultadoGolden,6> SelfTestWcf(const std::vector<std::uint8_t>& datos){
  std::array<ResultadoGolden,6> out{};
  for(std::size_t i=0;i<kGoldens.size();++i){
    wcf::Backend aislado;auto& r=out[i];const auto& g=kGoldens[i];
    if(!aislado.Load(datos.data(),datos.size())){r.error=aislado.Error();continue;}
    r.actual=g.ray?aislado.Ray(g.desde,g.hasta,7):aislado.Sweep(g.desde,g.hasta,.5f,2);
    auto error=[](wcf::V a,wcf::V b){
      if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))return std::numeric_limits<float>::infinity();
      return std::max({std::fabs(a.x-b.x),std::fabs(a.y-b.y),std::fabs(a.z-b.z)});
    };
    r.error_punto=error(r.actual.point,g.punto);r.error_normal=g.hit?error(r.actual.contact.normal,g.normal):0;
    r.pasa=r.actual.hit==g.hit&&r.error_punto<=.0002f&&r.error_normal<=.0002f&&
      (!g.hit||(r.actual.contact.material==g.material&&r.actual.contact.type==g.tipo));
  }
  return out; //sin RNG, Maquina, pose, backend activo ni estado de juego.
}
inline ResultadoClock SelfTestClock(const char* archivo){
  ResultadoClock r;
  if(!archivo||!*archivo){r.error="falta TEST_GP2_BB_CLOCK_FIXTURE";return r;}
  std::FILE* f=std::fopen(archivo,"rb");if(!f){r.error="fixture no legible";return r;}
  char magic[8]{};std::uint32_t n=0;
  if(std::fread(magic,1,8,f)!=8||std::memcmp(magic,"BBCLOCK1",8)||std::fread(&n,4,1,f)!=1||n!=99){
    r.error="cabecera/count RAW no es CLOCK99";std::fclose(f);return r;
  }
  for(std::uint32_t i=0;i<n;++i){
    //EE,before,paso,longitud,after: TODOS leidos de la captura original, no generados por ClockAdd.
    std::uint32_t raw[5]{};
    if(std::fread(raw,4,5,f)!=5){r.error="RAW incompleto";break;}
    float a=0,delta=0,len=0;std::memcpy(&a,raw+1,4);std::memcpy(&delta,raw+2,4);std::memcpy(&len,raw+3,4);
    //Mul de reloj identidad: no cobrar de nuevo8B80, FPS o velocidad del actor.
    const auto paso=noche::AvanzarClock(a,noche::ClockMul(delta,1.f),len);
    std::uint32_t bits=0;std::memcpy(&bits,&paso.siguiente,4);++r.total;
    if(bits==raw[4])++r.pasadas;
    else if(!r.primerEE){r.primerEE=raw[0];r.esperado=raw[4];r.actual=bits;}
  }
  if(std::fgetc(f)!=EOF&&r.error.empty())r.error="RAW con datos sobrantes";
  std::fclose(f);return r;
}
} // namespace dbz3::iwbubbles
