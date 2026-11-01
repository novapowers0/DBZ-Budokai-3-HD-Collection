// Adaptación de las pistas B2 al almacén global HD. Mismo método validado de
// anim_iw.cpp::Bloque/InstalarCuerpo: sufijos de hueso, sin sustituir geometría.
struct B2AnimHD {
  struct Mapa { uint32_t codigo, banco, clip; };
  // Medida del builtin11 en PCSX2 propio gp1, resolver_anim_vm.log. Los índices
  // NO se deducen por proximidad numérica a una animación de B3.
  static constexpr Mapa mapas[] = {{1285,0,167},{1286,0,168},{1287,0,169},{1288,0,170},
      {1289,0,171},{1290,0,172},{1291,0,173},{265,1,28},{520,1,74},{152,0,118},
      {160,0,137},{256,1,19},{36,0,107},{40,0,99},
      // Segunda consulta del builtin11 original: resolver_reacciones_b2.log.
      //158/159/173..175 vuelven a estado0: NO se inventan pistas para ellos.
      {144,0,118},{146,0,118},{147,0,118},{153,0,119},{154,0,120},
      {155,0,121},{156,0,122},{157,0,240},{168,0,129},{169,0,130},
      {170,0,131},{171,0,131},{172,0,132}};
  struct Banco { uint32_t acm=0, n=0, tabla=0, nueva=0, primer=0; bool rel=false; };
  struct Cuerpo { uint32_t a=0, antes=0, nuevo=0; };
  std::vector<Banco> bancos;
  std::vector<Cuerpo> cuerpos;
  std::array<std::array<uint32_t,8>,std::size(mapas)> metadatos{};
  std::array<uint32_t,std::size(mapas)> tablas_ap{},cuentas_ap{};
  uint32_t fuentes[2] = {};
  uint32_t Reservar(uint8_t* base, uint32_t n) {
    const uint32_t p = g_mem ? g_mem->SystemHeapAlloc(n, 32) : 0;
    if (p) std::memset(base+p,0,n);
    return p;
  }
  static uint32_t Ptr(uint32_t a,uint32_t v) { return v && v<0x10000000u ? a+v : v; }
  static std::string Sufijo(uint8_t* base,uint32_t p) {
    std::string n;
    for(unsigned i=0;i<32;++i) { const char c=char(REX_LOAD_U8(p+i));if(!c)break;n+=c; }
    const size_t j=n.find('_');return j==std::string::npos ? n : n.substr(j+1);
  }
  bool Preparar(uint8_t* base,const b2::Recurso& r) {
    const auto bsk=r.Bloque(11);
    if(!bsk.Rango(0,32)||std::memcmp(bsk.p,"#BSK",4))return false;
    const uint32_t n=bsk.U(16),tabla=bsk.U(20);
    if(!n||n>4096||!bsk.Rango(tabla,4u*n))return false;
    std::vector<uint8_t> sin_impactos(bsk.p,bsk.p+bsk.n);
    for(size_t i=0;i<std::size(mapas);++i) {
      const auto& m=mapas[i];if(m.codigo>=n)return false;
      const uint32_t p=bsk.U(tabla+4*m.codigo);
      if(!p||!bsk.Rango(p,48)||(bsk.U(p)&65535)!=m.clip||(bsk.U(p)>>16)!=m.banco)return false;
      for(unsigned j=0;j<8;++j)metadatos[i][j]=bsk.U(p+4+4*j);
      const uint32_t np=bsk.U(p+40),tp=bsk.U(p+44);
      if(np>64||!bsk.Rango(tp,8u*np))return false;
      for(uint32_t j=0;j<np;++j) {
        const uint32_t q=tp+8*j;
        // AP1 referencia HR/impactos de B2; AP7 son eventos propios. El
        // conversor existente excluye7: marcar1como7 SOLOenesta copiafiltrada.
        if(uint16_t(bsk.p[q]|uint16_t(bsk.p[q+1])<<8)==1) {
          sin_impactos[q]=7;sin_impactos[q+1]=0;
        }
      }
    }
    for(size_t i=0;i<std::size(mapas);++i) {
      dbz3::anim::ap::Pistas ap;
      if(!dbz3::anim::ap::Convertir(sin_impactos,bsk.U(tabla+4*mapas[i].codigo),0,ap)||!ap.hr.empty())return false;
      cuentas_ap[i]=ap.n;
      tablas_ap[i]=dbz3::anim::ap::Instalar(ap,
          [&](uint32_t bytes){return Reservar(base,bytes);},
          [&](uint32_t p,uint8_t v){REX_STORE_U8(p,v);});
      if(ap.n&&!tablas_ap[i])return false;
    }
    for(unsigned k=0;k<2;++k) if(!fuentes[k]) {
      const auto v=r.Bloque(8+k);
      if(v.n<32||v.U(24,true)!=42)return false;
      fuentes[k]=Reservar(base,uint32_t(v.n));if(!fuentes[k])return false;
      std::memcpy(base+fuentes[k],v.p,v.n);
    }
    uint32_t total_ap=0;for(const uint32_t n:cuentas_ap)total_ap+=n;
    REXLOG_INFO("dbz3 B2 movimientos: {} descriptores originales, {} grupos AP0/2..6; AP1/7 pendientes",std::size(mapas),total_ap);
    return true;
  }
  bool Instalar(uint8_t* base,uint32_t x) {
    auto fallo=[&](const char* motivo,uint32_t p,uint32_t q=0) {
      REXLOG_WARN("dbz3 B2 movimientos: cuerpo {:08X} sin vincular: {} ({:08X}, {:08X})",x,motivo,p,q);
      return false;
    };
    const uint32_t a=x+0x650,mod=REX_LOAD_U32(a+0x538);
    if(mod<0x10000000u)return fallo("modelo",mod);
    const uint32_t acm=REX_LOAD_U32(mod+0x46c);
    if(acm<0x10000000u||(REX_LOAD_U32(acm)&0xffffffu)!=0x41434du)return fallo("ACM",acm);
    Banco* banco=nullptr;
    for(auto& b:bancos)if(b.acm==acm)banco=&b;
    if(!banco) {
      Banco b;b.acm=acm;b.n=REX_LOAD_U32(acm+16);b.tabla=REX_LOAD_U32(acm+20);b.primer=b.n;
      const uint32_t nb=REX_LOAD_U32(acm+24),names=Ptr(acm,REX_LOAD_U32(acm+28));
      if(!nb||nb>128||!b.n||b.n>4096)return false;
      const uint32_t viejo=Ptr(acm,REX_LOAD_U32(Ptr(acm,b.tabla)+12));
      b.rel=false;
      for(uint32_t j=0;j<nb*2;++j)if(const uint32_t p=REX_LOAD_U32(viejo+4*j)) {b.rel=p<0x10000000u;break;}
      const uint32_t count=uint32_t(std::size(mapas));
      b.nueva=Reservar(base,16*(b.n+count));if(!b.nueva)return false;
      std::memcpy(base+b.nueva,base+Ptr(acm,b.tabla),16*b.n);
      for(uint32_t i=0;i<count;++i) {
        const Mapa& m=mapas[i];const uint32_t src=fuentes[m.banco];
        if(!src||m.clip>=REX_LOAD_U32(src+16))return false;
        const uint32_t st=src+REX_LOAD_U32(src+20)+16*m.clip,sb=src+REX_LOAD_U32(st+12);
        const uint32_t sn=REX_LOAD_U32(src+24),names_src=src+REX_LOAD_U32(src+28);
        const uint32_t block=Reservar(base,8*nb);if(!block)return false;
        for(uint32_t j=0;j<nb;++j) {
          const auto nom=Sufijo(base,names+32*j);
          uint32_t k=0;while(k<sn&&Sufijo(base,names_src+32*k)!=nom)++k;
          if(k==sn)continue;
          for(uint32_t z=0;z<2;++z) if(const uint32_t rel=REX_LOAD_U32(sb+4*(2*k+z))) {
            const uint32_t ptr=src+rel;REX_STORE_U32(block+4*(2*j+z),b.rel ? ptr-acm : ptr);
          }
        }
        const uint32_t d=b.nueva+16*(b.n+i);
        for(uint32_t j=0;j<3;++j)REX_STORE_U32(d+4*j,REX_LOAD_U32(st+4*j));
        REX_STORE_U32(d+12,b.rel ? block-acm : block);
      }
      REX_STORE_U32(acm+16,b.n+count);
      REX_STORE_U32(acm+20,b.tabla<0x10000000u ? b.nueva-acm : b.nueva);
      bancos.push_back(b);banco=&bancos.back();
      REXLOG_INFO("dbz3 B2 movimientos: pistas originales añadidas al ACM HD {:08X}, {}..{}",acm,b.primer,b.primer+count-1);
    }
    const uint32_t prev=REX_LOAD_U32(a+0x2c0);
    // Como InstalarCuerpo de anim_iw: el primer hueco puede estar vacío;
    // se crea ahí el CSK privado sin perder los otros dos bancos del cuerpo.
    if(prev&&(prev<0x10000000u||prev>=0xf0000000u))return fallo("CSK",prev);
    const uint32_t n0=prev?REX_LOAD_U32(prev+16):0,t0=prev?Ptr(prev,REX_LOAD_U32(prev+20)):0;
    if(n0>4096||(n0&&(t0<0x10000000u||t0>=0xf0000000u)))return fallo("tabla CSK",n0,t0);
    const uint32_t n=std::max(n0,1292u),count=uint32_t(std::size(mapas));
    const uint32_t csk=Reservar(base,32+4*n+48*count);if(!csk)return false;
    if(prev)std::memcpy(base+csk,base+prev,32);
    else {REX_STORE_U32(csk+4,32);REX_STORE_U32(csk+12,4);}
    REX_STORE_U32(csk,0x2143534b);REX_STORE_U32(csk+16,n);REX_STORE_U32(csk+20,csk+32);
    if(prev)REX_STORE_U32(csk+28,Ptr(prev,REX_LOAD_U32(prev+28)));
    for(uint32_t i=0;i<n0;++i) {
      const uint32_t v=REX_LOAD_U32(t0+4*i);REX_STORE_U32(csk+32+4*i,Ptr(prev,v));
    }
    for(uint32_t i=0;i<count;++i) {
      const uint32_t bl=csk+32+4*n+48*i;
      REX_STORE_U16(bl,uint16_t(banco->primer+i));REX_STORE_U16(bl+2,0);
      // Misma cabecera escalar de reproducción del BSK original: no sustituir
      // bucles/centinelas por0. AP0/2..6 usan el conversor existente; AP1/7
      // siguen pendientes, sin ejecutar HR/funciones ajenas de B3.
      for(unsigned j=0;j<8;++j)REX_STORE_U32(bl+4+4*j,metadatos[i][j]);
      REX_STORE_U32(bl+40,cuentas_ap[i]);REX_STORE_U32(bl+44,tablas_ap[i]);
      REX_STORE_U32(csk+32+4*mapas[i].codigo,bl);
    }
    REX_STORE_U32(a+0x2c0,csk);cuerpos.push_back({a,prev,csk});
    REXLOG_INFO("dbz3 B2 movimientos: CSK privado cuerpo={:08X} anterior={:08X} n={} nuevo={:08X}",x,prev,n0,csk);
    return true;
  }
  void Restaurar(uint8_t* base) {
    for(const auto& c:cuerpos)if(REX_LOAD_U32(c.a+0x2c0)==c.nuevo)REX_STORE_U32(c.a+0x2c0,c.antes);
    for(const auto& b:bancos)if(Ptr(b.acm,REX_LOAD_U32(b.acm+20))==b.nueva) {
      REX_STORE_U32(b.acm+16,b.n);REX_STORE_U32(b.acm+20,b.tabla);
    }
    cuerpos.clear();bancos.clear();
  }
};
