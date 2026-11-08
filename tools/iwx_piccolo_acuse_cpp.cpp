// Regresion del limite cola/motor: no fabricar un cuadro0 ni un avance+1.
#include "../src/iw_piccolo_reaccion.h"
#include <cstdio>
#include <limits>

namespace I = dbz3::iwpiccolo;
int main() {
  int comprobaciones = 0, fallos = 0;
  const auto comprobar = [&](bool bien, const char* nombre) {
    ++comprobaciones;
    if (!bien) { ++fallos; std::printf("FAIL %s\n",nombre); }
  };
  // Control: la API anterior conserva el contraejemplo conocido de cola lenta.
  I::ReaccionEnMotor anterior;
  anterior.Golpe(4,100,100);
  anterior.Paso({0xFCC,30,0,0},100,100,0);
  anterior.Paso({0xFCC,30,0,10},100,100,0);
  anterior.Golpe(4,100,100);
  anterior.Paso({0xFCC,30,0,10},100,100,0);
  comprobar(anterior.Paso({0xFCC,30,0,11},100,100,0).observado,"control anterior acepta muestra vieja");
  comprobar(anterior.Paso({0xFCC,30,0,29},100,100,0).regreso,"control anterior acaba sin segundo reinicio");

  I::ReaccionEnMotor r;
  const auto primero = r.Golpe(4,100,100,true);
  comprobar(!r.VincularReinicio(primero.estado.generacion+1,101),"generacion ajena");
  comprobar(!r.VincularReinicio(primero.estado.generacion,0),"identidad nula");
  comprobar(r.VincularReinicio(primero.estado.generacion,101),"primera orden");
  comprobar(!r.VincularReinicio(primero.estado.generacion,102),"no reasignar orden");
  comprobar(!r.Paso({0xFCC,30,0,10,0},100,100,0).observado,"primero pendiente");
  comprobar(r.Paso({0xFCC,30,0,2,101},100,100,0).observado,"primero aplicado sin ver cero");
  const auto segundo = r.Golpe(4,100,100,true);
  comprobar(segundo.estado.pose_anterior==3,"golpe durante reaccion");
  comprobar(r.VincularReinicio(segundo.estado.generacion,102),"segundo igual");
  // Veinte despachos del clip viejo, incluido terminal/vuelta, NO son un acuse.
  for (int i=10;i<30;++i) {
    const auto p = r.Paso({0xFCC,30,0,float(i),101},100,100,0);
    comprobar(p.activa && !p.observado && !p.regreso,"rechaza cuerpo viejo mismo alias");
  }
  comprobar(!r.Paso({0xFCC,30,1,0,101},100,100,0).observado,"vuelta vieja");
  comprobar(!r.Paso({0xFCC,30,0,2,103},100,100,0).observado,"orden futura ajena");
  comprobar(!r.Paso({0xFCB,30,0,2,102},100,100,0).observado,"otro codigo aun con orden");
  comprobar(!r.Paso({0xFCC,30,0,std::numeric_limits<float>::quiet_NaN(),102},100,100,0).observado,"reloj no finito");
  comprobar(r.Paso({0xFCC,30,0,2,102},100,100,0).observado,"nuevo aplicado cuadro2");
  const auto repetido = r.Paso({0xFCC,30,0,2,102},100,100,0);
  comprobar(repetido.observado && !repetido.regreso,"cuadro repetido conservado");
  comprobar(r.Paso({0xFCC,30,0,29,102},100,100,0).regreso,"fin solo de orden nueva");
  // Si el controlador perdio todos los cuadros intermedios, el acuse del motor
  // sigue acreditando el inicio. No se bloquea esperando ver cuadro0.
  const auto tercero = r.Golpe(3,100,100,true);
  comprobar(r.VincularReinicio(tercero.estado.generacion,103),"tercero distinto");
  comprobar(r.Paso({0xFC8,30,0,29,103},100,100,0).regreso,"primera observacion ya terminal confirmada");
  const auto cuarto = r.Golpe(9,100,100,true);
  comprobar(!r.VincularReinicio(tercero.estado.generacion,103),"acuse de generacion terminada");
  comprobar(!r.Paso({0xFCB,30,0,29,103},100,100,0).observado,"sin vinculo no termina");
  comprobar(r.VincularReinicio(cuarto.estado.generacion,104),"cuarto");
  r.Reiniciar();
  comprobar(!r.Paso({0xFCB,30,0,29,104},100,100,0).activa,"cancelacion al salir");
  comprobar(!r.VincularReinicio(cuarto.estado.generacion,104),"no revivir cancelacion");
  std::printf("{\"comprobaciones\":%d,\"fallos\":%d,\"cola_sintetica\":true,\"motor_vivo\":false,\"normal\":false}\n",comprobaciones,fallos);
  return fallos ? 1 : 0;
}
