// R16: catalogo original compatible con los cuatro campos persistentes de CaC.
#ifndef DBZ3_CAC_PARTES_ORIGINALES_UI_H_
#define DBZ3_CAC_PARTES_ORIGINALES_UI_H_

#include "cac.h"
#include <string>
#include <vector>

namespace dbz3::cac::originales {

inline bool Cuerpo(int cuerpo) {
  return cuerpo == 27 || cuerpo == 38 || cuerpo == 11 ||
         cuerpo == 34 || cuerpo == 35 || cuerpo == 36;
}

inline bool Compatible(const Pieza& p, int cuerpo, int traje = 0) {
  if (p.id >= 3000 && p.id < 3080) return cuerpo == 27 || cuerpo == 38;
  if (p.id >= 3080 && p.id < 3100) {
    const std::string prefijo = "local:" + std::to_string(cuerpo);
    if (p.archivo == prefijo) return traje == 0;  // Registro anterior: solo traje base.
    if (p.archivo.rfind(prefijo + ":", 0) != 0) return false;
    const std::string trajes = "," + p.archivo.substr(prefijo.size() + 1) + ",";
    return trajes.find("," + std::to_string(traje) + ",") != std::string::npos;
  }
  if (p.id >= 3100 && p.id < 3200) return cuerpo == 11;
  if (p.id >= 3200 && p.id < 3300) return cuerpo == 34 || cuerpo == 35 || cuerpo == 36;
  return false;
}

inline std::vector<Pieza> Opciones(int cuerpo, int zona, int traje = 0) {
  constexpr const char* tipos[] = {"original_torso", "original_piernas", "original_guantes", "original_botas"};
  std::vector<Pieza> out;
  if (zona < 0 || zona >= 4 || !Cuerpo(cuerpo)) return out;
  for (const Pieza& p : Catalogo())
    if (p.tipo == tipos[zona] && Compatible(p, cuerpo, traje)) out.push_back(p);
  return out;
}

inline const char* Rotulo(int cuerpo, int zona, bool ingles) {
  constexpr const char* fre_es[] = {"Cabeza", "Mandíbula", "Placas", "Cola"};
  constexpr const char* fre_en[] = {"Head", "Jaw", "Plates", "Tail"};
  constexpr const char* nam_es[] = {"Cabeza", "Orejas", "Capa", "Turbante"};
  constexpr const char* nam_en[] = {"Head", "Ears", "Cape", "Turban"};
  constexpr const char* maj_es[] = {"Antena", "Rostro", "Cuerpo", "Accesorio"};
  constexpr const char* maj_en[] = {"Tendril", "Face", "Body", "Accessory"};
  if (zona < 0 || zona >= 4) return "";
  if (cuerpo == 27 || cuerpo == 38) return ingles ? fre_en[zona] : fre_es[zona];
  if (cuerpo == 11) return ingles ? nam_en[zona] : nam_es[zona];
  return ingles ? maj_en[zona] : maj_es[zona];
}

}  // namespace dbz3::cac::originales
#endif
