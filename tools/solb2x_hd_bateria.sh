# eB2h: lanza HD (grupos en $GRUPOS) con todos los interruptores y resume contra PAL. uso: GRUPOS="2 4 8 7" bash tools/solb2x_hd_bateria.sh --ik-escala 1.24
# uso: todo.sh [extra flags]  -> corre g2,g4,g8,g7 y resume
cd "C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection"; export DBZ3_AGENTE=${DBZ3_AGENTE:-eB2h}
declare -A PALD=( [2]=original_g2_e0_20261105_204406 [4]=original_g4_e0_20261105_200854 [8]=original_g8_e0_20261105_205413 [7]=original_g7_e0_20261105_200120 [0]=original_g0_e1_20261106_014450 )
for g in ${GRUPOS:-2 4 8 7}; do
  extra=""; [ $g -ge 7 ] && extra="--spx 0 --reaccion-pal --anim-b2"; [ $g = 4 -o $g = 0 ] && extra="--reaccion-pal --anim-b2"; [ $g = 2 ] && extra="--anim-b2"
  bash tools/solb2x_hd_todas.sh $g $extra "$@" > ${TMPDIR:-/tmp}/eB2h_run_$g.txt
  HD=$(ls -dt D:/DBZ3HD/_pruebas/*grupo$g | head -1)
  echo "== g$g $HD"
  python tools/solb2x_escena_resumen.py D:/tmp/solB2X/${PALD[$g]}/traza.json "$HD" 1.0 i | head -1
  python tools/solb2x_atacante_mala.py D:/tmp/solB2X/${PALD[$g]}/traza.json "$HD" 1.0 | tail -3
done
