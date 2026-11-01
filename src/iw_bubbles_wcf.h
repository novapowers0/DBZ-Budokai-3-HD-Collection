// IW EU Bubbles, WCF del campo897 (DATA_CMN898/0 de la copia del jugador).
// CANDIDATO OFFLINE. No afirmar exactitud integral sin oraculo de barrido.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace dbz3::iwbubbles::wcf {
inline constexpr bool kExactitudIntegralValidada=false;
struct V {float x=0,y=0,z=0;};
inline V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V Mul(V a,float k){return {a.x*k,a.y*k,a.z*k};}
inline float Dot(V a,V b){return (a.z*b.z+a.x*b.x)+a.y*b.y;}
inline V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Len(V a){return std::sqrt(Dot(a,a));}
inline V Unit(V a){float l=Len(a);return l>0?Mul(a,1/l):V{};}
struct Contact {
  V normal;float d=0;
  std::uint16_t material=0,type=0;
  std::uint32_t node=0,plane=0,group=0,triangle=0;
};
struct Event {Contact contact;V correction;bool face=false,selected=false;};
struct Result {
  bool hit=false;V point;Contact contact;
  float score=-1,distance=0;
  std::vector<Event> trace; // solo cuando se solicita; orden del kernel, no ordenar por tipo.
};
struct Bounds {float minX=-900,minZ=-900,maxX=900,maxZ=900;unsigned flags=3;};
class Backend {
 public:
  // Buffer RAW #WCF, offsets relativos. No usar !WCF reubicado ni malla AMO.
  bool Load(const std::uint8_t* bytes,std::size_t size,Bounds bounds={});
  const std::string& Error()const{return error_;}
  std::size_t Nodes()const{return nodes_.size();}
  std::size_t Triangles()const{return triangles_;}
  Result Sweep(V from,V to,float radius=.5f,unsigned mask=2,bool trace=false);
  Result Ray(V from,V to,unsigned mask=7,bool trace=false);
 private:
  struct Triangle {V a,b,c;float area=0;std::uint32_t off=0;};
  struct Group {std::uint16_t mask=0,type=0;std::uint32_t off=0;std::vector<Triangle> triangles;};
  struct Plane {V normal;float d=0;unsigned mask=0;std::uint32_t off=0;std::vector<Group> groups;};
  struct Node {
    std::uint32_t off=0,child=0;unsigned count=0,tile=0;std::uint16_t flags=0;
    V sphere,summary;float radius=0,summaryRadius=0;
    std::array<float,16> matrix{};
    std::vector<Plane> planes;
    int cacheBits=-1;V shift;
  };
  std::vector<Node> nodes_;std::size_t triangles_=0;Bounds bounds_;std::string error_;
  V Transform(const Node& n,V p)const;
  void Copies(V query);
  void SphereCandidates(unsigned n,V p,float r,std::vector<unsigned>& out)const;
  struct Candidate {int key=0;unsigned index=0;};
  void RayCandidates(unsigned n,V from,V dir,float len,std::vector<Candidate>& out)const;
  void PointCandidates(unsigned n,V p,std::vector<Candidate>& out)const;
  void RootsSphere(V p,float r,std::vector<unsigned>& out)const;
  void RootsRay(V a,V b,std::vector<Candidate>& out)const;
  Contact MakeContact(const Node& n,const Plane& p,const Group& g,const Triangle& t)const;
  static bool Inside(V p,const Triangle& t);
  static V Merge(V old,V candidate);
};
} // namespace dbz3::iwbubbles::wcf
#include "iw_bubbles_wcf.inl"
