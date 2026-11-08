# eB2h: una prueba HD de grupo $1 con todos los interruptores de B2 (resto de argumentos pasan a solb2x_lanzamiento.py)
cd "C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection"; export DBZ3_AGENTE=${DBZ3_AGENTE:-eB2h}
timeout 1750 python tools/solb2x_lanzamiento.py --grupo $1 --curva --actores-1450 --actores-completo --huesos --ik-suelo --idle-b2 --mirada --levantar-b2 --ataque-b2 "${@:2}" --finalizar > ${TMPDIR:-/tmp}/eB2h_hd_g$1.log 2>&1
grep evidencia ${TMPDIR:-/tmp}/eB2h_hd_g$1.log
