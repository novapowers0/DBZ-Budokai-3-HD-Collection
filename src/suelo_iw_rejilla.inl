// Node_AssignNamesToGrid / Node_BuildGridFromModel, IW EU3046A0/3048E0.
// Incluido por suelo_iw.cpp: periodos originales compartidos por dibujo,
// colision y cambio de copia de la referencia de camara.
struct RejillaIw {
  uint32_t flags = 0;
  float x0 = 0, z0 = 0, ex = 0, ez = 0;
  int nx = 0, nz = 0;
  bool valida() const { return nx > 1 && nz > 1 && ex > 0 && ez > 0; }
};

RejillaIw LeerRejillaIw(const std::vector<uint8_t>& amo, const std::vector<uint8_t>& mcf) {
  auto u = [](const auto& b, size_t o) { uint32_t v=0; if(o+4<=b.size()) std::memcpy(&v,b.data()+o,4); return v; };
  auto f = [](const auto& b, size_t o) { float v=0; if(o+4<=b.size()) std::memcpy(&v,b.data()+o,4); return v; };
  RejillaIw r;
  if(amo.size()<48 || std::memcmp(amo.data(),"#AMO",4) || mcf.size()<20 || std::memcmp(mcf.data(),"#MCF",4)) return r;
  const size_t nb=u(amo,16), nombres=u(amo,36), a=u(amo,u(amo,28)), ejes=a+u(amo,a+20);
  if(nb>amo.size()/80 || nombres+nb*32>amo.size() || ejes+nb*80>amo.size()) return r;
  float xmin=0,xmax=0,zmin=0,zmax=0,ax=0,az=0;
  bool primera=false;
  for(size_t i=0;i<nb;++i) {
    const char* n=reinterpret_cast<const char*>(amo.data()+nombres+i*32);
    const size_t len=strnlen(n,32);
    size_t p=len;
    while(p && n[p-1]!='_') --p;
    if(!p || len-p<2 || len-p>3 || n[p]<'A' || n[p]>'J') continue;
    int j=0;
    bool digitos=true;
    for(size_t k=p+1;k<len;++k) { if(n[k]<'0'||n[k]>'9') digitos=false; else j=j*10+n[k]-'0'; }
    if(!digitos || j<1 || j>25) continue;
    const int ix=n[p]-'A', iz=j-1;
    const float x=f(amo,ejes+80*i+16),z=f(amo,ejes+80*i+24);
    if(!std::isfinite(x)||!std::isfinite(z)) return {};
    xmin=std::min(xmin,x); xmax=std::max(xmax,x); zmin=std::min(zmin,z); zmax=std::max(zmax,z);
    r.nx=std::max(r.nx,ix+1); r.nz=std::max(r.nz,iz+1);
    if(ix==0 && iz==0) ax=x,az=z,primera=true;
  }
  if(!primera || r.nx<2 || r.nz<2) return {};
  r.ex=(xmax-xmin)+(xmax-xmin)/float(r.nx-1);
  r.ez=(zmax-zmin)+(zmax-zmin)/float(r.nz-1);
  // Node_WrapPositionInGrid convierte los limites a int (truncado hacia0).
  r.x0=float(int(ax-r.ex*.5f/float(r.nx)));
  r.z0=float(int(az-r.ez*.5f/float(r.nz)));
  r.flags=u(mcf,16)&3;
  return r;
}
