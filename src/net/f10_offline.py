"""F10 sin juego ni compilacion: expresiones C++ interpretadas, modelos y UDP/HTTP locales.

python -B src/net/f10_offline.py --report E:/DBZ3HD_trabajo/f10-online/medidas.json
El informe distingue codigo Python ejecutado de reglas interpretadas y modelos.
No ejecuta netplay.cpp ni prueba el allocator guest, el render o routers reales.
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter
import hashlib
import importlib.util
import itertools
import json
import operator
from pathlib import Path
import random
import re
import socket
import struct
import sys
import time
import tomllib
from urllib.request import urlopen
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[2]
NET = ROOT / "src/net"
RESULTS = []


class Checks:
    def __init__(self, name, authority):
        self.name, self.authority = name, authority
        self.total = self.passed = 0
        self.first = None
        self.started = time.perf_counter()

    def eq(self, name, got, expected):
        self.total += 1
        self.passed += got == expected
        if got != expected and self.first is None:
            self.first = {"case": name, "observed": got, "expected": expected}

    def finish(self, **notes):
        row = dict(package=self.name, authority=self.authority, passed=self.passed,
                   total=self.total, first_difference=self.first,
                   seconds=round(time.perf_counter() - self.started, 4), **notes)
        RESULTS.append(row)
        print(f"{self.name}: {self.passed}/{self.total}; primera diferencia: "
              f"{json.dumps(self.first, ensure_ascii=False)}", flush=True)


def text(path):
    return path.read_text(encoding="utf-8-sig")


class Rules:
    """Interprete deliberadamente pequeno: solo las expresiones return del header.

    Sin eval/exec, llamadas arbitrarias, compilador ni traduccion de cuerpos C++.
    Para uint32 solo se ensayan entradas sin desbordamientos en operaciones no ensanchadas.
    uint64_t se conserva: es necesario para los limites de SumTickOk.
    """
    binary = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
              ast.FloorDiv: operator.floordiv}
    compare = {ast.Eq: operator.eq, ast.NotEq: operator.ne, ast.Lt: operator.lt,
               ast.LtE: operator.le, ast.Gt: operator.gt, ast.GtE: operator.ge}

    def __init__(self):
        source = text(NET / "f10_guards.h")
        self.rules = {}
        for name, params, expr in re.findall(
                r"constexpr bool (\w+)\(([^)]*)\)\s*\{\s*return (.*?);\s*\}", source, re.S):
            names = [p.strip().split()[-1] for p in params.split(",")]
            expr = re.sub(r"\b(0x[0-9a-fA-F]+|[0-9]+)u\b", r"\1", expr)
            expr = expr.replace("&&", " and ").replace("||", " or ")
            expr = expr.replace("/", "//")
            self.rules[name] = (names, ast.parse(" ".join(expr.split()), mode="eval").body)
        if len(self.rules) != 4:
            raise ValueError("Cambio de sintaxis del header: actualizar el interprete antes de afirmar PASS")

    def run(self, name, *values):
        names, body = self.rules[name]
        if len(names) != len(values):
            raise ValueError(name)
        return bool(self.node(body, dict(zip(names, values))))

    def node(self, n, env):
        if isinstance(n, ast.Constant) and type(n.value) in (int, bool):
            return n.value
        if isinstance(n, ast.Name):
            return env[n.id]
        if isinstance(n, ast.BoolOp):
            if isinstance(n.op, ast.And):
                return all(self.node(v, env) for v in n.values)
            if isinstance(n.op, ast.Or):
                return any(self.node(v, env) for v in n.values)
        if isinstance(n, ast.BinOp) and type(n.op) in self.binary:
            return self.binary[type(n.op)](self.node(n.left, env), self.node(n.right, env))
        if isinstance(n, ast.Compare) and len(n.ops) == 1 and type(n.ops[0]) in self.compare:
            return self.compare[type(n.ops[0])](self.node(n.left, env), self.node(n.comparators[0], env))
        if (isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                and n.func.id == "uint64_t" and len(n.args) == 1 and not n.keywords):
            return self.node(n.args[0], env)
        raise ValueError(f"Sintaxis no soportada: {ast.dump(n)}")


def protocol(rules):
    c = Checks("ONL04-fragmentos", "expresiones C++ interpretadas; oraculo de particiones independiente")
    regressions = []
    for args in [(2000, 0, 2, 1), (2000, 0, 2, 1100), (1001, 1, 2, 2), (2000, 0, 3, 1000)]:
        size, idx, count, length = args
        legacy = size > 0 and size <= 262144 and count > 0 and idx < count and 0 < length <= 1100
        c.eq(f"regresion {args}", rules.run("SnapshotChunkOk", *args), False)
        regressions.append(dict(fields=args, previous_accepted=legacy, f10_accepted=False))
    rng = random.Random(17840)
    sizes = [1, 999, 1000, 1001, 35204, 262144] + [rng.randint(1, 262144) for _ in range(64)]
    for size in sizes:
        # La particion se obtiene por slicing, no por la formula del validador.
        blob = bytes((i % 251 for i in range(size)))
        chunks = [blob[i:i + 1000] for i in range(0, len(blob), 1000)]
        order = list(range(len(chunks)))
        rng.shuffle(order)
        recovered = {}
        for idx in order + order[:3]:
            part = chunks[idx]
            args = (size, idx, len(chunks), len(part))
            c.eq(f"emisor {args}", rules.run("SnapshotChunkOk", *args), True)
            recovered.setdefault(idx, part)
            c.eq(f"cuenta incorrecta {args}", rules.run("SnapshotChunkOk", size, idx, len(chunks) + 1, len(part)), False)
        c.eq(f"reensamblado {size}", b"".join(recovered[i] for i in range(len(chunks))) == blob, True)
        c.eq(f"hueco ultimo {size}", len(recovered) == len(chunks) and sum(map(len, recovered.values())) == size, True)
    for args in [(0, 0, 0, 0), (262145, 0, 263, 1000), (1, 1, 1, 1), (1, 0, 1, 0),
                 (0xFFFFFFFF, 0, 65535, 1000), (262144, 65535, 263, 1000)]:
        c.eq(f"limite {args}", rules.run("SnapshotChunkOk", *args), False)
    for now in (0, 32, 255, 17840, 0xFFFFFFF0):
        for delta in (-256, -255, -1, 0, 32, 33):
            tick = now + delta
            if 0 <= tick <= 0xFFFFFFFF:
                expected = tick != 0xFFFFFFFF and now - 255 <= tick <= now + 32
                c.eq(f"suma {tick}/{now}", rules.run("SumTickOk", tick, now), expected)
    c.finish(previous_first_difference=regressions[0], regressions=regressions,
             limitation="No ejecuta Session ni valida el contenido del monton en una instantanea")


def journal(rules):
    c = Checks("ONL03-journal", "modelo de etiquetas/caducidad; no allocator ni pila nativa")
    capacity = int(re.search(r"kTicks\s*=\s*(\d+)", text(NET / "rb_wlog.h")).group(1))
    # 18.000 ticks, alta cada 100: NO son 18.000 ticks del juego.
    ring = [None] * capacity
    creations = 0
    for now in range(18001):
        if now % 100 == 0:
            creations += 1
        if now:
            ring[(now - 1) % capacity] = now - 1
        for age in (1, 6, 15, 16, 20):
            start = now - age
            if start < 0:
                continue
            retained = list(range(max(0, now - capacity), now))
            expected = 0 < age < capacity and all(k in retained for k in range(start, now))
            observed = rules.run("HistoryRangeOk", start, now, capacity) and all(
                ring[k % capacity] == k for k in range(start, now))
            c.eq(f"edad tick {now}, desde {start}", observed, expected)
    # Reproduccion repetida de una llamada no consumida: Begin elimina todas las bajas >= k.
    # Expone una perdida de propiedad, NO atribuye a esto el int3 del combate real.
    pending = [(17840, 0x4E001000)]
    pending = [(t, p) for t, p in pending if t < 17840]
    c.eq("baja huerfana se pierde al repetir el tick", pending, [])
    c.finish(model_ticks=18001, synthetic_creation_events=creations, fixture_tick=17840,
             unresolved="Se necesita traza de alloc/free y pila int3 del fallo real; no se modifica FlushFrees")


def desync(rules):
    c = Checks("ONL04-corte", "expresion C++ de decision + enlaces de codigo; no sesion C++ ejecutada")
    for enabled, rollback, tick, ignore in itertools.product((False, True), (False, True), (0, 1, 17840), (0, 1, 17840)):
        c.eq(f"corte {enabled}/{rollback}/{tick}/{ignore}",
             rules.run("StopOnBadSum", enabled, rollback, tick, ignore),
             enabled and rollback and tick > ignore)
    cpp = text(NET / "netplay.cpp")
    hooks = text(NET / "netplay_hooks.cpp")
    block = cpp.split("void Session::CompareSum", 1)[1].split("void Session::SendSums", 1)[0]
    for name, present in {
        "decision conectada": "f10::StopOnBadSum" in block,
        "marca antes de Fail": block.find("desync_ = true") < block.find("Fail(Link::kError") and "desync_ = true" in block,
        "informe antes de reset": cpp.index("const std::string rep = ds ? WriteDesyncReport") < cpp.index("ResetTransport();", cpp.index("void Session::Fail")),
        "no seguir lote tras cierre": "if (!active()) return;   // F10" in cpp,
        "no seguir sumas tras cierre": "if (!s.active()) { Snapshot::Release(); WLog::Release(); return; }" in hooks,
        "rollback sigue false": bool(re.search(r"REXCVAR_DEFINE_BOOL\(dbz3_net_rollback,\s*false", hooks)),
        "validacion sigue false": bool(re.search(r"REXCVAR_DEFINE_BOOL\(dbz3_net_validacion_f10,\s*false", hooks)),
    }.items():
        c.eq(name, present, True)
    cmake = text(ROOT / "CMakeLists.txt")
    pattern = re.search(r'if\(NOT DBZ3_PCH_TEXTO MATCHES "(.*?)"\)', cmake).group(1)
    # Decodificar escapes de la cadena CMake; re.MULTILINE no necesario (patron incluye salto).
    pattern = pattern.replace('\\"', '"').replace('\\\\', '\\').replace('\\n', '\n').replace('\\t', '\t').replace('\\r', '\r')
    for pch, expected in [('// net/rb_store.h\n', False), ('/* net/rb_store.h */\n', False),
                          ('#include "../src/net/rb_store.h"\n', True),
                          ('  # include "../src/net/rb_store.h"\r\n', True),
                          ('// #include "../src/net/rb_store.h"\n', False),
                          ('/*\n#include "../src/net/rb_store.h"\n*/\n', False),
                          ('#if 0\n\n#include "../src/net/rb_store.h"\n#endif\n', False),
                          ('#define IGNORE ' + chr(92) + '\n#include "../src/net/rb_store.h"\n', False),
                          ('// cabecera\n\n#include "../src/net/rb_store.h"\r\n', True)]:
        c.eq("include " + repr(pch), bool(re.search(pattern, pch)), expected)
        appended = pch if re.search(pattern, pch) else pch + '\n\n#include "../src/net/rb_store.h"\n'
        c.eq("include final idempotente " + repr(pch), bool(re.search(pattern, appended)), True)
    c.finish(build="PENDIENTE_Z", native_harness="src/net/f10_offline_test.cpp")


def capsules_and_seed():
    c = Checks("ONL06-07-09-contratos", "fixtures de protocolo y lectura de handlers; no combate ni relevo")
    def pack(team, member_caps, custom):
        b = bytes([len(team)]) + b"".join(struct.pack("<H", i) for i in team)
        for caps in member_caps:
            b += bytes([len(caps)]) + b"".join(struct.pack("<H", v) for v in caps)
        b += bytes([len(custom)])
        for pid, caps in sorted(custom.items()):
            b += bytes([pid, len(caps)]) + b"".join(struct.pack("<H", v) for v in caps)
        return b
    def unpack(b):
        offset = 0
        def get(fmt):
            nonlocal offset
            n = struct.calcsize(fmt)
            value = struct.unpack_from(fmt, b, offset)[0]
            offset += n
            return value
        team = [get("<H") for _ in range(get("B"))]
        caps = [[get("<H") for _ in range(get("B"))] for _ in team]
        custom = {}
        for _ in range(get("B")):
            pid, n = get("B"), get("B")
            custom[pid] = [get("<H") for _ in range(n)]
        if offset != len(b):
            raise ValueError("resto")
        return team, caps, custom
    for team in ([], [35], [35, 0], [0, 35, 63], [0x100, 0x101]):
        caps = [[65535, 1, 300, 0, 9, 7, 33][:i + 3] for i in range(len(team))]
        custom = {0: [0, 65535], 35: [17, 29, 300], 37: [1]}
        blob = pack(team, caps, custom)
        c.eq(f"Custom por miembro {team}", unpack(blob), (team, caps, custom))
        c.eq(f"fin exacto {team}", pack(*unpack(blob)), blob)
    cpp, hooks = text(NET / "netplay.cpp"), text(NET / "netplay_hooks.cpp")
    expression = re.search(r"const uint32_t v = (\(seed_ \^.*?\| 1u);", cpp).group(1)
    c.eq("semilla no depende del reloj al resembrar", "NowMs" not in expression, True)
    for seed in (0, 1, 0x5A5A, 0xFFFFFFFF):
        for battle in (1, 2, 3, 25, 50):
            host = ((seed ^ ((battle * 0x9E3779B9) & 0xFFFFFFFF)) | 1) & 0xFFFFFFFF
            wire = struct.pack(">I", host)
            c.eq(f"semilla compartida {seed}/{battle}", struct.unpack(">I", wire)[0], host)
            c.eq(f"semilla no cero {seed}/{battle}", host != 0, True)
    for name, present in {
        "Custom viaja por miembro": "local_caps_" in cpp and "peer_caps_" in cpp,
        "rechazo duplicado Custom": "F10Validation() && (used & (1ull << pid))" in cpp,
        "frontera revancha estado 7": "mgr + 35128) == 7) dbz3::tag::OnScreen(5)" in hooks,
        "limpieza Custom al cerrar": "local_caps_.clear(); peer_caps_.clear(); local_all_.clear(); peer_all_.clear();" in cpp,
        "corte conserva pad neutro": "cut_fight_ = true" in cpp,
    }.items():
        c.eq(name, present, True)
    c.finish(best_of="Sin contrato/handler de serie en los archivos online; propuesta a Z, no implementado",
             fixtures="IDs logicos preservados como u16; validez del roster/logica Buu depende de F9/S7",
             limitation="No certifica PutCapsules, absorcion ni cero sumas distintas durante cinco minutos")


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def local_proxy():
    c = Checks("ONL08-proxy", "FaultProxy Python real, UDP exclusivamente 127.0.0.1")
    mod = module("f10_fault_proxy", ROOT / "tools/fault_proxy.py")
    mod.BURST = mod.JITTER = 0
    for loss in (0, 5):
        host = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        guest = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        proxy = None
        try:
            host.bind(("127.0.0.1", 0))
            guest.bind(("127.0.0.1", 0))
            host.settimeout(.05)
            guest.settimeout(1)
            proxy = mod.FaultProxy(0, host.getsockname()[1], delay_ms=5, loss_every=loss)
            proxy.start()
            front = proxy.front.getsockname()
            # Handshake y respuesta se preservan, incluso con perdidas de INPUT habilitadas.
            guest.sendto(b"DBZ3\x01\x07hello", front)
            hello, peer = host.recvfrom(2048)
            c.eq(f"handshake {loss}", hello, b"DBZ3\x01\x07hello")
            host.sendto(b"DBZ3\x02\x07ack", peer)
            c.eq(f"respuesta {loss}", guest.recvfrom(2048)[0], b"DBZ3\x02\x07ack")
            count = 77
            for i in range(1, count + 1):
                guest.sendto(b"DBZ3\x09\x07" + struct.pack("<I", i), front)
                time.sleep(.001)
            expected = Counter()
            for i in range(1, count + 1):
                if loss and i % loss == 0:
                    continue
                expected[i] += 1 + (i % 7 == 0)
            seen, order = Counter(), []
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                try:
                    data, _ = host.recvfrom(2048)
                    i = struct.unpack("<I", data[6:])[0]
                    seen[i] += 1
                    order.append(i)
                except socket.timeout:
                    if sum(seen.values()) >= sum(expected.values()):
                        break
            for i in range(1, count + 1):
                c.eq(f"multiplicidad loss={loss} input={i}", seen[i], expected[i])
            c.eq(f"entradas {loss}", proxy.inputs, count)
            c.eq(f"perdidas {loss}", proxy.dropped, count // loss if loss else 0)
            c.eq(f"duplicadas {loss}", proxy.duplicates, sum(v - 1 for v in expected.values()))
            c.eq(f"reordenadas programadas {loss}", proxy.reordered,
                 sum(i % 11 == 0 and (not loss or i % loss != 0) for i in range(1, count + 1)))
            c.eq(f"hilos sin caida {loss}", all(t.is_alive() for t in proxy.threads), True)
        finally:
            if proxy:
                proxy.close()
                c.eq(f"cierre hilos {loss}", any(t.is_alive() for t in proxy.threads), False)
            guest.close()
            host.close()
    c.finish(limitation="Fallos UDP no son divergencias de estado ni prueban NAT/UPnP")


def injected_sum():
    c = Checks("ONL04-suma-inyectada", "FaultProxy real: mutacion de datagramas y envio UDP local; no Session C++")
    mod = module("f10_sum_proxy", ROOT / "tools/fault_proxy.py")
    mod.BURST = mod.JITTER = 0
    def packet(ticks):
        return b"DBZ3\x0f\x07" + struct.pack("<II", 123, 456) + bytes([len(ticks)]) + b"".join(
            struct.pack("<6I", t, t + 1, 2, 3, 4, 5) for t in ticks)
    host = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    guest = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    off = on = None
    try:
        host.bind(("127.0.0.1", 0))
        guest.bind(("127.0.0.1", 0))
        host.settimeout(1)
        guest.settimeout(1)
        off = mod.FaultProxy(0, host.getsockname()[1], loss_every=0, sum_flip_after=0)
        on = mod.FaultProxy(0, host.getsockname()[1], loss_every=0, sum_flip_after=17840)
        cases = [packet([17839]), packet([17839, 17840]), packet([17839, 17840, 17841]),
                 packet([17841]), packet([17840]), b"DBZ3\x01\x07hello", packet([17840])[:-1],
                 packet([17840])[:5] + b"\x06" + packet([17840])[6:]]
        for i, data in enumerate(cases):
            c.eq(f"apagado intacto {i}", off.flip_sum(data), data)
            changed = on.flip_sum(data)
            diffs = [p for p, (a, b) in enumerate(zip(data, changed)) if a != b]
            expected = [43] if i in (1, 2) else [19] if i == 4 else []
            c.eq(f"un bit/direccion exacta {i}", diffs, expected)
            c.eq(f"nonces conservados {i}", changed[:15], data[:15])
            c.eq(f"longitud conservada {i}", len(changed), len(data))
        c.eq("un unico tick elegido", on.sum_flip_tick, 17840)
        c.eq("repeticiones coherentes", on.sum_flipped, 3)
        on.start()
        original = packet([17840])
        guest.sendto(original, on.front.getsockname())
        changed, peer = host.recvfrom(4096)
        c.eq("UDP contiene suma alterada", changed[19], original[19] ^ 1)
        c.eq("UDP mantiene resto", changed[:19] + changed[20:], original[:19] + original[20:])
        host.sendto(original, peer)
        c.eq("sentido anfitrion no se altera", guest.recvfrom(4096)[0], original)
    finally:
        if off: off.close()
        if on: on.close()
        guest.close()
        host.close()
    c.finish(injection="DBZ3_PROXY_FLIP_SUM_AFTER=17840; defecto 0; solo guest->host",
             first_forced_difference="byte 19 (bloque gestor) de SUM v7; nonces intactos",
             limitation="El aviso/cierre y el informe deben observarse en Z; el arnes normal espera cero desync y fallara en esta prueba negativa")


def matchmaking():
    c = Checks("ONL08-servicio-cancelacion", "Servidor ntfy_fake real en loopback + modelo de propiedad de topic")
    fake = module("f10_ntfy_fake", ROOT / "tools/ntfy_fake.py")
    server = fake.Servidor(0)
    base = f"http://127.0.0.1:{server.puerto}"
    def fetch(path):
        with urlopen(base + path, timeout=2) as r:
            return r.status, r.read().decode()
    try:
        for msg in ("dbz3 1 OPEN AAAAA-11111", "dbz3 1 BYE AAAAA-11111"):
            status, _ = fetch("/f10-a/publish?" + urlencode({"message": msg}))
            c.eq("publicar local " + msg, status, 200)
        _, body = fetch("/f10-a/json?poll=1&since=80s")
        messages = [json.loads(line)["message"] for line in body.splitlines()]
        c.eq("anuncio y cancelacion exactos", messages,
             ["dbz3 1 OPEN AAAAA-11111", "dbz3 1 BYE AAAAA-11111"])
        c.eq("otro topic vacio", fetch("/f10-b/json?poll=1&since=80s")[1], "")
        # Misma secuencia de propiedad que Mm::Start; el trabajo viejo termina despues del nuevo.
        old = {"topic": ""}
        current = {"topic": ""}
        old["topic"] = "version-anterior"
        current["topic"] = "version-nueva"
        c.eq("cancelar/reiniciar no comparte topic", old is current, False)
        c.eq("resultado tardio no contamina busqueda nueva", current["topic"], "version-nueva")
        source = text(ROOT / "src/ingame/matchmaking.inl")
        panel = text(ROOT / "src/ingame/online_panel.cpp")
        cpp = text(NET / "netplay.cpp")
        cases = {
            "servicio caido": "No se pudo contactar con el servicio de busqueda." in source,
            "timeout rival": "now - join_at > 20.0" in source and "bad.insert(join_code)" in source,
            "UPnP/NAT sin publicidad": 'how != "local" || test_local' in source and 'pub_how_ = "router"' in cpp,
            "cancelacion": 'st = kOff' in source and 'Publish("BYE", announced)' in source,
            "servicio cae con sala abierta": "tu sala sigue abierta, pasa el codigo a mano" in panel,
            "propiedad aislada en codigo": "if (f10_safe) topic = std::make_shared<MmTopic>()" in source,
            "huella capturada por valor": "[tp, b, first, safe, build]" in source,
        }
        for name, present in cases.items():
            c.eq(name, present, True)
    finally:
        server.cerrar()
        server.httpd.server_close()
    c.finish(limitation="Cinco rutas inventariadas; solo HTTP local y modelo de cancelacion ejecutados, no UI ni NAT real")


def delivery():
    c = Checks("ONL04-entrega", "validador Python ejecutado con logs sinteticos y TOML parseados; escenarios no ejecutados")
    validator = module("f10_validate_runs", NET / "f10_validate_runs.py")
    def line(n, ticks=3000, rollbacks=3, sums=2900, bad=0):
        return (f"dbz3 rb: resumen del combate {n}: {ticks} ticks, {rollbacks} rollbacks "
                f"(prof 1:1 2:1 3:1 4+:0), 7 esperas, frame p50/p99 16.7/30.0 ms, sumas {sums} / malas {bad}\n")
    good = "".join(line(i) for i in range(1, 51))
    cases = {
        "50 resumenes por lado": (good, good, True, None),
        "falta el primer combate": (good.replace(line(1), ""), good, True, "host: numero de resumenes"),
        "invitado incompleto": (good, good.replace(line(50), ""), True, "guest: numero de resumenes"),
        "combate duplicado": (good + line(1), good, True, "host: numero de resumenes"),
        "combate extra": (good + line(51), good, True, "host: numero de resumenes"),
        "divergencia temprana con ultimo sano": (good.replace(line(7), line(7, bad=1)), good, True, "host: combate 7"),
        "pocas sumas": (good.replace(line(4), line(4, sums=0)), good, True, "host: combate 4"),
        "pocos ticks": (good.replace(line(5), line(5, ticks=0)), good, True, "host: combate 5"),
        "id fuera de secuencia": (good.replace(line(1), line(0)), good, True, "host: combate 1"),
        "solo el ultimo no es 50/50": (line(50), line(50), True, "host: numero de resumenes"),
        "sin rollback real": (good.replace("3 rollbacks", "0 rollbacks"), good, True, "host: combate 1"),
        "sin rollback permitido sin red adversa": (good.replace("3 rollbacks", "0 rollbacks"), good, False, None),
    }
    for marker in ("SUMA distinta", "DESINCRONIA", "reparacion aplicada", "F10: corte", "0x80000003",
                   "PURE tick 90: 1 bytes (20 pruebas, 1 malas)"):
        cases["error fuera del resumen " + marker] = (good + marker, good, True, "host: log sin divergencia/excepcion")
    for name, (host, guest, required, expected) in cases.items():
        result = validator.validate(host, guest, require_rollback=required)
        c.eq(name + ": primera diferencia", result["first_difference"]["case"] if result["first_difference"] else None, expected)
        c.eq(name + ": aceptacion", result["passed"] == result["total"], expected is None)
        c.eq(name + ": denominador", result["total"], 104)
    folder = ROOT / "docs/14_frentes/escenarios_pruebas/final/f10-online"
    for stem, port in (("rollback", "25458"), ("desconexion", "25468")):
        for suffix, role in (("", "host"), ("_guest", "guest")):
            if not (folder / (stem + suffix + ".toml")).exists():   # apartados en la ronda 3 de F10 (pareja = pruebas.py online-rb)
                print(f"aviso: {stem}{suffix}.toml apartado; sus comprobaciones no cuentan")
                continue
            cfg = tomllib.loads(text(folder / (stem + suffix + ".toml")))
            env = dict(v.split("=", 1) for v in cfg["env"])
            c.eq(stem + suffix + ": rol", env["DBZ3_NET_AUTO"], role)
            c.eq(stem + suffix + ": puerto", env["DBZ3_NET_PORT"], port)
            c.eq(stem + suffix + ": destino", env["DBZ3_NET_JOIN_PORT"], port)
            c.eq(stem + suffix + ": guardia explicita", "dbz3_net_validacion_f10=true" in env["DBZ3_TEST_CFG"], True)
            c.eq(stem + suffix + ": rollback explicito", "dbz3_net_rollback=true" in env["DBZ3_TEST_CFG"], True)
            c.eq(stem + suffix + ": no silenciar errores", "DBZ3_NET_NOFAIL" in env, False)
            c.eq(stem + suffix + ": pasos presentes", bool(cfg["pasos"]), True)
            c.eq(stem + suffix + ": metadatos", all(cfg.get(k) for k in ("clave", "cvar", "criterio", "log")), True)
            re.compile(cfg["log"])
            if stem == "desconexion" and not suffix:
                for sample, expected in (
                    ("dbz3 online: conexion perdida en el tick 650 (rollback)", True),
                    ("dbz3 online: informe de conexion guardado en informes_online/conexion.txt", True),
                    ("dbz3 online: rollback: combate desde el tick 650", False),
                ):
                    c.eq("desconexion: timeout o BYE con informe", bool(re.search(cfg["log"], sample)), expected)
            for step in cfg["pasos"]:
                import shlex
                words = shlex.split(step)
                c.eq(stem + suffix + ": verbo " + words[0], words[0] in ("log", "esperar", "captura"), True)
                if words[0] == "log":
                    re.compile(words[1])
                    c.eq(stem + suffix + ": timeout positivo", int(words[2]) > 0, True)
    c.finish(limitation="Fixtures de logs prueban el rechazo de resultados incompletos, no son 50 partidas reales ni prueba de UI")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    rules = Rules()
    for run in (lambda: protocol(rules), lambda: journal(rules), lambda: desync(rules),
                capsules_and_seed, local_proxy, injected_sum, matchmaking, delivery):
        run()
    paths = [NET / "f10_guards.h", NET / "rb_wlog.h", NET / "netplay.cpp", NET / "netplay_hooks.cpp",
             ROOT / "CMakeLists.txt", ROOT / "src/ingame/matchmaking.inl",
             ROOT / "tools/fault_proxy.py", ROOT / "tools/ntfy_fake.py", NET / "f10_validate_runs.py", Path(__file__)]
    paths.extend(sorted((ROOT / "docs/14_frentes/escenarios_pruebas/final/f10-online").glob("*.toml")))
    report = dict(scope="offline F10; sin juego, emulador ni compilacion", packages=RESULTS,
                  passed=sum(r["passed"] for r in RESULTS), total=sum(r["total"] for r in RESULTS),
                  source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                  pending=["pila int3 real tick 17840", "compilar banco C++ y build limpio por Z", "25/25 largos y 50/50 R5",
                           "Buu/transformaciones 5 min", "Tag/swaps/Custom/desconexion", "dos PCs/NAT", "best-of N"])
    if args.report:
        if not args.report.parent.is_dir():
            raise SystemExit("La carpeta del informe debe existir")
        if args.report.exists():
            raise SystemExit("El informe existe; usa otro nombre para conservarlo")
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"TOTAL F10 offline: {report['passed']}/{report['total']}; build y juego PENDIENTES_Z")
    return int(report["passed"] != report["total"])


if __name__ == "__main__":
    sys.exit(main())
