"""Modelos Python de fragmentos C++ leidos; NO ejecutan el port compilado.

Revision vinculada por hashes de fragmento en fuentes.py y en cada evidencia. El azar
de std::rand/reloj es una entrada, no una supuesta reproduccion de libc host.
"""

ALLY_ORDER = (28, 26, 27, 6, 4, 7, 8, 3, 5, 35, 24, 21, 33, 2)
FIXED = ((503, ()), (505, (503,)), (506, (505,)), (507, (506,)),
         (508, (507,)), (509, (508,)))
FUSIONS = ((237, (323, 324, 220)), (266, (318, 253)), (279, (314, 253)),
           (290, (326, 253)), (301, (328, 253)))


def phase(order, following, current=3):
    return dict(fase_byte=max(0, min(following, 8)) if order == 41 else 255 if order == 42 else current,
                modo={41: 9, 42: 11, 43: 10}[order], guardado_solicitado=order != 43)


def rivals(neighbors, random_word):
    v = neighbors[random_word & 3]
    return v if v > 0 else -1


def rival_order(rows):
    return [row['indice'] for row in rows
            if row['bando'] == 6 and row['personaje'] != 22 and row['casilla_valida']
            for _ in range(row['acciones_turno'])]


def allies(rows):
    eligible = [row for row in rows if row['bando'] == 0 and row['personaje'] != 22
                and 0 <= row['personaje'] < 53 and row['casilla_valida']]
    rank = {char: i for i, char in enumerate(ALLY_ORDER)}
    return [row['indice'] for row in sorted(eligible, key=lambda r: rank.get(r['personaje'], 99))]


def capsules(rows, group, mask, random_word):
    a, b = [], []
    for i, (m, g, maximum, inventory) in enumerate(rows):
        if g != (group & 255) or (m and not (m & mask)):
            continue
        if m:
            b.append(i)
        if inventory < maximum:
            a.append(i)
    pool = a if a else b
    return pool[random_word % len(pool)] if pool else 0


def effective_wish_flags(dw_flags, inventory, inventory_enabled=False, portal=False):
    flags = set(dw_flags)
    if inventory_enabled:
        for f in list(range(461, 490)) + [220, 253]:
            flags.discard(f)
            if inventory.get(f, 0) > 0:
                flags.add(f)
    if portal:
        flags.add(503)
    return flags


def wishes(flags, seed, detail=False):
    def granted(flag):
        return flag in flags
    fixed = next((i for i, (f, req) in enumerate(FIXED)
                  if not granted(f) and all(granted(r) for r in req)), -1)
    c1 = [11 + k for k in range(29) if not granted(314 + k)]
    ok1 = [11 + k for k in range(29) if not granted(314 + k) and granted(461 + k)]
    c2 = [6 + i for i, (f, _) in enumerate(FUSIONS) if not granted(f)]
    ok2 = [6 + i for i, (f, req) in enumerate(FUSIONS)
           if not granted(f) and all(granted(r) for r in req)]
    out = [fixed] if fixed >= 0 else []
    def rnd():
        nonlocal seed
        seed = (seed * 0x5851F42D4C957F2D + 1) & 0xFFFFFFFFFFFFFFFF
        return (seed >> 32) & 0x7FFFFFFF
    def draw(v):
        i = rnd() % len(v)
        out.append(v[i])
        v[i] = v[-1]
        v.pop()
    if ok1:
        for _ in range(2 if not ok2 and len(ok1) > 1 else 1):
            draw(ok1)
    if ok2:
        for _ in range(2 if len(out) < 3 and not ok1 and len(ok2) > 1 else 1):
            draw(ok2)
    if len(out) < 3 and fixed < 0 and (ok1 or ok2):
        v = ok1 if c1 else ok2
        if v:
            out.append(v[rnd() % len(v)])
    if not out and (c2 or c1):
        v = c2 if not c1 else c1
        out.append(v[rnd() % len(v)])
    return (out[:3], seed) if detail else out[:3]
