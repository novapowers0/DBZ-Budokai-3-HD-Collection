#pragma once
namespace dbz3::iwbubbles::wcf {
inline bool Backend::Load(const std::uint8_t* b,std::size_t size,Bounds bounds){
  nodes_.clear();triangles_=0;error_.clear();bounds_=bounds;
  auto range=[&](std::size_t p,std::size_t n){return p<=size&&n<=size-p;};
  auto U=[&](std::size_t p){std::uint32_t v=0;if(range(p,4))std::memcpy(&v,b+p,4);return v;};
  auto H=[&](std::size_t p){std::uint16_t v=0;if(range(p,2))std::memcpy(&v,b+p,2);return v;};
  auto F=[&](std::size_t p){float v=0;if(range(p,4))std::memcpy(&v,b+p,4);return v;};
  auto Vec=[&](std::size_t p){return V{F(p),F(p+4),F(p+8)};};
  auto fail=[&](const char* e){error_=e;nodes_.clear();triangles_=0;return false;};
  if(!b||!range(0,16)||std::memcmp(b,"#WCF",4))return fail("Se requiere WCF RAW");
  const std::uint32_t root=U(12),count=U(8)>>16;
  if(!count||!root||!range(root,std::size_t(count)*0x90))return fail("Tabla de nodos fuera de rango");
  nodes_.resize(count);
  for(unsigned i=0;i<count;++i){
    const std::size_t o=root+std::size_t(i)*0x90;Node& n=nodes_[i];n.off=std::uint32_t(o);
    //897 usa hueso0 en los218 nodos. No inventar matrices de otro campo/actor.
    if(H(o+0x20)!=0)return fail("Falta matriz externa de hueso WCF");
    n.flags=H(o+0x22);n.count=b[o+0x25];n.tile=b[o+0x27];
    if(n.tile>8)return fail("Indice de copia WCF desconocido");
    const auto ch=U(o+0x2c);
    if(n.count){if(ch<root||(ch-root)%0x90||!range(ch,std::size_t(n.count)*0x90)||std::size_t(ch-root)+std::size_t(n.count)*0x90>std::size_t(count)*0x90)return fail("Hijos invalidos");n.child=(ch-root)/0x90;}
    for(unsigned k=0;k<16;++k)n.matrix[k]=F(o+0x50+4*k);
    //BuildPartMatrices: u32*+0xC/+0x10 = BYTES+30/+40; salidas+0/+10.
    const V localSphere=Vec(o+0x30),localSummary=Vec(o+0x40);
    n.sphere=n.tile?Transform(n,localSphere):Vec(o);
    n.summary=n.tile?Transform(n,localSummary):Vec(o+0x10);
    n.radius=F(o+0x0c);n.summaryRadius=F(o+0x1c);
    const auto table=U(o+0x28);const unsigned planes=b[o+0x24];
    if(planes&&!range(table,std::size_t(planes)*32))return fail("Planos invalidos");
    for(unsigned j=0;j<planes;++j){
      const auto q=table+32*j;Plane p;p.off=q;p.normal=Vec(q);p.d=F(q+12);p.mask=U(q+20);
      std::uint32_t g=U(q+16);unsigned guard=0;
      while(range(g,8)&&guard++<size/8){
        const unsigned nt=H(g+2);const auto mask=H(g+4);
        if(!nt||!(mask&7u))break; //las consultas Bubbles solo usan2/7; no seguir datos ajenos.
        if(!range(g+8,std::size_t(nt)*16))return fail("Grupo WCF invalido");
        Group group;group.off=g;group.mask=mask;group.type=H(g+6);
        for(unsigned t=0;t<nt;++t){
          const auto e=g+8+16*t;const auto va=U(e),vb=U(e+4),vc=U(e+8);
          if(!range(va,16)||!range(vb,16)||!range(vc,16))return fail("Vertice WCF invalido");
          group.triangles.push_back({Vec(va),Vec(vb),Vec(vc),F(e+12),e});++triangles_;
        }
        p.groups.push_back(std::move(group));g+=8+16*nt;
      }
      n.planes.push_back(std::move(p));
    }
  }
  return true;
}
inline V Backend::Transform(const Node& n,V p)const{
  const auto& m=n.matrix;
  return {((m[0]*p.x+m[4]*p.y)+m[8]*p.z)+m[12]+n.shift.x,
          ((m[1]*p.x+m[5]*p.y)+m[9]*p.z)+m[13]+n.shift.y,
          ((m[2]*p.x+m[6]*p.y)+m[10]*p.z)+m[14]+n.shift.z};
}
inline void Backend::Copies(V query){
  const float ex=bounds_.maxX-bounds_.minX,ez=bounds_.maxZ-bounds_.minZ;
  const float cx=(bounds_.minX+bounds_.maxX)*.5f,cz=(bounds_.minZ+bounds_.maxZ)*.5f;
  std::array<int,9> bits{};std::array<V,9> shifts{};
  if(bounds_.flags&1){
    if(query.x>cx+ex*.25f)for(unsigned i:{1u,4u,6u}){bits[i]|=1;shifts[i].x=ex;}
    else if(query.x<cx-ex*.25f)for(unsigned i:{3u,5u,8u}){bits[i]|=1;shifts[i].x=-ex;}
  }
  if(bounds_.flags&2){
    if(query.z>cz+ez*.25f)for(unsigned i:{1u,2u,3u}){bits[i]|=2;shifts[i].z=ez;}
    else if(query.z<cz-ez*.25f)for(unsigned i:{6u,7u,8u}){bits[i]|=2;shifts[i].z=-ez;}
  }
  for(Node& n:nodes_){
    const int b=bits[n.tile];if(n.cacheBits==b)continue;
    const V delta=Sub(shifts[n.tile],n.shift);n.sphere=Add(n.sphere,delta);n.summary=Add(n.summary,delta);n.shift=shifts[n.tile];
    //Original solo marca26 para copias1..8; los planos de copia0 ya son mundo.
    if(n.tile)for(Plane& p:n.planes)if(!p.groups.empty()&&!p.groups[0].triangles.empty()){
      const Triangle& t=p.groups[0].triangles[0];V a=Transform(n,t.a),b0=Transform(n,t.b),c=Transform(n,t.c);
      p.normal=Unit(Cross(Sub(b0,a),Sub(c,a)));p.d=-Dot(p.normal,a);
    }
    n.cacheBits=b;
  }
}
inline void Backend::SphereCandidates(unsigned i,V p,float r,std::vector<unsigned>& out)const{
  const Node& n=nodes_[i];
  if(!(n.flags&0x8000)&&!n.planes.empty()&&Dot(Sub(p,n.sphere),Sub(p,n.sphere))<=(n.radius+r)*(n.radius+r))out.push_back(i);
  if(n.count>=2&&Dot(Sub(p,n.summary),Sub(p,n.summary))>(n.summaryRadius+r)*(n.summaryRadius+r))return;
  for(unsigned j=0;j<n.count;++j)SphereCandidates(n.child+j,p,r,out);
}
inline void Backend::PointCandidates(unsigned i,V p,std::vector<Candidate>& out)const{
  const Node& n=nodes_[i];
  if(!(n.flags&0x8000)&&!n.planes.empty()&&Dot(Sub(p,n.sphere),Sub(p,n.sphere))<=n.radius*n.radius)out.push_back({0,i});
  if(n.count>=2&&Dot(Sub(p,n.summary),Sub(p,n.summary))>n.summaryRadius*n.summaryRadius)return;
  for(unsigned j=0;j<n.count;++j)PointCandidates(n.child+j,p,out);
}
inline void Backend::RayCandidates(unsigned i,V from,V dir,float len,std::vector<Candidate>& out)const{
  const Node& n=nodes_[i];
  auto intersects=[&](V c,float r,float& along,float& perp2){
    const V q=Sub(c,from),cr=Cross(q,dir);perp2=Dot(cr,cr);along=Dot(q,dir);
    if(r*r<perp2)return false;
    if(along<0&&r*r<along*along+perp2)return false;
    if(along>len&&r*r<(along-len)*(along-len)+perp2)return false;
    return true;
  };
  float along=0,p2=0;
  if(!(n.flags&0x8000)&&!n.planes.empty()&&intersects(n.sphere,n.radius,along,p2)){
    const int key=int(-along+(n.radius-std::sqrt(p2)));
    auto pos=out.begin();while(pos!=out.end()&&pos->key>=key)++pos;out.insert(pos,{key,i});
  }
  if(n.count>=2&&!intersects(n.summary,n.summaryRadius,along,p2))return;
  for(unsigned j=0;j<n.count;++j)RayCandidates(n.child+j,from,dir,len,out);
}
inline void Backend::RootsSphere(V p,float r,std::vector<unsigned>& out)const{
  if(nodes_.empty())return;for(unsigned j=0;j<nodes_[0].count;++j)SphereCandidates(nodes_[0].child+j,p,r,out);
}
inline void Backend::RootsRay(V a,V b,std::vector<Candidate>& out)const{
  if(nodes_.empty())return;const float len=Len(Sub(b,a));const V dir=Unit(Sub(b,a));
  for(unsigned j=0;j<nodes_[0].count;++j){
    if(len==0)PointCandidates(nodes_[0].child+j,b,out);else RayCandidates(nodes_[0].child+j,a,dir,len,out);
  }
}
inline Contact Backend::MakeContact(const Node& n,const Plane& p,const Group& g,const Triangle& t)const{
  return {p.normal,p.d,g.mask,g.type,n.off,p.off,g.off,t.off};
}
inline bool Backend::Inside(V p,const Triangle& t){
  const V a=Sub(t.a,p),b=Sub(t.b,p),c=Sub(t.c,p);
  return (Len(Cross(a,c))+Len(Cross(b,a)))+Len(Cross(c,b))<=1.05f*t.area;
}
inline V Backend::Merge(V old,V candidate){
  const float len=Len(old);if(len==0)return candidate;
  const V axis=Unit(old);const float dot=Dot(axis,candidate);
  const V side=Cross(Cross(axis,candidate),axis);
  if(dot<0)old=Mul(axis,len+dot);else if(len<dot)old=Mul(axis,dot);
  return Add(old,side);
}
inline Result Backend::Ray(V from,V to,unsigned mask,bool trace){
  Result result;result.point=to;if(nodes_.empty())return result;
  Copies(from);std::vector<Candidate> candidates;RootsRay(from,to,candidates);mask&=0x7fff;
  const float full=Len(Sub(to,from));
  for(const Candidate& cn:candidates){const Node& n=nodes_[cn.index];
    for(const Plane& p:n.planes){if(!(p.mask&mask))continue;
      const float start=p.d+Dot(from,p.normal),end=p.d+Dot(to,p.normal),product=start*end;
      if(product>0)continue;
      float sign=start<0?-1.f:1.f,score=0;V point;
      if(product<0){if(start<0)continue;const float sum=std::fabs(start)+std::fabs(end);
        point=Add(from,Mul(Sub(to,from),std::fabs(start)/sum));score=std::fabs(end)/sum;
      }else if(start==0&&end==0){point=to;score=0;}
      else if(start==0){if(end>0)continue;point=from;score=1;}
      else{point=to;score=0;}
      for(const Group& g:p.groups){if(!(g.mask&mask))break;
        for(const Triangle& raw:g.triangles){Triangle t{Transform(n,raw.a),Transform(n,raw.b),Transform(n,raw.c),raw.area,raw.off};
          if(!Inside(point,t)||!(result.score<score))continue;
          Contact c=MakeContact(n,p,g,raw);c.material=std::uint16_t(p.mask); //2E6A40: material del PLANO; sweep usa grupo.
          c.normal=Mul(c.normal,sign);c.d*=sign;
          result.hit=true;result.point=point;result.contact=c;result.score=score;result.distance=score*full;
          if(trace)result.trace.push_back({c,{},true,true});
        }
      }
    }
  }
  return result;
}
inline Result Backend::Sweep(V from,V to,float radius,unsigned mask,bool trace){
  Result result;result.point=to;if(nodes_.empty()||radius<0)return result;
  Copies(to);std::vector<unsigned> candidates;RootsSphere(to,radius,candidates);mask&=0x7fff;
  V totalCorrection{};float selectedScore=-1;bool selectedFace=false;
  for(unsigned ni:candidates){const Node& n=nodes_[ni];
    for(const Plane& p:n.planes){if(!(p.mask&mask))continue;
      const float end=p.d+Dot(to,p.normal),start=p.d+Dot(from,p.normal);
      if(end>radius||Dot(Sub(to,from),p.normal)>0||!(start>0))continue;
      V impact=to,projection;float planeDistance,penetration;
      if(start*end>0||start==0){planeDistance=std::fabs(end);penetration=radius-planeDistance;projection=Sub(to,Mul(p.normal,end));}
      else{impact=Add(from,Mul(Sub(to,from),std::fabs(start)/(std::fabs(start)+std::fabs(end))));
        planeDistance=0;penetration=Len(Sub(Add(impact,Mul(p.normal,radius)),to));projection=impact;}
      const float disk2=radius*radius-planeDistance*planeDistance;
      V edgeCorrection{};float edgeDistance=10000000.f;bool planeHit=false,faceHit=false;
      for(const Group& g:p.groups){if(!(g.mask&mask))break;
        for(const Triangle& raw:g.triangles){
          const Triangle t{Transform(n,raw.a),Transform(n,raw.b),Transform(n,raw.c),raw.area,raw.off};
          if(Inside(projection,t)){
            const V correction=Sub(Add(projection,Mul(p.normal,radius)),to);
            const bool selected=selectedScore<penetration;
            if(selected){result.contact=MakeContact(n,p,g,raw);selectedScore=penetration;
              totalCorrection=selectedFace?Merge(totalCorrection,correction):correction;selectedFace=true;
            }else totalCorrection=Merge(totalCorrection,correction);
            result.hit=true;planeHit=true;faceHit=true;
            if(trace)result.trace.push_back({MakeContact(n,p,g,raw),correction,true,selected});
            break; // -1: primer interior detiene TODOS los triangulos de este plano.
          }
          const V vertices[3]={t.a,t.b,t.c};
          // Tabla47DA10: B,C,A; extremo inicial A; edges A->B,B->C,C->A.
          for(unsigned e=0;e<3;++e){
            const V a=vertices[e],b=vertices[(e+1)%3];const V edge=Sub(b,a);const float length=Len(edge);const V dir=Unit(edge);
            const V ap=Sub(a,projection);const float cross2=Dot(Cross(ap,dir),Cross(ap,dir));
            if(disk2-cross2<0)continue;
            const float ext=std::sqrt(disk2-cross2),along=-Dot(ap,dir);
            if(along< -ext||along>length+ext)continue;
            const float clamped=along<0?0:along<=length?along:length;
            const V closest=Add(a,Mul(dir,clamped));V outward=Sub(impact,closest);const float dist=Len(outward);outward=Unit(outward);
            if(!(dist<edgeDistance))continue;edgeDistance=dist;
            edgeCorrection=Sub(Add(impact,Mul(outward,radius-dist)),to);planeHit=true;
            const float score=Len(edgeCorrection);const bool selected=selectedScore<score;
            Contact c=MakeContact(n,p,g,raw);c.normal=outward;c.d=-Dot(outward,closest);
            if(selected){result.contact=c;selectedScore=score;selectedFace=false;}
            if(trace)result.trace.push_back({c,edgeCorrection,false,selected});
          }
        }
        if(faceHit)break;
      }
      if(planeHit){result.hit=true;if(!faceHit)totalCorrection=Merge(totalCorrection,edgeCorrection);}
    }
  }
  result.point=Add(to,totalCorrection);result.score=selectedScore;return result;
}
} // namespace dbz3::iwbubbles::wcf
