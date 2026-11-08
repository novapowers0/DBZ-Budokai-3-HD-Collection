// PAL1AF9B0 elimina TODAS las tareas de la raíz con400, no solo la primera.
// HD BF8A0/C82E8 marcan estas tareas con100. BD570 conserva el enlazado,
// la tabla de256 ranuras y el contador del motor; nunca se simula su liberación.
struct B2TareasHD {
  static constexpr uint32_t cabeza=0x82398890,fin=0x823987e0;
  static constexpr uint32_t tabla=0x82398940,pool=0x82398d40;
  struct Seleccion {std::array<uint32_t,256> tareas{};unsigned cantidad=0;bool valida=false;};
  static constexpr bool Direccion(uint32_t p) {
    return p>=pool&&p<pool+256*176&&(p-pool)%176==0;
  }
  template<class Leer> static constexpr Seleccion Buscar(uint32_t usuario,Leer leer) {
    Seleccion r;
    uint32_t anterior=cabeza,t=leer(cabeza+36);
    unsigned vistos=0;
    while(t!=fin) {
      if(!Direccion(t)||vistos++>=256||leer(t+40)!=anterior)return r;
      if(leer(t+16)==usuario&&(leer(t+32)&0x100)) {
        bool registrado=false;
        for(unsigned i=0;i<256;++i)if(leer(tabla+4*i)==t){registrado=true;break;}
        if(!registrado)return r;
        r.tareas[r.cantidad++]=t;
      }
      anterior=t;t=leer(t+36);
    }
    r.valida=leer(fin+40)==anterior;
    return r;
  }
  template<class Leer,class Quitar> static bool Eliminar(uint32_t usuario,Leer leer,Quitar quitar,unsigned& n) {
    // Comprobar la lista completa ANTES de la primera mutación.
    const auto r=Buscar(usuario,leer);n=0;
    if(!r.valida)return false;
    for(unsigned i=0;i<r.cantidad;++i){quitar(r.tareas[i]);++n;}
    return true;
  }
};

// Casos sintéticos del MISMO selector usado en runtime, evaluados en el build.
// Dos coincidencias, una tarea ajena y otra de igual usuario SIN100 intercaladas.
constexpr bool B2ProbarTareasHD(unsigned caso) {
  using T=B2TareasHD;
  constexpr uint32_t u=0x40000000;
  const auto leer=[caso](uint32_t p) constexpr -> uint32_t {
    if(p==T::cabeza+36)return caso==1?T::fin:T::pool;
    if(p==T::fin+40)return caso==1?T::cabeza:T::pool+3*176;
    if(p>=T::tabla&&p<T::tabla+1024) {
      const unsigned i=(p-T::tabla)/4;
      return i<4&&!(caso==4&&i==3)?T::pool+i*176:0;
    }
    for(unsigned i=0;i<4;++i) {
      const uint32_t t=T::pool+i*176;
      if(p==t+16)return i==1?u+32:u;
      if(p==t+32)return i==2?0:0x100;
      if(p==t+40)return caso==3&&i==3?T::cabeza:(i?t-176:T::cabeza);
      if(p==t+36)return caso==2&&i==3?T::pool:(caso==5&&i==3?1:(i==3?T::fin:t+176));
    }
    return 0;
  };
  const auto r=T::Buscar(u,leer);
  if(caso==1)return r.valida&&r.cantidad==0;
  if(caso>=2)return !r.valida;
  return r.valida&&r.cantidad==2&&r.tareas[0]==T::pool&&r.tareas[1]==T::pool+3*176;
}
static_assert(B2ProbarTareasHD(0),"94: seleccionar todas y solo las tareas de la raiz con100");
static_assert(B2ProbarTareasHD(1),"94: lista vacia valida");
static_assert(B2ProbarTareasHD(2),"94: rechazar ciclos");
static_assert(B2ProbarTareasHD(3),"94: rechazar enlaces inversos rotos");
static_assert(B2ProbarTareasHD(4),"94: rechazar tarea no registrada antes de mutar");
static_assert(B2ProbarTareasHD(5),"94: rechazar direccion ajena al pool");
