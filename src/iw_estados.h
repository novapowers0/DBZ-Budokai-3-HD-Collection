// Estados de la maquina de tierra IW, no numeros de clip ni fases del port.
#pragma once
#include <cstdint>
#include <cstring>

namespace dbz3::iwestado {
inline int DeAnim(const char* nombre,uint32_t codigo) {
  if(!nombre)return -1;
  // Una misma reaccion del port recorre varios estados originales. Publicar
  // el codigo real en cada cambio evita conservar el ancla del dash anterior.
  if(!std::strcmp(nombre,"golpe")) {
    switch(codigo) {
      case 0x840:case 0x841:return 0x20; // EU29E260, suelo/aire
      case 0x843:return 0x27; // EU29CC90
      case 0x844:return 0x28; // EU29CA70
      case 0x845:return 0x29; // EU29C840
      default:return -1;
    }
  }
  if(!std::strcmp(nombre,"muerte")) {
    if(codigo==0x846)return 0x2A; // EU29C6A0
    if(codigo==0x847)return 0x2B; // EU29C530
    return -1;
  }
  struct Par { const char* n;int e; };
  static const Par pares[]={
    {"reposo",0x00},{"reposo-1",0x01},{"andar",0x02},{"correr",0x03},{"freno",0x04},
    {"entrada",0x0A},{"entrada-2",0x0A},{"salto-prep",0x0C},{"salto-sube",0x0D},
    {"salto-aire",0x0E},{"salto-cae",0x0F},{"aterrizar",0x10},{"salto-doble",0x13},
    {"dash-ini",0x14},{"dash",0x15},{"dash-fin",0x16},{"dash-aereo-ini",0x14},
    {"dash-aereo",0x15},{"dash-aereo-fin",0x16},{"pared-kick",0x1F},
    {"pared-rebote",0x20},{"pared-rebote-aire",0x20},{"saliente",0x0E},
    {"cansado",0x07},{"recuperacion",0x08},{"final-bien",0x2F},{"final-mal",0x30},
    // EU2A3B10/2A3940: 2 y 3 son el MISMO estado5; 4 entra en6
    // (EU2A37D0), que no vuelve a escribir work+1C8.
    {"carga",0x05},{"carga-bucle",0x05},{"carga-fin",0x06},
    {"salto-vuelo",0x11},{"despegue",0x12}, // EU2A19C0/2A1760
    {"choque",0x19}, // EU29FC60: 3D suelo/3E aire
    {"resbalon",0x1E}, // EU29E9A0: CA
  };
  for(const auto& p:pares)if(!std::strcmp(nombre,p.n))return p.e;
  // Sobrevuelo/entradas de guion no acreditados aqui: no inventar su estado.
  return -1;
}
} // namespace dbz3::iwestado
