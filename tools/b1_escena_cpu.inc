// Arnés de extracción TR3: se añade a la unidad autónoma, sin enlazar el juego.
namespace tr3 {
using namespace dbz3;
using namespace dbz3::pasarela;
using namespace dbz3::pasarela::cine1910;

struct Salida : std::ostringstream {
  std::string nombre;
  explicit Salida(const std::filesystem::path& p) : nombre(p.filename().string()) {
    exceptions(std::ios::badbit | std::ios::failbit);
  }
  template<class T> void num(T v) {
    static_assert(sizeof(T)==4);
    write(reinterpret_cast<const char*>(&v),4);
  }
  void close() {
    const auto data=view();
    std::cout << nombre << ' ' << data.size() << '\n';
    std::cout.write(data.data(),data.size());
    Exigir(bool(std::cout),"salida TR3 incompleta");
  }
};

void imagen(const Imagen& im, const std::filesystem::path& path, bool reparar_alfa) {
  Exigir(im && im.rgba.size()==size_t(im.w)*im.h*4,"imagen RGBA incompleta");
  auto rgba=im.rgba;
  if (reparar_alfa) {
    bool cero=true;
    for (size_t i=3;i<rgba.size();i+=4) cero &= rgba[i]==0;
    if (cero) for (size_t i=3;i<rgba.size();i+=4) rgba[i]=255;
  }
  Salida out(path);
  out.num(uint32_t(im.w)); out.num(uint32_t(im.h));
  out.write(reinterpret_cast<const char*>(rgba.data()),rgba.size());
  out.close();
}

void cuadro(Player& p, size_t corte, double t, const std::filesystem::path& base) {
  Exigir(t>=p.tomas.at(corte).t && t<(corte+1<p.tomas.size() ? p.tomas[corte+1].t : p.fin),"instante fuera del corte");
  p.Avanzar(t);
  Exigir(p.toma==corte,"corte efectivo distinto del solicitado");
  const auto cam=Camara(p.tomas[corte],t,{320,224},{320,224});
  Exigir(cam.ok,"camara invalida");
  Salida out(base/("corte_"+std::to_string(corte)+".tri"));
  out.write("TR31",4);
  for (float v:cam.m) out.num(v);
  out.num(cam.fov); out.num(cam.foco_x); out.num(cam.foco_y);
  const auto& e=p.entorno;
  for (float v:e.color) out.num(v);
  out.num(p.subtitular ? int32_t(p.subtitulo) : -1);
  out.num(p.pos_sub.x); out.num(p.pos_sub.y);
  for (size_t slot=0;slot<p.actores.size();++slot) {
    const auto& a=p.actores[slot];
    if (!a.dibujo && !a.dibujo_b) continue;
    const auto& m=*a.modelo;
    const auto& sk=*m.m.esq;
    auto mesh=&sk.crudo;
    ps2::Malla pose, filtrada;
    const float f=a.clip ? float(std::clamp(t-a.inicio,0.,double(a.clip->cuadros-1))) : 0.f;
    if (m.hd) {
      const auto* local=a.clip ? a.clip->hd.data()+size_t(f)*sk.padre.size() : sk.local.data();
      std::vector<ps2::M34> world(sk.padre.size());
      for (size_t b=0;b<world.size();++b) {
        const int parent=sk.padre[b];
        world[b]=parent>=0 ? world[parent]*local[b] : local[b];
      }
      FijarPelo(m,world);
      pose=m.m.malla;
      for (auto& tri:pose.tris) for (int j=0;j<3;++j) {
        const auto& w=world.at(tri.hueso[j]);
        tri.p[j]=w.por(tri.p[j]); tri.n[j]=w.gira(tri.n[j]);
      }
      mesh=&pose;
    } else {
      if (a.clip) { ps2::Posar(sk,a.clip->ps,f/60.f,pose); mesh=&pose; }
      if (a.partes) {
        const float fp=float(std::clamp(t-a.inicio_partes,0.,double(a.partes->cuadros-1)));
        FiltrarPartes(*a.partes,fp,*mesh,filtrada);
        mesh=&filtrada;
      }
    }
    if (mesh->tris.empty()) continue;
    out.num(int32_t(slot)); out.num(uint32_t(mesh->tris.size()));
    for (const auto& tr:mesh->tris) {
      const int tex=tr.tex>=0 && size_t(tr.tex)<m.m.tex.size() && m.m.tex[tr.tex] ? tr.tex : -1;
      out.num(int32_t(tex));
      for (int j=0;j<3;++j) {
        const auto v=tr.p[j], n=tr.n[j];
        auto light=Punto{1,1,1};
        if (slot!=4 && (n.x || n.y || n.z)) {
          light=e.ambiente;
          for (int l=0;l<2;++l) {
            const auto& d=e.dir[l];
            const float lambert=std::max(0.f,-(n.x*d[0]+n.y*d[1]+n.z*d[2]));
            for (int c=0;c<3;++c) light[c]+=lambert*e.luz[l][c];
          }
        }
        const auto rgba=tr.con_color ? tr.col[j] : tr.trans ? IM_COL32(255,255,255,110) : IM_COL32_WHITE;
        const float depth=-(cam.m[2]*v.x+cam.m[6]*v.y+cam.m[10]*v.z+cam.m[14]);
        float fog=0;
        if (slot!=4 && e.niebla && !tr.trans && (rgba>>24)==255) {
          const float u=std::clamp((depth-e.distancia[0])/(e.distancia[1]-e.distancia[0]),0.f,1.f);
          fog=std::clamp((e.densidad[0]+u*(e.densidad[1]-e.densidad[0]))/255.f,0.f,1.f);
        }
        for (float x:{v.x,v.y,v.z,tr.uv[j].x,1.f-tr.uv[j].y}) out.num(x);
        for (int c=0;c<3;++c) out.num(std::clamp(float((rgba>>(8*c))&255)*light[c]/255.f,0.f,1.f));
        out.num(float(rgba>>24)/255.f); out.num(fog);
      }
    }
  }
  out.num(int32_t(-1));
  out.close();
}
}

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc!=3 || !CargarFixture() || _setmode(_fileno(stdout),_O_BINARY)==-1) return 2;
  using namespace tr3;
  try {
    std::cerr << "Cargando cache\n";
    const auto p=TR3_CARGAR(argv[1],argv[2]);
    Exigir(bool(p),"cache rechazada");
    std::cerr << "Extrayendo materiales\n";
    const std::filesystem::path base;
    for (size_t slot=0;slot<p->actores.size();++slot) if (const auto m=p->actores[slot].modelo) {
      for (size_t j=0;j<m->m.tex.size();++j) if (m->m.tex[j])
        imagen(m->m.tex[j],base/("tex_"+std::to_string(slot)+"_"+std::to_string(j)+".rgba"),true);
    }
    AfsPs2 banco;
    Exigir(banco.Abrir(argv[2],BancoPal(1)),"banco de subtitulos EN");
    const auto sub=HijoAmb(banco.Entrada(1909),58);
    for (int i=0;i<8;++i) imagen(TexturaAmt(sub,i),base/("sub_"+std::to_string(i)+".rgba"),false);
    const auto bytes=Fixture("cuadros.txt");
    std::istringstream plan(std::string(bytes.begin(),bytes.end()));
    for (size_t i=0;i<p->tomas.size();++i) {
      double t;
      Exigir(bool(plan>>t) && std::isfinite(t),"plan truncado o invalido");
      cuadro(*p,i,t,base);
      std::cerr << "Corte " << i << " a " << t << " u\n";
    }
    std::string extra;
    Exigir(!(plan>>extra),"plan con cortes de mas");
    return 0;
  } catch (const std::exception& err) {
    std::cerr << "TR3: " << err.what() << '\n';
    return 1;
  }
}
