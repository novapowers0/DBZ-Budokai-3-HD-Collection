# SLOTS_ESC-01 — copias de alta para las pruebas de muchos escenarios

Estas pruebas NO van con las copias smoke_a/b/c: necesitan la copia fija `D:\DBZ3HD\esc_n`, preparada con
el numero de altas que pide cada fichero. Se preparan (y se cambian) con un solo comando, sin borrar nada
(lo anterior se aparta a `E:\DBZ3HD_trabajo\escenarios_n\_retirados`):

```
python tools\escenarios_copia_n.py preparar --n 12
python tools\pruebas.py correr esc_n "docs\14_frentes\escenarios_pruebas\final\slots_esc\selector_desplazamiento_12.toml"
```

| Fichero | N | Que prueba |
|---|---|---|
| `selector_desplazamiento_12.toml` | 12 | cuenta 12 altas, abre el selector, llega a la ultima (registro 28) y lucha en ella |
| `selector_desplazamiento_40.toml` | 40 | lo mismo con 40 (registro 56) |
| `selector_desplazamiento_110.toml` | 110 | cuenta 110 altas (la lista de nombres acaba en la 126) |
| `selector_desplazamiento_200.toml` | 200 | aviso actual del tope: se cargan las 110 primeras |

Nota (11/11): con 110 altas el juego se cae al abrir el selector (access violation escribiendo en 0x8070,
`lr=0x82228F40`, un memcpy del cargador con destino nulo) con 112 de 128 lecturas usadas. No es de este
paquete: lo arregla SLOTS_ESC-03 (quitar el tope) y lo mide SLOTS_ESC-02. Con 12 y 40 el selector abre y
se lucha; con 200 el aviso sale antes de intentarlo.
