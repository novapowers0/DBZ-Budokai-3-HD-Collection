// B3 HD US: 82333208 (tecnicas) y 82322950 (auras), data_cmn.afs.
// Generado por fabrica/P4/datos.py a partir de las tablas nativas.
#pragma once
namespace dbz3::survivors::fxreal {
inline constexpr int kBspB3[64]={
  533,531,530,529,528,535,536,553,
  549,548,539,543,550,555,547,552,
  541,551,544,542,532,545,-1,-1,
  -1,-1,-1,524,505,506,507,-1,
  509,518,515,516,517,523,521,510,
  514,508,546,519,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,
};
inline constexpr int kAuraB3[64]={
  23,21,20,19,18,24,25,41,
  37,36,27,31,38,43,35,40,
  29,39,32,30,22,33,-1,-1,
  -1,-1,-1,15,0,1,2,-1,
  4,10,7,8,9,14,13,5,
  6,3,34,11,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,
};
// CombatAtlas publica por ordinal; el invitado usa #AZT.descriptor+0.
inline int OrdinalAtlas(int bsp,int id) {
  if(id<0)return -1;
  switch(bsp) {
    case 505: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,-1,-1,10,11,-1,-1,-1,-1,-1,-1,-1,-1,20,21,22,-1,24,25,-1,-1,-1,29,30};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    case 523: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,-1,2,3,4,-1,6,7,8,9,10,11,12,-1,-1,-1,-1,-1,-1,-1,20,21,22,-1,24,-1,-1,-1,28,-1,30,31,32,33,-1,-1,-1,-1,-1,-1,40,41,42,43,44};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    case 532: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,-1,2,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,19,20,21,22,23,24,25,26,27,28,-1,30,-1,-1,33,-1,35,36,37};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    case 545: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    case 547: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,20,21,-1,23,24,25};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    case 555: {
      static constexpr int ord[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,20,21,-1,23,24,25,26,27,28,29,30,31,32,33,34};
      return id<int(sizeof ord/sizeof *ord) ? ord[id] : -1;
    }
    default: return id;
  }
}
}  // namespace dbz3::survivors::fxreal
