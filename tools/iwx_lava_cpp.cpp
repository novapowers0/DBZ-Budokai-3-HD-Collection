// El oraculo suministra casos desde las dos funciones del ELF, no de este codigo.
#include "../src/iw_lava.h"
#include <iostream>
int main() {
  int estado,pies,cuerpo,aire;float y,suelo;
  while(std::cin>>estado>>pies>>cuerpo>>y>>suelo>>aire)
    std::cout<<dbz3::iwlava::Superficie3(estado,pies,y,suelo,aire)<<' '
             <<dbz3::iwlava::Superficie4(estado,pies,cuerpo,y,suelo)<<'\n';
}
