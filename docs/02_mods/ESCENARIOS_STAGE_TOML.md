# Escenarios añadidos: campos de `stage.toml`

Cada mod de escenario lleva `mods/<mod>/stage.toml` con uno o más bloques `[escenario]` (o `[[escenario]]`). Lo lee
`CargarDeMods` (`src/escenarios.cpp`). Las líneas que empiezan por `#` son comentarios. Un campo desconocido se ignora.
Normalmente el archivo lo escribe el conversor (`tools/escenarios_convertir.py`); esta tabla sirve para revisarlo o retocarlo a mano.

## Obligatorios
| Campo | Qué es |
|---|---|
| `entrada` | Entrada del escenario en el AFS de datos (≥ 5000). Con menos, el alta se ignora y el registro dice `alta ignorada`. |
| `tema` | Registro nativo (0..16) que presta luz, cámara y reglas. Fuera de 0..16, el alta se ignora. |

## Selector y textos
| Campo | Qué es |
|---|---|
| `nombre` | Nombre corto (máx. 15 letras) del selector. |
| `titulo` | Nombre largo para pantallas propias (si falta, `nombre`). |
| `menu` | Entrada del #AZT del selector (0 círculo, 1 vista, 2 rótulo, 3 bola). Por defecto `entrada + 100`. |
| `variante` | Registro que se elige al mantener Circle en el selector (-1 o ausente = el mismo). |
| `selector` | `false`/`no`/`0` = no sale en el selector (cuenta como oculta). |
| `musica` | Entrada de `adx_usa.afs` para la vista previa del selector (solo US). |
| `id_se` | Sonido ambiente (id_SE del registro). |

## Zonas y transiciones
| Campo | Qué es |
|---|---|
| `zona_escena` | Escena del cambio de zona. |
| `zona_b` | Entrada de la zona B (la otra variante del escenario). |
| `transicion_b2` | Solo vale `transicion_b2.b2t` (archivo fijo dentro del mod). |

## Montaje y efectos (los escribe el conversor)
| Campo | Formato |
|---|---|
| `setup` | Función de montaje del original (informativa). |
| `variante_escena` | 0/1: decide clima, polvo, destellos y objeto global. |
| `clima` / `polvo` / `particulas` | `tipo,n,tam,vel,area,alto,RRGGBB,viento[,afs,hijo_amt,tex]` (n de 1 a 4000). |
| `destello` | `x,y,z,tam,RRGGBB[,afs,hijo_amt,tex]`. |
| `mosaico` | `paso,afs,amb_hijo,amo,amt,variante` (`amb_hijo` = `-` o `0xFFFFFFFF` si son entradas sueltas). |
| `niebla` | `RRGGBB,ini,fin[,fogA,fogB]`. |
| `nubes` | `firma_hex,n,du,dv,tam[,px,py,capa,sigue]`. |
| `arrastre` | `false` apaga el arrastre de capas. |
| `capas` / `capa` | `flags,ex,ey` / `L,u,v,px,py` (L de 0 a 7). |
| `dinamico` | `modelo,rejilla,x,y,z,nombre`. |
| `material` / `matclave` | `firma_hex,cuadros` / `firma_hex,cuadro,RRGGBBAA`. |
| `uvanim`, `uvobjeto`, `uvcanal` | Animación UV (#MTA). |

## Ocultos y repetidos
Un alta que es el mismo lugar que uno nativo (`res/datos/escenarios_repetidos.toml`) no sale en el selector salvo con el ajuste
`dbz3_escenarios_repetidos`. Las ocultas no cuentan para el "?", la rueda ni el HUB.
