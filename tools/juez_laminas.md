# Haiku — juez visual de una lámina

Lámina: `{id}`. La **{tipo}** está a la izquierda; la **captura del port**, a la derecha.
Foco del cotejo: {foco}

Compara lo que se ve en esa zona: elementos presentes, posición y tamaño, color, legibilidad y estado de la escena. No penalices por sí solos el cambio de resolución, idioma o encuadre; si el momento, el estado o la calidad de la captura impiden cotejar el foco, dilo. No deduzcas causas internas ni juzgues animación o audio a partir de un fotograma.

- **SE PARECE**: el foco conserva los elementos y la presentación relevantes.
- **CASI**: coincide la escena o pantalla, pero hay una diferencia visual relevante y localizada.
- **NO**: falta algo esencial, hay un desajuste decisivo o el foco no se puede cotejar. En este último caso, describe qué captura habría que repetir, sin atribuir un defecto al port.

Responde en exactamente dos líneas, sin Markdown ni lista. Elige un solo veredicto y da un solo arreglo prioritario, identificando la zona y el cambio visible que buscas. Si no hay un cambio relevante, escribe `Ninguno`.

```text
Veredicto: SE PARECE / CASI / NO
Arreglo: <un cambio concreto, una recaptura concreta o Ninguno>
```
