// dbz3 - Boca con la voz (R23 BOCA-02..04). RE en studies 5/ENTREGAS/R23/BOCAS.md.
// B3 HD ya trae el motor de boca de Dimps (el mismo de IW y B2): hablante = modelo LIPS de la forma (A+0x540), estado en
// A+0x640/0x644/0x648, y cada cuadro el juego elige la pose (sub_820F97C8) y la copia al cuerpo (sub_82136FD0). Aqui solo
// se ARRANCA con una pista #AMK ajena (IW, B2): las poses son siempre las LIPS del personaje de B3, nada inventado.
#pragma once
#include <cstdint>
#include <vector>

namespace dbz3::boca {

// Interruptor dbz3_boca (apagado hasta juzgar BOCA-02 en captura).
bool Activo();

// Punto de entrada comun: el luchador de B3 `x` (X; cuerpo A = X+0x650) habla el clip `clip` de la pista #AMK `amk`
// (tal cual del disco PS2, little-endian). Hace lo que sub_820FA750 con el banco puesto en la ranura 15 del hablante.
// false si esta apagado, no hay hablante (sin LIPS), el cuerpo esta congelado o el clip no existe.
bool Hablar(uint8_t* base, uint32_t x, const std::vector<uint8_t>& amk, int clip, bool ignorar_pausa = true, bool intro7db = false);

// Para el registro de las pruebas: visema/variante que el juego tiene puestos ahora en el hablante de `x`
// (-1 si no habla). Lee la clave que elige sub_820F97C8 (hablante+0x20+0x4D/0x4F).
int VisemaActual(uint8_t* base, uint32_t x, int* variante = nullptr);

}  // namespace dbz3::boca
