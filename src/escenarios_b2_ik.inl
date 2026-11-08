// eB2f: IK de suelo de Budokai 2 (PAL FUN_001f2c50, llamada justo antes de FUN_001f68c0 en el
// bucle principal 001E54B0). Es la causa del giro extra del hueso STMC (y de unos grados en
// brazos/piernas) que PAL muestra con la victima tumbada, levantandose o aterrizando:
//   1. Cabeza (FUN_001EB5C0): si el cuello queda por debajo de suelo+2,0-y_actor, gira el
//      abuelo del cuello (STMC) el giro minimo cuello->objetivo (sin limite de angulo).
//   2. Pies NLF/NRF (umbral suelo+0,8) y manos NLA/NRA (suelo+0,7), FUN_001EB8F0: CCD sobre la
//      cadena (4 ancestros), alternando P4 y P2, giros con cos(mitad)<1 y >=0,6, tope 33 pasos.
// Validado fuera del juego contra matrices PAL (tumbado 0,2-1,5 grados, aterrizaje 0,2).
// Interruptor apagado: DBZ3_TEST_B2_IK_SUELO=1. Solo actua con la escena B2 activa.
// Se llama una vez por fotograma desde B2MotorHD::Paso, con las matrices ya finales (sub_82110AB8 se
// llama tambien a mitad de actualizacion y dejaba matrices a medio hacer: no sirve como punto de gancho).
namespace dbz3::escenarios::b2ik {
struct Esq {
  uint32_t sk=0;
  int n=0;
  std::vector<int> par;
  std::vector<std::vector<int>> sub;   // sub[j]: j y todos sus descendientes
  int cuello=-1,nra=-1,nla=-1,nrf=-1,nlf=-1;
  int stmc=-1,cabeza=-1;                // eB2g: mirada procedimental (STMC, NECK, HEAD)
  std::vector<std::array<double,3>> rt;  // eB2g: desplazamiento de reposo de cada hueso (sk->reposo+0x10)
  bool ok=false;
};
using M16=std::array<double,16>;
struct V3 {double x=0,y=0,z=0;};
inline V3 Pos(const M16& m) {return {m[12],m[13],m[14]};}
inline double Norma(V3 v) {return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
inline V3 Resta(V3 a,V3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 Unit(V3 v) {const double n=Norma(v);return n>1e-12?V3{v.x/n,v.y/n,v.z/n}:v;}
// vector fila por M3^T: l[c]=sum_k v[k]*M[c][k]
inline V3 ALocal(const M16& m,V3 v) {
  return {v.x*m[0]+v.y*m[1]+v.z*m[2],v.x*m[4]+v.y*m[5]+v.z*m[6],v.x*m[8]+v.y*m[9]+v.z*m[10]};
}
// Rotacion a->b (convencion columna, Rodrigues), 3x3 fila-mayor.
inline std::array<double,9> Entre(V3 a,V3 b) {
  a=Unit(a);b=Unit(b);
  const V3 c{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
  const double s=Norma(c),d=a.x*b.x+a.y*b.y+a.z*b.z;
  if(s<1e-9) return {1,0,0,0,1,0,0,0,1};
  const V3 k{c.x/s,c.y/s,c.z/s};
  const double ang=std::atan2(s,d),sn=std::sin(ang),cs=1-std::cos(ang);
  const double K[9]={0,-k.z,k.y,k.z,0,-k.x,-k.y,k.x,0};
  double K2[9];
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){double t=0;for(int q=0;q<3;++q)t+=K[i*3+q]*K[q*3+j];K2[i*3+j]=t;}
  std::array<double,9> r{};
  for(int i=0;i<9;++i)r[i]=(i%4==0?1.0:0.0)+sn*K[i]+cs*K2[i];
  return r;
}
// Gira el subarbol de j alrededor de su posicion: Rw = Rwc*Rq*Rwc^T (Rwc = M3^T de j).
inline std::vector<char>& Sucias() {static std::vector<char> v;return v;}
inline void Girar(std::vector<M16>& W,const Esq& e,int j,const std::array<double,9>& Rq) {
  const M16 mj=W[j];
  // Base ortonormal del hueso (Gram-Schmidt sobre las filas): sin esto el ruido float32 de las matrices
  // se amplifica x3 por giro en el CCD (33 giros -> matrices con escala; medido en el juego).
  V3 u[3]={{mj[0],mj[1],mj[2]},{mj[4],mj[5],mj[6]},{mj[8],mj[9],mj[10]}};
  for(int i=0;i<3;++i) {
    for(int k=0;k<i;++k){const double d=u[i].x*u[k].x+u[i].y*u[k].y+u[i].z*u[k].z;u[i]={u[i].x-d*u[k].x,u[i].y-d*u[k].y,u[i].z-d*u[k].z};}
    const double n=Norma(u[i]);
    if(!(n>0.5&&n<2.0))return;   // fila nula o con mucha escala: no es un hueso rigido
    u[i]={u[i].x/n,u[i].y/n,u[i].z/n};
  }
  double Rwc[9],T[9],Rw[9];
  for(int i=0;i<3;++i){Rwc[i*3+0]=(&u[0].x)[i];Rwc[i*3+1]=(&u[1].x)[i];Rwc[i*3+2]=(&u[2].x)[i];}
  for(int i=0;i<3;++i)for(int k=0;k<3;++k){double t=0;for(int q=0;q<3;++q)t+=Rwc[i*3+q]*Rq[size_t(q*3+k)];T[i*3+k]=t;}
  for(int i=0;i<3;++i)for(int k=0;k<3;++k){double t=0;for(int q=0;q<3;++q)t+=T[i*3+q]*Rwc[k*3+q];Rw[i*3+k]=t;}
  const V3 J=Pos(mj);
  for(const int d:e.sub[j]) {
    Sucias()[size_t(d)]=1;
    M16& m=W[d];
    double r[9];
    for(int i=0;i<3;++i)for(int k=0;k<3;++k){double t=0;for(int q=0;q<3;++q)t+=m[i*4+q]*Rw[k*3+q];r[i*3+k]=t;}
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)m[i*4+k]=r[i*3+k];
    const V3 p{m[12]-J.x,m[13]-J.y,m[14]-J.z};
    m[12]=J.x+p.x*Rw[0]+p.y*Rw[1]+p.z*Rw[2];
    m[13]=J.y+p.x*Rw[3]+p.y*Rw[4]+p.z*Rw[5];
    m[14]=J.z+p.x*Rw[6]+p.y*Rw[7]+p.z*Rw[8];
  }
}
inline bool Cabeza(std::vector<M16>& W,const Esq& e,double y,double suelo,double k=1.0) {   // k: escala del modelo HD respecto al B2 (eB2h, medido Goku 1,24)
  if(e.cuello<0)return false;
  const int j=e.par[e.par[e.cuello]];
  if(j<=0)return false;
  const V3 N=Pos(W[e.cuello]),J=Pos(W[j]);
  const double T=suelo+2.0*k-y;
  if(!std::isfinite(N.y)||!std::isfinite(J.y)||N.y>=T)return false;
  const V3 tgt{N.x,T,N.z};
  Girar(W,e,j,Entre(ALocal(W[j],Resta(N,J)),ALocal(W[j],Resta(tgt,J))));
  return true;
}
inline bool Extremidad(std::vector<M16>& W,const Esq& e,int idx,double umbral,double y,double suelo,double k=1.0) {
  if(idx<0)return false;
  const V3 E0=Pos(W[idx]);
  const double T=suelo+umbral*k-y;
  if(!std::isfinite(E0.y)||E0.y>=T)return false;
  const V3 tgt{E0.x,T,E0.z};
  const int p1=e.par[idx];if(p1<=0)return false;
  const int p2=e.par[p1];if(p2<=0)return false;
  const int p3=e.par[p2];if(p3<=0)return false;
  const int p4=e.par[p3];if(p4<=0)return false;
  int j=p4,fl=1,f8=-1;bool movido=false;
  for(int it=0;it<0x21;++it) {
    const V3 Jp=Pos(W[j]),E=Pos(W[idx]);
    const V3 v1=Resta(E,Jp),v2=Resta(tgt,Jp);
    if(Norma(v1)>1e-9&&Norma(v2)>1e-9) {
      const V3 l1=Unit(ALocal(W[j],v1)),l2=Unit(ALocal(W[j],v2));
      const V3 h{l1.x+l2.x,l1.y+l2.y,l1.z+l2.z};
      const double nh=Norma(h),w=nh>1e-9?(l1.x*h.x+l1.y*h.y+l1.z*h.z)/nh:0.0;
      if(w<1.0&&w>=0.6){Girar(W,e,j,Entre(l1,l2));movido=true;}
    }
    ++fl;
    j=e.par[e.par[j]];
    if(fl>1) {
      f8=movido?0:f8+1;
      if(f8>1)break;
      fl=0;movido=false;j=p2;
    }
    if(j<=0)break;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// eB2g: mirada procedimental del PS2 (FUN_001F6D80, llamada cada cuadro justo antes de 0013EE90 en 001E54B0).
// Con el bit 0x20 del flag de animacion (+0x924) activo, el luchador gira CUELLO y CABEZA hacia la cabeza del rival:
//   cuello: cuaternio local = nlerp(w, Rx(pitch_n), pose A);  pitch_n suaviza (0,1*k) hacia clamp(y/hypot(x,z),-0,3..1)*-35 grados
//   cabeza: cuaternio local = nlerp(w, Euler(pitch_h, yaw, 0), pose A); yaw suaviza (0,15*k) hacia atan2(x,z) (+-80 grados),
//           pitch_h hacia clamp(...)*-25 grados.  w sube 0,06*k por cuadro con el flag y baja sin el.
// El objetivo se expresa en el marco del hueso STMC (cuello) o de STMC girado por el cuello nuevo (cabeza).
// Validado contra PAL (traza g8 y g2) con pose A: error 0,0-0,5 grados salvo transitorios (<6).
struct MiradaEst {bool init=false;double w=0,pn=0,ph=0,sy=0;};
using Q4=std::array<double,4>;
using R3=std::array<double,9>;
inline R3 QMat(const Q4& q) {   // matriz de juego (filas) = q2R^T, sin normalizar (FUN_00145470)
  const double x=q[0],y=q[1],z=q[2],w=q[3];
  return {1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),
          2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),
          2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)};
}
inline Q4 QDeMat(const R3& m) {  // inversa de QMat para matrices ortonormales
  const double a[9]={m[0],m[3],m[6],m[1],m[4],m[7],m[2],m[5],m[8]};   // a = m^T = q2R(q)
  const double tr=a[0]+a[4]+a[8];Q4 q{};
  if(tr>0){const double s=std::sqrt(tr+1)*2;q={(a[7]-a[5])/s,(a[2]-a[6])/s,(a[3]-a[1])/s,0.25*s};}
  else if(a[0]>a[4]&&a[0]>a[8]){const double s=std::sqrt(1+a[0]-a[4]-a[8])*2;q={0.25*s,(a[1]+a[3])/s,(a[2]+a[6])/s,(a[7]-a[5])/s};}
  else if(a[4]>a[8]){const double s=std::sqrt(1+a[4]-a[0]-a[8])*2;q={(a[1]+a[3])/s,0.25*s,(a[5]+a[7])/s,(a[2]-a[6])/s};}
  else{const double s=std::sqrt(1+a[8]-a[0]-a[4])*2;q={(a[2]+a[6])/s,(a[5]+a[7])/s,0.25*s,(a[3]-a[1])/s};}
  return q;
}
inline Q4 QEuler(double a,double b,double c) {   // FUN_00144f60
  const double sa=std::sin(a/2),sb=std::sin(b/2),sc=std::sin(c/2),ca=std::cos(a/2),cb=std::cos(b/2),cc=std::cos(c/2);
  return {sa*cb*cc-ca*sb*sc,sa*cb*sc+ca*sb*cc,ca*cb*sc-sa*sb*cc,sa*sb*sc+ca*cb*cc};
}
inline Q4 NLerp(double w,Q4 q,const Q4& q0) {    // FUN_00145b70 con destino: mezcla SIN normalizar
  if(q0[0]*q[0]+q0[1]*q[1]+q0[2]*q[2]+q0[3]*q[3]<0)for(auto& v:q)v=-v;
  Q4 r;for(int i=0;i<4;++i)r[size_t(i)]=w*q[size_t(i)]+(1-w)*q0[size_t(i)];
  return r;
}
inline R3 Mul3(const R3& a,const R3& b) {R3 r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j){double t=0;for(int k=0;k<3;++k)t+=a[size_t(i*3+k)]*b[size_t(k*3+j)];r[size_t(i*3+j)]=t;}return r;}
inline R3 Rot3(const M16& m) {return {m[0],m[1],m[2],m[4],m[5],m[6],m[8],m[9],m[10]};}
inline R3 Inv3(const R3& m) {
  const double c0=m[4]*m[8]-m[5]*m[7],c1=m[5]*m[6]-m[3]*m[8],c2=m[3]*m[7]-m[4]*m[6];
  const double det=m[0]*c0+m[1]*c1+m[2]*c2;
  if(std::fabs(det)<1e-12)return {1,0,0,0,1,0,0,0,1};
  const double d=1/det;
  return {c0*d,(m[2]*m[7]-m[1]*m[8])*d,(m[1]*m[5]-m[2]*m[4])*d,
          c1*d,(m[0]*m[8]-m[2]*m[6])*d,(m[2]*m[3]-m[0]*m[5])*d,
          c2*d,(m[1]*m[6]-m[0]*m[7])*d,(m[0]*m[4]-m[1]*m[3])*d};
}
inline R3 Trans3(const R3& m) {return {m[0],m[3],m[6],m[1],m[4],m[7],m[2],m[5],m[8]};}
inline V3 VxM(V3 v,const R3& m) {return {v.x*m[0]+v.y*m[3]+v.z*m[6],v.x*m[1]+v.y*m[4]+v.z*m[7],v.x*m[2]+v.y*m[5]+v.z*m[8]};}
inline V3 Sum(V3 a,V3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
// Gira el subarbol de j alrededor de su posicion: R' = R*E, p' = J + (p-J)*E  (E = R_viejo^-1 * R_nuevo del hueso j).
inline void Rotar(std::vector<M16>& W,const Esq& e,int j,const R3& E) {
  const V3 J=Pos(W[size_t(j)]);
  for(const int d:e.sub[size_t(j)]) {
    Sucias()[size_t(d)]=1;
    M16& m=W[size_t(d)];
    const R3 r=Mul3(Rot3(m),E);
    m[0]=r[0];m[1]=r[1];m[2]=r[2];m[4]=r[3];m[5]=r[4];m[6]=r[5];m[8]=r[6];m[9]=r[7];m[10]=r[8];
    const V3 p=VxM({m[12]-J.x,m[13]-J.y,m[14]-J.z},E);
    m[12]=J.x+p.x;m[13]=J.y+p.y;m[14]=J.z+p.z;
  }
}
inline double Tope(double v) {return v<-0.3?-0.3:(v>1.0?1.0:v);}
// W: matrices de modelo (pose A). Mx: rotacion de colocacion del actor (mundo = Mx*v + pos; filas como en x+0..x+0x28).
// T: cabeza del rival en mundo. k: cuadros PS2 transcurridos (PAL 1,2; HD = delta de reloj). Devuelve true si modifico W.
inline bool Mirada(std::vector<M16>& W,const Esq& e,const R3& Mx,V3 pos,V3 T,double k,MiradaEst& st,int modo,bool reinicia) {
  const int s=e.stmc,n=e.cuello,h=e.cabeza;
  if(s<=0||n<=0||h<=0||e.par[size_t(n)]<=0||e.rt.size()<=size_t(h))return false;
  const R3 Pl=Trans3(Mx);                                  // v*Pl = Mx*v
  const R3 R8=Rot3(W[size_t(s)]);const V3 t8=Pos(W[size_t(s)]);
  const V3 rn{e.rt[size_t(n)][0],e.rt[size_t(n)][1],e.rt[size_t(n)][2]},rh{e.rt[size_t(h)][0],e.rt[size_t(h)][1],e.rt[size_t(h)][2]};
  const V3 on=Sum(VxM(rn,R8),t8);
  const R3 Rf=Mul3(R8,Pl);const V3 of=Sum(VxM(on,Pl),pos);
  auto coords=[&](const R3& Rm,V3 o){return VxM(Resta(T,o),Trans3(Rm));};
  // pose A: rotacion local de cuello y cabeza
  const int pn_=e.par[size_t(n)];
  const R3 Rn0=Rot3(W[size_t(n)]),Rp=Rot3(W[size_t(pn_)]);
  const R3 Rh0=Rot3(W[size_t(h)]);
  const Q4 qa=QDeMat(Mul3(Rn0,Trans3(Rp)));
  const Q4 qh0=QDeMat(Mul3(Rh0,Trans3(Rn0)));
  auto paso=[&](MiradaEst& q,double w,double k,Q4& qn,Q4& qh) {
    const V3 c=coords(Rf,of);
    const double d=std::hypot(c.x,c.z),yy=d>0?c.y/d:c.y;
    q.pn=Tope(yy)*-0.61086524*k*0.1+q.pn*(1-k*0.1);
    qn=NLerp(w,QEuler(q.pn,0,0),qa);
    const R3 Qn=QMat(qn);
    const V3 ohm=Sum(VxM(rh,Mul3(Qn,R8)),on);
    const R3 Rfh=Mul3(Mul3(Qn,R8),Pl);const V3 ofh=Sum(VxM(ohm,Pl),pos);
    const V3 c2=coords(Rfh,ofh);
    const double yaw=std::atan2(c2.x,c2.z);
    if(std::fabs(q.sy-yaw)<4.0142574){
      const double t=yaw<-1.3962634?-1.3962634:(yaw>1.3962634?1.3962634:yaw);
      q.sy=t*k*0.15+q.sy*(1-k*0.15);
    }
    const double d2=std::hypot(c2.x,c2.z),y2=d2>0?c2.y/d2:c2.y;
    q.ph=Tope(y2)*-0.43633232*k*0.1+q.ph*(1-k*0.1);
    qh=NLerp(w,QEuler(q.ph,q.sy,0),qh0);
  };
  Q4 qn,qh;
  if(reinicia||!st.init) {   // el estado previo del PS2 ya convergio al objetivo cuando empieza la escena
    MiradaEst t;
    for(int i=0;i<300;++i)paso(t,1.0,1.0,qn,qh);
    st.pn=t.pn;st.ph=t.ph;st.sy=t.sy;st.init=true;
  }
  // modo: 1 = flag 0x20 activo (w sube), 0 = sin flag (w baja), 2 = cambio de estado (FUN_001F6D60 pone w=0)
  st.w=modo==1?std::min(1.0,st.w+0.06*k):(modo==2?0.0:std::max(0.0,st.w-0.06*k));
  paso(st,st.w,k,qn,qh);
  if(st.w<=0)return false;
  const R3 Qn=QMat(qn);
  const R3 Rn1=Mul3(Qn,Rp);
  Rotar(W,e,n,Mul3(Inv3(Rn0),Rn1));
  const R3 Rh1=Mul3(QMat(qh),Rn1);
  Rotar(W,e,h,Mul3(Inv3(Rot3(W[size_t(h)])),Rh1));
  return true;
}
inline bool Sufijo(uint8_t* base,uint32_t p,const char* fin) {
  char n[33]={};
  for(unsigned i=0;i<32;++i)n[i]=char(REX_LOAD_U8(p+i));
  const char* s=std::strchr(n,'_');
  return s&&!std::strcmp(s+1,fin);
}
inline bool Esqueleto(uint8_t* base,uint32_t mod,Esq& e) {
  const uint32_t sk=REX_LOAD_U32(mod+4);
  if(sk<0x10000000u||sk>=0xeffff000u)return false;
  if(e.sk==sk)return e.ok;
  e=Esq{};e.sk=sk;
  const uint32_t n=REX_LOAD_U32(sk+0x10),rest=REX_LOAD_U32(sk+0x14),names=REX_LOAD_U32(sk+0x1c);
  if(n<8||n>128||rest<0x10000000u||rest>=0xeffff000u||names<0x10000000u||names>=0xeffff000u)return false;
  e.n=int(n);     // filas 0..n-1 (la 0 es la raiz XBODY); PAL: n=42, mas matrices reservadas que filas
  e.par.assign(e.n,-1);
  for(int b=1;b<e.n;++b) {
    const uint32_t pp=REX_LOAD_U32(rest+0x50u*uint32_t(b)+0x40);
    if(pp>=rest&&pp<rest+0x50u*uint32_t(e.n)&&(pp-rest)%0x50u==0)e.par[b]=int((pp-rest)/0x50u);
  }
  for(int b=0;b<e.n;++b) {
    const uint32_t nm=names+0x20u*uint32_t(b);
    if(Sufijo(base,nm,"NECK"))e.cuello=b;
    else if(Sufijo(base,nm,"NRA"))e.nra=b;
    else if(Sufijo(base,nm,"NLA"))e.nla=b;
    else if(Sufijo(base,nm,"NRF"))e.nrf=b;
    else if(Sufijo(base,nm,"NLF"))e.nlf=b;
    else if(Sufijo(base,nm,"STMC"))e.stmc=b;
    else if(Sufijo(base,nm,"HEAD"))e.cabeza=b;
  }
  e.rt.assign(size_t(e.n),{0,0,0});
  for(int b=0;b<e.n;++b)for(unsigned k=0;k<3;++k)e.rt[size_t(b)][k]=double(dbz3::escenarios::LeeFl(rest+0x50u*uint32_t(b)+0x10u+4u*k));
  e.sub.assign(e.n,{});
  for(int j=0;j<e.n;++j) {
    e.sub[j].push_back(j);
    for(int d=0;d<e.n;++d) {
      if(d==j)continue;
      for(int a=e.par[d],g=0;a>0&&g<64;a=e.par[a],++g)if(a==j){e.sub[j].push_back(d);break;}
    }
  }
  e.ok=e.cuello>0&&e.nra>0&&e.nla>0&&e.nrf>0&&e.nlf>0;
  {
    std::string pl;
    for(int b=0;b<e.n;++b) {
      char nm[33]={};for(unsigned i=0;i<32;++i)nm[i]=char(REX_LOAD_U8(names+0x20u*uint32_t(b)+i));
      pl+=std::to_string(b)+":"+nm+"<-"+std::to_string(e.par[size_t(b)])+" ";
    }
    REXLOG_INFO("dbz3 B2 IK suelo: jerarquia {}",pl);
  }
  REXLOG_INFO("dbz3 B2 IK suelo: esqueleto {:08X} n={} cuello={} NRA={} NLA={} NRF={} NLF={} ok={}",sk,n,e.cuello,e.nra,e.nla,e.nrf,e.nlf,e.ok);
  return e.ok;
}
inline bool Activo() {
  static const bool v=[]{const char* s=std::getenv("DBZ3_TEST_B2_IK_SUELO");return (s&&*s=='1')||::dbz3::original::Arreglo("ik_b2");}();
  return v;
}
inline double EscalaIK() {   // eB2h DBZ3_TEST_B2_IK_ESCALA=<k>: umbrales de suelo del IK x k (el modelo HD mide ~1,24 veces el B2); 1 = PAL literal
  static const double v=[]{const char* s=std::getenv("DBZ3_TEST_B2_IK_ESCALA");const double k=s?std::atof(s):1.0;return k>0.5&&k<2.0?k:1.0;}();
  return v;
}
inline double EscalaLevantar() {   // eB2h DBZ3_TEST_B2_IK_ESCALA_LEVANTAR=<k>: escala en los estados de levantarse (HD 166/172); la raiz HD queda ~0,7 mas alta que la B2 escalada. 0 = la misma que EscalaIK
  static const double v=[]{const char* s=std::getenv("DBZ3_TEST_B2_IK_ESCALA_LEVANTAR");const double k=s?std::atof(s):0.0;return k>0.5&&k<3.0?k:(EscalaIK()>1.0?EscalaIK()*1.21:0.0);}();   // por defecto 1,21 x EscalaIK (medido 1,5 con 1,24)
  return v;
}
struct IkB3 {uint32_t x=0;uint8_t v=0;};
inline std::array<IkB3,2>& IkB3Guardado() {static std::array<IkB3,2> g{};return g;}
void RestaurarIkB3(uint8_t* base) {   // fin de escena: devuelve x+8190 a su valor (la batalla vuelve a tener su IK B3)
  for(auto& g:IkB3Guardado())if(g.x){REX_STORE_U8(g.x+8190,g.v);g=IkB3{};}
}
inline bool SinIkB3() {   // eB2i DBZ3_TEST_B2_SIN_IK_B3=1: apaga el IK de suelo nativo de B3 (sub_8211B1F0) en la escena B2
  static const bool v=[]{const char* s=std::getenv("DBZ3_TEST_B2_SIN_IK_B3");return (s&&*s=='1')||::dbz3::original::Arreglo("ik_b2");}();
  return v;
}
// P3 7/11: umbrales de la tabla PAL por ficha (= indice de personaje de B2, filas 0..52 de DAT_00389380 / DAT_00389460, leidas del ELF
// SLES_518.39 y validadas contra b2_ik_tabla_pal.json). La ficha del luchador sale de su ID de B3 (mgr+1712+80*lado) por la tabla de la pasarela.
inline constexpr double kIkPie[53]={1.0,1.1,0.8,0.9,0.8,1.1,1.2,1.0,1.0,1.1,2.2,1.2,1.8,1.3,1.5,1.2,1.5,1.1,1.0,1.1,1.5,1.1,0.8,0.8,1.0,1.0,0.8,0.8,1.0,1.4,1.4,0.9,1.25,1.1,0.9,0.8,0.8,1.0,1.0,1.0,1.1,1.0,1.0,1.5,0.35,0.8,1.0,1.4,1.4,1.4,1.4,1.4,1.4};
inline constexpr double kIkMano[53]={0.8,0.8,0.6,0.7,0.6,0.8,0.8,0.8,0.8,0.9,1.5,1.0,1.2,0.8,0.8,0.8,1.0,0.8,0.7,0.8,1.0,0.8,0.7,0.7,0.8,0.8,0.7,0.7,0.8,1.1,1.0,0.7,0.8,0.8,0.7,0.7,0.7,0.8,0.8,0.8,0.8,0.8,0.8,0.8,0.8,0.7,0.8,1.0,1.0,1.0,1.0,1.0,1.0};
}
namespace dbz3::pasarela::b2 {int FichaDeB3(int id_b3);}   // pasarela_b2.cpp
namespace dbz3::escenarios::b2ik {
// false = ficha desconocida: se quedan los valores de siempre. DBZ3_TEST_B2_IK_FICHA=0 la apaga (A/B con el mismo exe).
inline bool FilaFicha(uint8_t* base,uint32_t x,uint32_t mgr,double& pie,double& mano) {
  static const bool off=[]{const char* s=std::getenv("DBZ3_TEST_B2_IK_FICHA");return s&&*s=='0';}();
  if(off||!mgr)return false;
  const int id=int(REX_LOAD_U16(mgr+1712u+80u*(x==mgr+2096u?0u:1u)));
  const int f=::dbz3::pasarela::b2::FichaDeB3(id);
  static bool logeado[2]{};
  const int lado=x==mgr+2096u?0:1;
  if(!logeado[lado]){logeado[lado]=true;REXLOG_INFO("dbz3 B2 IK: lado {} id B3 {} ficha B2 {}",lado,id,f);}
  if(f<0||f>=53)return false;
  pie=kIkPie[f];mano=kIkMano[f];return true;
}
void AplicarActor(uint8_t* base,uint32_t x) {
  if(!Activo())return;
  auto& m=dbz3::escenarios::g_b2_motor;
  if(!m.activo||(x!=m.actor[0]&&x!=m.actor[1]))return;
  // eB2i: HD aplica ADEMAS su IK de suelo B3 (sub_8211B1F0, llamado desde 8210E8E8 si x+8190==0; vigia de escritura 6/11 en Nieve 1290:
  // es quien gira el STMC 48,9 grados sobre el clip). PAL no tiene ese IK con ese suelo; con el IK PAL portado aqui, el B3 sobra.
  // Igual que FUN_001f2c50 de PAL se salta con actor+0x9ef9 != 0. Se escribe cada cuadro (el codigo nativo lo repone al cambiar de estado).
  if(SinIkB3()) {
    auto& g=IkB3Guardado()[x==m.actor[1]?1:0];
    if(g.x!=x){g.x=x;g.v=REX_LOAD_U8(x+8190);}
    REX_STORE_U8(x+8190,1);
  }
  const uint32_t mod=REX_LOAD_U32(x+2952);
  if(mod<0x10000000u||mod>=0xeffff000u)return;
  static Esq esq[2];
  Esq& e=esq[x==m.actor[1]?1:0];
  if(!Esqueleto(base,mod,e))return;
  const uint32_t mats=REX_LOAD_U32(mod+0x408);
  if(mats<0x10000000u||mats>=0xeffff000u)return;
  std::vector<M16> W(size_t(e.n));
  for(int b=0;b<e.n;++b)for(int k=0;k<16;++k)W[size_t(b)][size_t(k)]=double(dbz3::escenarios::LeeFl(mats+0x40u*uint32_t(b)+4u*uint32_t(k)));
  Sucias().assign(size_t(e.n),0);
  double y=double(dbz3::escenarios::LeeFl(x+68)),suelo=double(dbz3::escenarios::LeeFl(x+6216));
  // Llanura: la trayectoria HD pasa por y=-220 (bajo el suelo que informa x+6216); PAL no tiene ese caso. Sin esta guarda el
  // umbral (suelo+2-y) se dispara y el CCD deforma el vuelo. El aterrizaje real llega a y=-1,6.
  // eB2h: prueba, el atacante colgado a y=12,6 en los estados finales 1285..1291 (Nieve 1290): se supone que PAL tiene ahi suelo=y
  static const bool sf=[]{const char* e=std::getenv("DBZ3_TEST_B2_IK_SUELO_FIN");return e&&*e=='1';}();
  const uint32_t cod0=REX_LOAD_U32(x+2928);
  if(sf&&x==m.actor[0]&&cod0>=1285&&cod0<=1291)suelo=y;
  if(y<suelo-8.0)return;
  const uint32_t cod=REX_LOAD_U32(x+2928);
  // eB2i: sin el IK B3 de por medio (SIN_IK_B3) el atacante va mejor con los umbrales PAL literales (Nieve 547 -> 696 de 719 con k=1);
  // la victima sigue necesitando la escala (k=1: Ciudad 340 -> 334, Llanura 497 -> 480, Base 168 -> 157). Medido con Goku contra Goku.
  const double k=(SinIkB3()&&x==m.actor[0])?1.0:((EscalaLevantar()>0&&(cod==166||cod==172))?EscalaLevantar():EscalaIK());
  const bool h=Cabeza(W,e,y,suelo,k);
  bool l=false;
  // eB2i DBZ3_TEST_B2_IK_TABLA=1: umbrales de la tabla PAL por personaje (FUN_001f2c50: pies DAT_00389380[actor+0x754], manos DAT_00389460[...]);
  // en la prueba Goku contra Goku el atacante es la fila 0 (1,0 / 0,8) y la victima la 22 (0,8 / 0,7, los de siempre).
  // ponytail: filas medidas en esta prueba; para otros personajes hace falta la fila de su ficha (actor+0x754 del B2).
  static const bool tabla=[]{const char* s=std::getenv("DBZ3_TEST_B2_IK_TABLA");return (s&&*s=='1')||::dbz3::original::Arreglo("ik_b2");}();
  const bool fila0=tabla&&x==m.actor[0];
  double pie=fila0?1.0:0.8,mano=fila0?0.8:0.7;
  if(tabla)FilaFicha(base,x,m.mgr,pie,mano);
  l|=Extremidad(W,e,e.nlf,pie,y,suelo,k);
  l|=Extremidad(W,e,e.nrf,pie,y,suelo,k);
  l|=Extremidad(W,e,e.nla,mano,y,suelo,k);
  l|=Extremidad(W,e,e.nra,mano,y,suelo,k);
  // eB2h: Nieve, atacante en 1290 (reloj 450-570): PAL tiene el STMC girado un angulo constante sobre su eje x local respecto al clip (medido
  // +64,4 sobre el clip nativo HD 0x8000a (se aplica 49,4 en Girar: es lo que reduce el error a 0), +48,9 sobre el B2 172; piernas y cintura a 0,0). Origen en PAL sin identificar.
  static const double tronco=[]{const char* e=std::getenv("DBZ3_TEST_B2_TRONCO_FIN");return e?std::atof(e):0.0;}();
  bool t=false;
  if(tronco!=0.0&&x==m.actor[0]&&cod0==1290&&e.stmc>0) {
    const double a=tronco*3.14159265358979323846/180.0,c=std::cos(a),sn=std::sin(a);
    Girar(W,e,e.stmc,{1,0,0,0,c,-sn,0,sn,c});t=true;
  }
  static unsigned n=0;
  if(h||l||t) {
    bool fin=true;
    for(int b=0;b<e.n&&fin;++b)if(Sucias()[size_t(b)])for(int k=0;k<16;++k)if(!std::isfinite(W[size_t(b)][size_t(k)])||std::fabs(W[size_t(b)][size_t(k)])>1e5){fin=false;break;}
    static unsigned malas=0;
    if(!fin) {
      if(malas++<8)REXLOG_WARN("dbz3 B2 IK suelo: resultado no finito (actor={:08X} y={:.3f} cabeza={} extremidades={}); no se escribe",x,y,h,l);
      return;
    }
    for(int b=0;b<e.n;++b)if(Sucias()[size_t(b)])for(int k=0;k<16;++k) {
      const float f=float(W[size_t(b)][size_t(k)]);uint32_t u;std::memcpy(&u,&f,4);
      REX_STORE_U32(mats+0x40u*uint32_t(b)+4u*uint32_t(k),u);
    }
    if(n<40){++n;REXLOG_INFO("dbz3 B2 IK suelo: actor={:08X} y={:.3f} suelo={:.3f} cabeza={} extremidades={}",x,y,suelo,h,l);}
  }
}
inline bool ActivoMirada() {
  static const bool v=[]{const char* s=std::getenv("DBZ3_TEST_B2_MIRADA");return (s&&*s=='1')||::dbz3::original::Arreglo("mirada_b2");}();
  return v;
}
// eB2g: mirada procedimental del atacante (ver Mirada). Interruptor DBZ3_TEST_B2_MIRADA=1 (apagado por defecto).
// Se llama una vez por cuadro desde B2MotorHD::Paso, ANTES del IK de suelo (en PAL 001F6D80 va antes de 0013EE90 y del IK).
void AplicarMirada(uint8_t* base,uint32_t x,uint32_t rival,double cuadro) {
  if(!ActivoMirada())return;
  auto& m=dbz3::escenarios::g_b2_motor;
  if(!m.activo||x!=m.actor[0]||rival!=m.actor[1])return;
  static Esq esq,esr;
  static MiradaEst st;
  static double ultimo=-1e9;
  auto mats_de=[&](uint32_t a,Esq& e,std::vector<M16>& W)->uint32_t {
    const uint32_t mod=REX_LOAD_U32(a+2952);
    if(mod<0x10000000u||mod>=0xeffff000u||!Esqueleto(base,mod,e))return 0;
    const uint32_t mats=REX_LOAD_U32(mod+0x408);
    if(mats<0x10000000u||mats>=0xeffff000u)return 0;
    W.assign(size_t(e.n),M16{});
    for(int b=0;b<e.n;++b)for(int k=0;k<16;++k)W[size_t(b)][size_t(k)]=double(dbz3::escenarios::LeeFl(mats+0x40u*uint32_t(b)+4u*uint32_t(k)));
    return mats;
  };
  std::vector<M16> W,Wr;
  const uint32_t mats=mats_de(x,esq,W);
  if(!mats||mats_de(rival,esr,Wr)==0||esr.cabeza<=0)return;
  if(cuadro+0.5<ultimo)st=MiradaEst{};   // reloj reiniciado: escena nueva
  const double k=ultimo<-1e8?1.0:std::min(4.0,std::max(0.0,cuadro-ultimo));
  ultimo=cuadro;
  if(k<=0)return;
  auto Mx_de=[&](uint32_t a){R3 r;for(int j=0;j<3;++j)for(int c=0;c<3;++c)r[size_t(j*3+c)]=double(dbz3::escenarios::LeeFl(a+16u*uint32_t(j)+4u*uint32_t(c)));return r;};
  auto Tr_de=[&](uint32_t a){return V3{double(dbz3::escenarios::LeeFl(a+48)),double(dbz3::escenarios::LeeFl(a+52)),double(dbz3::escenarios::LeeFl(a+56))};};
  const R3 Mx=Mx_de(x),Mr=Mx_de(rival);
  const M16& hr=Wr[size_t(esr.cabeza)];
  V3 T=VxM(V3{hr[12],hr[13],hr[14]},Trans3(Mr));    // Mr*v
  const V3 tr=Tr_de(rival);T={T.x+tr.x,T.y+tr.y,T.z+tr.z};
  const uint32_t cod=REX_LOAD_U32(x+2928);
  // PAL: flag 0x20 desde el reloj 15,6 del ataque (573) y en el reposo (0); otro estado = w a 0 (medido, traza g8/g2).
  const int modo=(cod==574||cod==0)?(cuadro>=15.6?1:0):2;
  Sucias().assign(size_t(esq.n),0);
  if(!Mirada(W,esq,Mx,Tr_de(x),T,k,st,modo,false))return;
  for(int b=0;b<esq.n;++b)if(Sucias()[size_t(b)])for(int c=0;c<16;++c)if(!std::isfinite(W[size_t(b)][size_t(c)])||std::fabs(W[size_t(b)][size_t(c)])>1e5)return;
  for(int b=0;b<esq.n;++b)if(Sucias()[size_t(b)])for(int c=0;c<16;++c) {
    const float f=float(W[size_t(b)][size_t(c)]);uint32_t u;std::memcpy(&u,&f,4);
    REX_STORE_U32(mats+0x40u*uint32_t(b)+4u*uint32_t(c),u);
  }
  static unsigned n=0;
  if(n<12||(n%200)==0)REXLOG_INFO("dbz3 B2 mirada: cuadro={:.1f} codigo={} modo={} w={:.2f} pn={:.1f} ph={:.1f} yaw={:.1f}",cuadro,cod,modo,st.w,st.pn*57.2958,st.ph*57.2958,st.sy*57.2958);
  ++n;
}
}  // namespace dbz3::escenarios::b2ik
