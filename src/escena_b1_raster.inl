// Included by escena_b1_1910.inl. Private PAL viewport and depth buffer;
// the immediate drawer composites only the finished RGBA image.
namespace raster {
constexpr int W=640, H=448;
constexpr float Cerca=.1f, Lejos=65535.f;
struct Material {
  uint32_t flags=0;
  uint64_t alpha=0x44;
  bool cola() const { return flags>>31; }
  bool mezcla() const { return flags&0x800; }
  bool prueba() const { return flags&0x1000; }
};
// GS ALPHA: (A - B) * C / 128 + D. The decoded AMT alpha is expanded
// from 0..128 to 0..255, so as is converted back before alpha test/blend.
inline int Mezclar(uint64_t alpha, int src, int dst, int as) {
  const int rgb[]={src,dst,0,0};
  const int coef[]={as,128,int(alpha>>32&255),0}; // output surface stays opaque (Ad=0x80)
  return std::clamp(((rgb[alpha&3]-rgb[alpha>>2&3])*coef[alpha>>4&3] >> 7)+rgb[alpha>>6&3],0,255);
}
struct Parte {
  size_t primero, fin;
  float z;
  int pase; // fondo, opaco y cola (respectivamente 0, 1, 2)
};
inline void Ordenar(std::vector<Parte>& orden) {
  std::stable_sort(orden.begin(),orden.end(),[](const Parte& a,const Parte& b) {
    if (a.pase!=b.pase) return a.pase<b.pase;
    return a.pase==2 && a.z>b.z;
  });
}
struct Vert {
  float x,y,d,u,v;
  std::array<float,4> rgba;
};
struct Tri {
  Vert v[3];
  const Imagen* imagen=nullptr;
  Material mat;
  bool niebla=false;
};
inline float Prof(const ps2::Camara& c,const ps2::V3& p) {
  return -(c.m[2]*p.x+c.m[6]*p.y+c.m[10]*p.z+c.m[14]);
}
struct Superficie {
  std::vector<Parte> orden;
  std::vector<Tri> tris;
  std::vector<uint32_t> rgba=std::vector<uint32_t>(W*H);
  std::vector<float> profundidad=std::vector<float>(W*H);

  void Empezar() {
    orden.clear(); tris.clear();
    std::fill(rgba.begin(),rgba.end(),IM_COL32_BLACK);
    std::fill(profundidad.begin(),profundidad.end(),0.f);
  }

  void Poner(const ps2::Camara& c,const ps2::Malla& m,const std::vector<Imagen>& imgs,
             const ps2::Escena::Entorno& e,bool hd=false) {
    const float fy=c.Foco(), fx=fy*c.foco_x/c.foco_y;
    size_t j=0;
    while(j<m.tris.size()) {
      const size_t primero=tris.size(), a=j;
      const auto& parte=m.tris[a];
      Material mat{parte.b1_flags,parte.b1_alpha};
      float zmax=-Lejos;
      do {
        const auto& t=m.tris[j++];
        Tri tr;
        tr.mat=mat;
        tr.niebla=e.niebla && !e.fondo && (hd || (mat.flags&0x80));
        if (t.tex>=0 && size_t(t.tex)<imgs.size() && imgs[t.tex]) tr.imagen=&imgs[t.tex];
        bool finitos=true;
        for(int k=0;k<3;++k) {
          const auto& p=t.p[k]; const auto& n=t.n[k];
          const float d=Prof(c,p);
          zmax=std::max(zmax,d);
          auto& v=tr.v[k];
          v={c.m[0]*p.x+c.m[4]*p.y+c.m[8]*p.z+c.m[12],
             c.m[1]*p.x+c.m[5]*p.y+c.m[9]*p.z+c.m[13],d,t.uv[k].x,t.uv[k].y,{}};
          finitos &= std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(d)&&std::isfinite(v.u)&&std::isfinite(v.v);
          const auto col=t.con_color?t.col[k]:IM_COL32_WHITE;
          std::array<float,3> luz{1,1,1};
          if (!e.sin_luz && (n.x||n.y||n.z)) {
            luz=e.ambiente;
            for(int l=0;l<2;++l) {
              const auto& dr=e.dir[l];
              const float f=std::max(0.f,-(dr[0]*n.x+dr[1]*n.y+dr[2]*n.z));
              for(int ch=0;ch<3;++ch) luz[ch]+=e.luz[l][ch]*f;
            }
          }
          for(int ch=0;ch<3;++ch) v.rgba[ch]=std::clamp(luz[ch]*float(col>>(8*ch)&255),0.f,255.f);
          v.rgba[3]=float(col>>24);
        }
        if (!finitos) continue;
        Recortar(tr,fx,fy);
      } while(j<m.tris.size() && m.tris[j].parte==parte.parte && m.tris[j].b1_flags==mat.flags);
      if (primero!=tris.size()) {
        // FUN_00145a20 stores (part sphere center.z + radius) * 1000.
        const float z=Prof(c,parte.b1_centro)+parte.b1_radio;
        orden.push_back({primero,tris.size(),std::isfinite(z)?z:zmax,e.fondo&&!mat.cola()?0:mat.cola()?2:1});
      }
    }
  }

  void Recortar(Tri tr,float fx,float fy) {
    const float sx=fx/(W*.5f), sy=fy/(H*.5f);
    auto dist=[&](const Vert& v,int i) {
      switch(i) {
        case 0:return v.d-Cerca; case 1:return Lejos-v.d;
        case 2:return v.d+sx*v.x; case 3:return v.d-sx*v.x;
        case 4:return v.d+sy*v.y; default:return v.d-sy*v.y;
      }
    };
    std::array<Vert,12> a{},b{};
    std::copy_n(tr.v,3,a.begin());
    int n=3;
    for(int i=0;i<6 && n>=3;++i) {
      int count=0;
      for(int j=0;j<n;++j) {
        const auto& p=a[j]; const auto& q=a[(j+1)%n];
        const float d=dist(p,i), e=dist(q,i);
        if(d>=0) b[count++]=p;
        if ((d<0 && e>0)||(d>0 && e<0)) {
          const float k=d/(d-e);
          auto l=[&](float x,float y) { return std::lerp(x,y,k); };
          auto& v=b[count++]; v={l(p.x,q.x),l(p.y,q.y),l(p.d,q.d),l(p.u,q.u),l(p.v,q.v),{}};
          for(int ch=0;ch<4;++ch) v.rgba[ch]=l(p.rgba[ch],q.rgba[ch]);
        }
      }
      n=count; a.swap(b);
    }
    for(int i=0;i<n;++i) {
      auto& v=a[i];
      v.d=1.f/std::max(v.d,Cerca);
      v.x=W*.5f+v.x*fx*v.d;
      v.y=H*.5f-v.y*fy*v.d;
      v.u*=v.d; v.v*=v.d;
    }
    for(int i=1;i+1<n;++i) {
      tr.v[0]=a[0]; tr.v[1]=a[i]; tr.v[2]=a[i+1]; tris.push_back(tr);
    }
  }

  void Pintar(const ps2::Escena::Entorno& e) {
    Ordenar(orden);
    bool fondo=false;
    for (const auto& p:orden) {
      if (p.pase!=0 && fondo) {
        std::fill(profundidad.begin(),profundidad.end(),0.f);
        fondo=false;
      }
      fondo |= p.pase==0;
      for (size_t i=p.primero;i<p.fin;++i) Dibujar(tris[i],p.pase,e);
    }
  }

  void Dibujar(const Tri& tr,int pase,const ps2::Escena::Entorno& e) {
    auto a=tr.v[0], b=tr.v[1], c=tr.v[2];
    auto borde=[](const Vert& a,const Vert& b,float x,float y) { return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); };
    auto incluye=[](const Vert& a,const Vert& b) { return b.y<a.y || (b.y==a.y && b.x>a.x); };
    float area=borde(a,b,c.x,c.y);
    if (area<0) { std::swap(b,c); area=-area; }
    if (!(area>1e-6f)) return;
    const float inv=1.f/area;
    const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))), x1=std::min(W,int(std::ceil(std::max({a.x,b.x,c.x}))));
    const int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))), y1=std::min(H,int(std::ceil(std::max({a.y,b.y,c.y}))));
    const bool ia=incluye(b,c), ib=incluye(c,a), ic=incluye(a,b);
    const float da=b.y-c.y, db=c.y-a.y, dc=a.y-b.y;
    for (int y=y0;y<y1;++y) {
      float ea=borde(b,c,x0+.5f,y+.5f), eb=borde(c,a,x0+.5f,y+.5f), ec=borde(a,b,x0+.5f,y+.5f);
      for(int x=x0;x<x1;++x,ea+=da,eb+=db,ec+=dc) {
      if(ea<0 || (!ia && ea==0) || eb<0 || (!ib && eb==0) || ec<0 || (!ic && ec==0)) continue;
      const float wa=ea*inv, wb=eb*inv, wc=ec*inv;
      const float d=wa*a.d+wb*b.d+wc*c.d;
      const size_t off=y*W+x;
      if(d<profundidad[off]) continue;
      std::array<float,4> s;
      for(int ch=0;ch<4;++ch) s[ch]=wa*a.rgba[ch]+wb*b.rgba[ch]+wc*c.rgba[ch];
      if (tr.imagen) {
        const auto& im=*tr.imagen;
        float u=(wa*a.u+wb*b.u+wc*c.u)/d, v=(wa*a.v+wb*b.v+wc*c.v)/d;
        if (tr.mat.flags) { u-=std::floor(u); v-=std::floor(v); }
        const int ix=std::clamp(int(std::clamp(u,0.f,1.f)*im.w),0,im.w-1);
        const int iy=std::clamp(int(std::clamp(v,0.f,1.f)*im.h),0,im.h-1);
        const auto* tx=im.rgba.data()+4*(iy*im.w+ix);
        for(int ch=0;ch<4;++ch) s[ch]*=float(tx[ch])/255.f;
      }
      const int as=int(std::clamp(s[3]*(128.f/255),0.f,128.f)+.5f);
      // TEST: scene baseline AREF=1, ATST=GEQUAL, AFAIL=KEEP (0x3001b).
      // B3 stand-ins do not carry GS headers; alpha zero is a cutout there.
      if ((tr.mat.prueba() || !tr.mat.flags) && as<1) continue;
      if (tr.niebla && e.distancia[1]>e.distancia[0]) {
        const float f=std::clamp((1.f/d-e.distancia[0])/(e.distancia[1]-e.distancia[0]),0.f,1.f);
        const float t=std::lerp(e.densidad[0],e.densidad[1],f)/255.f;
        for(int ch=0;ch<3;++ch) s[ch]=std::lerp(s[ch],255*e.color[ch],t);
      }
      auto ch=[&](int k) {
        const int src=int(std::clamp(s[k],0.f,255.f)+.5f);
        return tr.mat.mezcla()?Mezclar(tr.mat.alpha,src,int(rgba[off]>>(8*k)&255),as):src;
      };
      rgba[off]=IM_COL32(ch(0),ch(1),ch(2),255);
      // ZMSK=1 only for the sorted queue. Sky depth was cleared between passes.
      if(pase!=2) profundidad[off]=d;
      }
    }
  }
};
} // namespace raster
