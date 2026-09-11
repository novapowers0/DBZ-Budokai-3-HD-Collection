#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""strip2.py - Stripificador del port PRESERVANDO orden y winding."""
import sys
sys.path.insert(0, sys.argv[2])
from awg_vertex_buffer import AwgVertexBuffer


def runs_in_order(tris):
    """Reconstruye runs recorriendo tris EN ORDEN (los chunks PS2 vienen en
    orden). Extiende por la arista final o inicial compartida."""
    runs = []
    cur = None
    for t in tris:
        if cur is None:
            cur = [t[0], t[1], t[2]]
            continue
        s = set(t)
        if len(cur) >= 2 and cur[-2] in s and cur[-1] in s:
            o = [x for x in t if x not in (cur[-2], cur[-1])]
            if o:
                cur.append(o[0]); continue
        if len(cur) >= 2 and cur[0] in s and cur[1] in s:
            o = [x for x in t if x not in (cur[0], cur[1])]
            if o:
                cur.insert(0, o[0]); continue
        runs.append(cur)
        cur = [t[0], t[1], t[2]]
    if cur:
        runs.append(cur)
    return runs


def strip_tris(seq):
    out = []
    for i in range(len(seq) - 2):
        a, b, c = seq[i], seq[i + 1], seq[i + 2]
        out.append((a, b, c) if i % 2 == 0 else (c, b, a))
    return out


def same_orientation(t1, t2):
    """Misma cara con el MISMO winding (rotaciones ok, reflejo no)."""
    return (t1 in ((t2[0],t2[1],t2[2]), (t2[1],t2[2],t2[0]), (t2[2],t2[0],t2[1])))


def join_runs(runs):
    out = []
    for r in runs:
        if not r:
            continue
        if not out:
            out.extend(r); continue
        # conectores degenerados hasta que el proximo triangulo (indice len(out)-2)
        # tenga paridad 0 (para que el run interno empiece con paridad 0)
        # probamos 2 y 3 duplicados
        for extra in (2, 3):
            trial = out + [out[-1]] + [r[0]] * (extra - 1) + r
            # el primer triangulo del run r queda en el indice len(out)+extra-2
            start = len(out) + extra - 2
            if start % 2 == 0:
                out = trial
                break
        else:
            out = out + [out[-1], r[0]] + r
    return out


def main():
    a = AwgVertexBuffer.load(sys.argv[1])
    ib = a.indices()
    tris = [(ib[i], ib[i+1], ib[i+2]) for i in range(0, len(ib) - 2, 3)]
    runs = runs_in_order(tris)
    seq = join_runs(runs)
    rec = strip_tris(seq)
    rec = [t for t in rec if len(set(t)) == 3]
    # valida winding fiel
    good = 0
    for orig, r in zip(tris, [t for t in rec]):
        pass
    # comparacion por conjunto de caras orientadas
    from collections import Counter
    corig = Counter(tuple(t) for t in tris)
    # normaliza orientacion de rec a la del original por rotacion
    def canon(t):
        return min((t[0],t[1],t[2]),(t[1],t[2],t[0]),(t[2],t[0],t[1]))
    orig_set = set(canon(t) for t in tris)
    bad = 0
    for t in rec:
        if canon(t) not in orig_set:
            bad += 1
    print("tris=%d runs=%d seq=%d rec=%d  orient_mal=%d" % (len(tris), len(runs), len(seq), len(rec), bad))
    open(sys.argv[3], "w").write(" ".join(map(str, seq)))


if __name__ == "__main__":
    main()
