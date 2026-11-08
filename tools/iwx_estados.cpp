// Replay de acciones contra estados/ancla observados en el original.
#include "../src/iw_estados.h"
#include "../src/iw_camaras_gp2.h"
#include <iostream>
#include <string>

int main() {
  std::string nombre;
  uint32_t codigo;
  int esperado,anterior,ancla;
  unsigned casos=0,fallos=0;
  while(std::cin>>nombre>>codigo>>esperado>>anterior>>ancla) {
    const char* n=nombre=="<null>"?nullptr:nombre.c_str();
    const int estado=dbz3::iwestado::DeAnim(n,codigo);
    const int modo=dbz3::gp2cam::ModoAnclaOriginal(estado,anterior!=0);
    ++casos;
    if(estado!=esperado || modo!=ancla) {
      ++fallos;
      std::cerr<<nombre<<" codigo="<<codigo<<" estado="<<estado<<"/"<<esperado
          <<" ancla="<<modo<<"/"<<ancla<<'\n';
    }
  }
  const bool completo=std::cin.eof();
  std::cout<<"{\"casos\":"<<casos<<",\"fallos\":"<<fallos
      <<",\"completo\":"<<(completo?"true":"false")<<"}\n";
  return !completo || casos==0 || fallos!=0;
}
