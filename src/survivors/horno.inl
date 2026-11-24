// Dragon Budokai Survivors (prueba tecnica): IMPOSTORES. Se incluye desde pasarela_nave.cpp justo despues de DibujarFiguraB3En, dentro de
// dbz3::pasarela::nave (ve SkB3, Tk y TkEstado). Hornea UNA vez, por software (sin GPU: todo el 3D propio va por CPU), un atlas de
// cuadros 2D de una figura de B3 andando, vista con la camara cenital de la prueba, para pintar cientos de enemigos con un rectangulo
// cada uno en una sola textura.
//
// Atlas: cols = `cuadros` (repartidos por una vuelta del clip), filas = `dirs` (fila d: giro d * 2 pi / dirs con el giro de
// DibujarFiguraB3En, 0 = de cara a +z = mirando a la camara). Celda px x px, RGBA con alfa recto (borde suave por el supermuestreo).
// Camara: ortografica, mirando a -z e inclinada `picado` radianes hacia abajo (eje derecha = +x del mundo).
// Anclaje (devuelto en `ancla`, si no es nulo): [0], [1] = pixel de la celda donde caen los PIES (origen del modelo en x/z y la y
// minima de cada pose, el mismo punto que DibujarFiguraB3En pone en `suelo`), igual en todos los cuadros y direcciones (encuadre fijo:
// la caja de todas las poses y giros); [2] = px del atlas por unidad de mundo de una figura de `alto` 1 de DibujarFiguraB3En (en
// pantalla, con S px por unidad de mundo y una figura de alto A, la celda mide px * S * A / ancla[2]); [3] = segundos de una vuelta del clip.
// Pintado: z-buffer, supermuestreo 2x reducido a px, textura por vecino, tramo de la rampa toon de B3 por pixel (como Escena::PonerT;
// sin rampa, sus 3 tonos), prueba de alfa (< 64 fuera), triangulos translucidos (`trans`) saltados y contorno negro de 1,5 px
// (dilatacion del alfa) en lugar del borde de caras empujadas. Devuelve el estado de TkEstado (2 = atlas relleno; 1 = generandose).
// `tono` (grados, Dragon Budokai Survivors): recolor por GIRO de tono de las texturas del modelo antes de muestrear, como retint_modelo de
// mod center hd/colores.py (el verde del Saibaman pasa a rojo con ~-120; lo gris, saturacion < 0,15, no cambia). 0 = sin recolor.
// `clip`: cualquier clip del tk (0 espera, 1 andar, 2 ataque...): para otra animacion se hornea otro atlas.
namespace {
void GirarTonoRgba(pasarela::Imagen& im, float grados) {   // HSV: h += grados; s y v igual
  const float g = grados / 60.0f;
  for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
    const float r = im.rgba[i] / 255.0f, gg = im.rgba[i + 1] / 255.0f, b = im.rgba[i + 2] / 255.0f;
    const float mx = std::max({r, gg, b}), mn = std::min({r, gg, b}), d = mx - mn;
    if (mx <= 0 || d / mx < 0.15f) continue;
    float h = mx == r ? (gg - b) / d : mx == gg ? 2.0f + (b - r) / d : 4.0f + (r - gg) / d;   // sextantes 0..6
    h = std::fmod(h + g, 6.0f);
    if (h < 0) h += 6.0f;
    const float x = d * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    float o[3];
    switch (int(h)) {
      case 0: o[0] = d, o[1] = x, o[2] = 0; break;
      case 1: o[0] = x, o[1] = d, o[2] = 0; break;
      case 2: o[0] = 0, o[1] = d, o[2] = x; break;
      case 3: o[0] = 0, o[1] = x, o[2] = d; break;
      case 4: o[0] = x, o[1] = 0, o[2] = d; break;
      default: o[0] = d, o[1] = 0, o[2] = x; break;
    }
    for (int k = 0; k < 3; ++k) im.rgba[i + size_t(k)] = uint8_t(std::clamp((o[k] + mn) * 255.0f + 0.5f, 0.0f, 255.0f));
  }
}
}  // namespace
static int HornearModeloB3(const SkB3* sk, const Tk* tk, int fid, int clip, int cuadros, int dirs, int px, float picado, pasarela::Imagen* atlas, float* ancla, float tono) {
  std::vector<pasarela::Imagen> giradas;   // copia recoloreada de las texturas (solo con tono)
  if (tono != 0.0f) {
    giradas = sk->imgs;
    for (pasarela::Imagen& im : giradas) GirarTonoRgba(im, tono);
  }
  const auto& imgs = tono != 0.0f ? giradas : sk->imgs;
  const Tk::Clip* c = &tk->clip[std::clamp(clip, 0, 7)];
  if (c->cuadros == 0) c = &tk->clip[0];
  if (c->cuadros == 0 || cuadros < 1 || dirs < 1 || px < 8) return 0;
  const auto t0 = std::chrono::steady_clock::now();
  const size_t nt = sk->malla.tris.size(), nv = nt * 3;

  // 1) poses (como DibujarFiguraB3En, sin mezcla), con los pies de cada una en y = 0
  std::vector<ps2::V3> P(size_t(cuadros) * nv), N(size_t(cuadros) * nv);
  std::vector<ps2::M34> w(tk->nb);
  for (int f = 0; f < cuadros; ++f) {
    // bucle: cuadros repartidos por una vuelta; sin bucle (derrota, golpe...): del primero al ULTIMO, que es la pose en la que se queda
    const uint32_t cf = c->bucle || cuadros < 2 ? uint32_t(f) * c->cuadros / uint32_t(cuadros) : uint32_t(f) * (c->cuadros - 1) / uint32_t(cuadros - 1);
    const ps2::M34* loc = c->m.data() + size_t(cf) * tk->nb;
    for (size_t i = 0; i < w.size(); ++i) w[i] = sk->esq.padre[i] >= 0 ? w[size_t(sk->esq.padre[i])] * loc[i] : loc[i];
    ps2::V3* p = P.data() + size_t(f) * nv;
    ps2::V3* n = N.data() + size_t(f) * nv;
    float ymin = 1e9f;
    for (size_t t = 0; t < nt; ++t) {
      const ps2::Malla::Tri& src = sk->malla.tris[t];
      for (int k = 0; k < 3; ++k) {
        const ps2::M34& W = w[src.hueso[k]];
        p[t * 3 + k] = W.por(src.p[k]);
        n[t * 3 + k] = W.gira(src.n[k]);
        ymin = std::min(ymin, p[t * 3 + k].y);
      }
    }
    for (size_t v = 0; v < nv; ++v) p[v].y -= ymin;
  }

  // vista de un punto del modelo con el giro de la fila: x derecha, y arriba, z hacia la camara (la de Escena::PonerT)
  const float cp = std::cos(picado), sp = std::sin(picado);
  const auto vista = [&](const ps2::V3& v, float cg, float sg) {
    const float x = v.x * cg + v.z * sg, z = -v.x * sg + v.z * cg;
    return ps2::V3{x, v.y * cp - z * sp, v.y * sp + z * cp};
  };

  // 2) encuadre fijo: la caja de todas las poses y giros (x simetrico alrededor de los pies)
  float xm = 1e-3f, ya = 1e-3f, yb = 0.0f;
  for (int d = 0; d < dirs; ++d) {
    const float g = float(d) * 6.2831853f / float(dirs), cg = std::cos(g), sg = std::sin(g);
    for (const ps2::V3& v : P) {
      const ps2::V3 q = vista(v, cg, sg);
      xm = std::max(xm, std::abs(q.x)), ya = std::max(ya, q.y), yb = std::min(yb, q.y);
    }
  }
  const float margen = 3.0f;   // px: el contorno y el filtro
  const float k = std::min((float(px) - 2 * margen) / (2 * xm), (float(px) - 2 * margen) / (ya - yb));   // px del atlas por unidad de modelo
  const float ax = float(px) * 0.5f, ay = margen + ya * k;

  // 3) cada celda a doble resolucion: color + z-buffer, contorno y reduccion
  constexpr int kSs = 2;
  const int hp = px * kSs;
  const float kh = k * kSs, axh = ax * kSs, ayh = ay * kSs;
  std::vector<uint32_t> col(size_t(hp) * hp);
  std::vector<float> zb(size_t(hp) * hp);
  std::vector<uint8_t> m1(size_t(hp) * hp), m2(size_t(hp) * hp);
  atlas->w = px * cuadros, atlas->h = px * dirs;
  atlas->rgba.assign(size_t(atlas->w) * size_t(atlas->h) * 4, 0);
  constexpr ps2::V3 kLuz{-0.4512f, 0.5514f, 0.7018f};   // la de Escena::PonerT (luz de B3 en vista)
  const auto texel = [&](int ti, ImVec2 uv) -> uint32_t {
    if (ti < 0 || size_t(ti) >= imgs.size() || !imgs[size_t(ti)]) return 0xFFFFFFFFu;
    const pasarela::Imagen& im = imgs[size_t(ti)];
    const float u = uv.x - std::floor(uv.x), v = uv.y - std::floor(uv.y);
    const int x = std::min(int(u * float(im.w)), im.w - 1), y = std::min(int(v * float(im.h)), im.h - 1);
    uint32_t t;
    std::memcpy(&t, im.rgba.data() + (size_t(y) * size_t(im.w) + size_t(x)) * 4, 4);
    return t;   // bytes R G B A
  };
  for (int d = 0; d < dirs; ++d) {
    const float g = float(d) * 6.2831853f / float(dirs), cg = std::cos(g), sg = std::sin(g);
    for (int f = 0; f < cuadros; ++f) {
      std::fill(col.begin(), col.end(), 0u);
      std::fill(zb.begin(), zb.end(), -1e30f);
      const ps2::V3* p = P.data() + size_t(f) * nv;
      const ps2::V3* n = N.data() + size_t(f) * nv;
      for (size_t t = 0; t < nt; ++t) {
        const ps2::Malla::Tri& tr = sk->malla.tris[t];
        if (tr.trans) continue;
        float sx[3], sy[3], sz[3], l[3], dn[3];
        bool normales = true;
        for (int q = 0; q < 3; ++q) {
          const ps2::V3 v = vista(p[t * 3 + q], cg, sg), nn = vista(n[t * 3 + q], cg, sg);
          sx[q] = axh + v.x * kh, sy[q] = ayh - v.y * kh, sz[q] = v.z;
          normales = normales && (tr.n[q].x != 0 || tr.n[q].y != 0 || tr.n[q].z != 0);
          dn[q] = nn.x * kLuz.x + nn.y * kLuz.y + nn.z * kLuz.z;
          l[q] = 0.5f + 0.5f * dn[q];
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::abs(area) < 1e-6f) continue;
        const auto* tramos = (normales && !sk->rampa.empty() && tr.tex >= 0 && size_t(tr.tex) < sk->rampa.size() && sk->rampa[size_t(tr.tex)].size() > 1)
                                 ? &sk->rampa[size_t(tr.tex)] : nullptr;
        const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]})))), x1 = std::min(hp - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]})))), y1 = std::min(hp - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        const float ia = 1.0f / area;
        for (int yy = y0; yy <= y1; ++yy) {
          const float qy = float(yy) + 0.5f;
          for (int xx = x0; xx <= x1; ++xx) {
            const float qx = float(xx) + 0.5f;
            const float b0 = ((sx[2] - sx[1]) * (qy - sy[1]) - (qx - sx[1]) * (sy[2] - sy[1])) * ia;
            const float b1 = ((sx[0] - sx[2]) * (qy - sy[2]) - (qx - sx[2]) * (sy[0] - sy[2])) * ia;
            const float b2 = 1.0f - b0 - b1;
            if (b0 < 0 || b1 < 0 || b2 < 0) continue;
            const size_t o = size_t(yy) * size_t(hp) + size_t(xx);
            const float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
            if (z <= zb[o]) continue;
            const ImVec2 uv(b0 * tr.uv[0].x + b1 * tr.uv[1].x + b2 * tr.uv[2].x, b0 * tr.uv[0].y + b1 * tr.uv[1].y + b2 * tr.uv[2].y);
            int ti = tr.tex;
            float tono = 1.0f;
            if (tramos) {   // el tramo de la rampa de B3 que toca a esta luz (tramos de menor a mayor l0; el primero cubre lo de debajo)
              const float ll = b0 * l[0] + b1 * l[1] + b2 * l[2];
              size_t s = 0;
              while (s + 1 < tramos->size() && ll >= (*tramos)[s + 1].first) ++s;
              ti = (*tramos)[s].second;
            } else if (normales) {   // los 3 tonos de Escena::Toon
              const float dd = b0 * dn[0] + b1 * dn[1] + b2 * dn[2];
              tono = dd > 0.25f ? 1.0f : dd > -0.25f ? 0.78f : 0.58f;
            }
            uint32_t tx = texel(ti, uv);
            if ((tx >> 24) < 64) continue;   // prueba de alfa
            if (tono < 1.0f) {
              const auto ch = [&](int s) { return uint32_t(float((tx >> s) & 0xFF) * tono) << s; };
              tx = ch(0) | ch(8) | ch(16);
            }
            col[o] = tx | 0xFF000000u;
            zb[o] = z;
          }
        }
      }
      // contorno: dilatacion (cuadrada, separable) del alfa en 3 px de doble resolucion = 1,5 px del atlas
      constexpr int kR = 3;
      for (int yy = 0; yy < hp; ++yy)
        for (int xx = 0; xx < hp; ++xx) {
          uint8_t a = 0;
          for (int q = std::max(0, xx - kR); q <= std::min(hp - 1, xx + kR) && !a; ++q) a = col[size_t(yy) * hp + q] ? 1 : 0;
          m1[size_t(yy) * hp + xx] = a;
        }
      for (int yy = 0; yy < hp; ++yy)
        for (int xx = 0; xx < hp; ++xx) {
          uint8_t a = 0;
          for (int q = std::max(0, yy - kR); q <= std::min(hp - 1, yy + kR) && !a; ++q) a = m1[size_t(q) * hp + xx];
          m2[size_t(yy) * hp + xx] = a;
        }
      for (size_t o = 0; o < col.size(); ++o)
        if (!col[o] && m2[o]) col[o] = 0xFF000000u;
      // reduccion 2x2 (media con alfa premultiplicado) a la celda (f, d) del atlas
      for (int yy = 0; yy < px; ++yy)
        for (int xx = 0; xx < px; ++xx) {
          float r = 0, gg = 0, b = 0, a = 0;
          for (int q = 0; q < kSs * kSs; ++q) {
            const uint32_t v = col[size_t(yy * kSs + q / kSs) * hp + size_t(xx * kSs + q % kSs)];
            const float al = float(v >> 24) / 255.0f;
            r += float(v & 0xFF) * al, gg += float((v >> 8) & 0xFF) * al, b += float((v >> 16) & 0xFF) * al, a += al;
          }
          if (a <= 0) continue;
          uint8_t* o = atlas->rgba.data() + ((size_t(d) * px + yy) * size_t(atlas->w) + size_t(f) * px + xx) * 4;
          o[0] = uint8_t(r / a + 0.5f), o[1] = uint8_t(gg / a + 0.5f), o[2] = uint8_t(b / a + 0.5f), o[3] = uint8_t(a / (kSs * kSs) * 255.0f + 0.5f);
        }
    }
  }
  const float e1 = std::clamp(sk->alto / 19.6f, 0.62f, 1.3f) / std::max(sk->alto, 1e-3f);   // escala de DibujarFiguraB3En con alto = 1
  if (ancla) ancla[0] = ax, ancla[1] = ay, ancla[2] = k / e1, ancla[3] = float(c->cuadros) / 60.0f;
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  REXLOG_INFO("dbz3 survivors horno: fid {} clip {} tono {:.0f} atlas {}x{} ({} cuadros x {} dirs, celda {} px, {:.2f} px por unidad de modelo, pies en ({:.1f}, {:.1f}), {} triangulos) en {:.1f} ms",
              fid, clip, tono, atlas->w, atlas->h, cuadros, dirs, px, k, ax, ay, nt, ms);
  return 2;
} 
// Resolver en el dibujo: los hilos del horno solo leen objetos ya publicados, sin consultar mapas ni tocar la GPU.
std::function<int(pasarela::Imagen*, float*)> PrepararHornoB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, float tono) {
  const SkB3* sk = nullptr;
  const Tk* tk = nullptr;
  if (TkEstado(b3, forma, &sk, &tk, fid) != 2) return {};
  return [=](pasarela::Imagen* atlas, float* ancla) { return HornearModeloB3(sk, tk, fid, clip, cuadros, dirs, px, picado, atlas, ancla, tono); };
}
int HornearFiguraB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, pasarela::Imagen* atlas, float* ancla, float tono) {
  const SkB3* sk = nullptr;
  const Tk* tk = nullptr;
  const int estado = TkEstado(b3, forma, &sk, &tk, fid);
  return estado != 2 || !atlas ? estado : HornearModeloB3(sk, tk, fid, clip, cuadros, dirs, px, picado, atlas, ancla, tono);
}
// la de la prueba tecnica (prueba.cpp), sin recolor
int HornearFiguraB3(int b3, int forma, int fid, int clip, int cuadros, int dirs, int px, float picado, pasarela::Imagen* atlas, float* ancla) {
  return HornearFiguraB3(b3, forma, fid, clip, cuadros, dirs, px, picado, atlas, ancla, 0.0f);
}
