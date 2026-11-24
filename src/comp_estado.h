// R16 COMP: lectura del estado publicado por combate.cpp; lados 0 y 1.
#pragma once

namespace dbz3::comp {
// Solo true cuando las reglas competitivas estan activas y el luchador esta en Aura Burst.
// Lectura desde cualquier hilo (copia atomica; nunca escribe el estado Hiper de B3).
bool EnBurst(int lado);
// Ki actual en bloques de 1000, limitado a [0, 6]. Lado invalido o sin combate = 0.
float KiBloques(int lado);
}  // namespace dbz3::comp
