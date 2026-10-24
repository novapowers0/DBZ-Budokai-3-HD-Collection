// dbz3 - ESCENARIOS NUEVOS (altas): escenarios de mods como casillas NUEVAS del juego (solo US).
//
// Contrato de zona B (opcional, frente 07): un alta puede declarar `zona_b = <entrada de OTRO alta>` en su
// stage.toml. Esa otra alta es la zona A; al caer por el borde con `dbz3_escenarios_zonas_b=true`,
// ComprobarZonaB recarga el escenario del combate con la zona B (PasarZonaB) y consume el aviso de ring out.
// Si `zona_b` no corresponde a ningun alta cargada, CargarDeMods lo ignora y avisa: nunca se recarga un
// registro inexistente. El cambio nativo del motor (sub_820CC6C0) sigue bloqueado mientras haya un alta
// cargada; no se quita esa guarda sin una medicion en el juego.
//
// Un mod declara en mods/<mod>/stage.toml uno o mas escenarios nuevos:
//   [[escenario]]
//   nombre  = "B2 ISLAS"   # nombre del registro (<= 15 letras)
//   titulo  = "Islas de B2" # nombre para pantallas propias (UTF-8; si falta, `nombre`)
//   entrada = 5002         # data_cmn.afs: bin del escenario (entrada NUEVA, >= 5000)
//   id_se   = 45           # data_cmn.afs: sonido/ambiente (el de un escenario de serie)
//   tema    = 16           # luz y cielo: la funcion de tema del registro N de serie
// Cada alta es el registro 17, 18... El juego solo tiene 17 registros (tabla 0x82323760 pegada a
// la de temas): el alta se carga en el hueco de un registro ANFITRION (16, R&R BASE) solo mientras
// ese escenario esta cargado, y se devuelve el original en cuanto se carga otro. El indice de
// escenario del combate (CFG+340) lleva el numero del alta (17+), asi "otra vez" vuelve a cargarla
// y dbz3::hub::RequestBattle(id1, id2, 17, ...) la lanza igual que una de serie.
#pragma once

#include <string>

namespace rex::memory {
class Memory;
}

namespace dbz3::escenarios {

constexpr int kSerie = 17;   // registros de serie (0..16)

// Lee las altas de los mods activos (lo llama stages::ApplyAtLaunch).
void CargarDeMods(rex::memory::Memory* memory);
// Registros disponibles: 17 de serie + altas de mods.
int Total();
int Visibles();   // altas con casilla en el selector (las primeras; los campos de IW quedan fuera)
// true si hay un alta de mod puesta en el registro anfitrion, o sea, el escenario cargado es un alta
// (frente 05: lo usa el bloqueo del cambio de zona del motor en escenarios.cpp).
bool AltaActiva();
bool AltaActivaSinGuardaCamara();   // Ring de Cell B1: el gancho de camara sin clip no salta su actualizacion
// Registro del alta del campo de Infinite World `campo` (DATA_CMN de IW: 881, 883, 884, 897, 900, 903, 906, 909...)
// que crea mod center hd/campos_iw.py en la copia del jugador (carpeta "Campo IW <campo>"), o -1 si no esta
// instalado (frente R8).
int CampoIW(int campo);
// Registro del alta cuya carpeta de mod es `mod` (p. ej. los escenarios de B2 de campos_iw.py, B12), o -1.
int PorMod(const std::string& mod);
// Nombre para pantallas propias (HUB, retos), en el idioma del juego (id de idioma de Xbox, como
// dbz3_language: 1 ingles, 2 japones, 3 aleman, 4 frances, 5 espanol, 6 italiano; otro = ingles), en UTF-8:
// los de serie son los oficiales del selector (rotulos de data_<idioma>.afs 2043); las altas dan su `titulo`
// (o `nombre`). El juego no trae texto japones (con el idioma 2 sale en ingles, como data_usi = data_eng) y
// la letra de las pantallas propias es latina: el 2 da el ingles. Fuera de rango: "".
std::string Nombre(int registro, int idioma);
// Lo mismo en el idioma elegido por el jugador (dbz3::settings::Language()).
std::string Nombre(int registro);
// NB006 (IW): apaga SOLO la geometria/cielo del escenario (bit 0 de G+0x4C), sin tocar a los luchadores.
void OcultarFondo(bool ocultar);

}  // namespace dbz3::escenarios
