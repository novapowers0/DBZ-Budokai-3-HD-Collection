// Incluido dentro del namespace de escenarios.cpp. Sonda/ejecución de la VM
// separada del consumidor B3: el registro NO acredita efectos ni movimiento.
thread_local uint32_t g_b2_actor_colision = 0;
struct B2Contacto {
  uint32_t gestor = 0, actor = 0, cuadro = 0, superficie = 0;
  int entrada = -1;
  std::array<float,3> punto{}; // 3734E0/E4/E8: punto exacto de la consulta B2
};
B2Contacto g_b2_contacto;

bool NieveB2Activa() {
  // Nombre histórico de la sonda; el consumidor/colisión es ahora común.
  if(g_activa<kSerie||g_activa>=Total()||dbz3::net::Session::Get().active())return false;
  const uint32_t e=g_altas[size_t(g_activa-kSerie)].entrada;
  return e==5000||e==5002||e==5004||e==5007||e==5008;
}

// El motor B2 corre en pruebas (DBZ3_TEST_B2_MOTOR) o, de serie, solo en la Llanura (unica cotejada con el original).
bool B2MotorOn() {
  if(std::getenv("DBZ3_TEST_B2_MOTOR"))return true;
  return REXCVAR_GET(dbz3_b2_transicion_llanura)&&NieveB2Activa()&&g_altas[size_t(g_activa-kSerie)].entrada==5004;
}

void B2ContactoReal(uint8_t* base, uint32_t actor, uint32_t superficie, uint32_t resultado) {
  if (!actor || !resultado || !NieveB2Activa()) return;
  if (g_b2.version < 2 && !g_b2_leido) { std::vector<Clip> cam; (void)LeerClips(1, cam); }
  const int entrada = b2::EntradaDeSuperficie(g_b2, superficie);
  if (entrada < 0) return;
  const uint32_t mgr = REX_LOAD_U32(kMgrPtr);
  if (!mgr || REX_LOAD_U32(mgr + kEstadoCombate) != 4 ||
      (actor != mgr + 2096 && actor != mgr + 2096 + 8240)) return;
  const uint32_t cuadro = REX_LOAD_U32(0x8238D6B4);
  // Observación de la colisión HD. El disparador lo decide la consulta B2 de
  // segmento extendido, con sus filtros y la espera de lanzamiento posterior.
  if (std::getenv("DBZ3_TEST_ESCENA_VOLCADO")) {
    static uint32_t antes = ~0u;
    if (cuadro != antes) {
      antes = cuadro;
      REXLOG_INFO("dbz3 B2 contacto HD observado: guest={} actor={:08X} superficie={} entradaSPX={} resultado={} accion={} codigo={:X}",
                  cuadro, actor, superficie, entrada, resultado, REX_LOAD_U16(actor + 3244), REX_LOAD_U32(actor + 2928));
    }
  }
}

// Auditoría del intérprete con SPX REAL, sin modificar la pelea. El consumidor
// de registro acepta órdenes para listarlas; NO se presenta como motor visual.
void B2AuditarVm() {
  if (!std::getenv("DBZ3_TEST_ESCENA_VOLCADO") || g_b2.version < 2 || g_b2.grupo != 7) return;
  const b2::Vista spx = g_b2.Bloque(2);
  b2vm::Maquina vm(b2vm::Configurar());
  b2vm::Reloj reloj;
  std::array<uint32_t, 2> codigo{520, 152}; // estado original11094, no un estado B3 inventado
  std::array<uint32_t, 2> aire{};
  std::array<std::array<float, 3>, 2> pos{{{188.415115f, 0, 11.107143f}, {230.280212f, 11.047528f, -3.332968f}}};
  std::vector<std::array<uint32_t, 5>> hitos;
  if (!vm.Cargar(std::vector<uint8_t>(spx.p, spx.p + spx.n), b2vm::kTabla)) {
    REXLOG_WARN("dbz3 B2 VM auditoria: carga fallida {}", vm.Error()); return;
  }
  reloj.Instalar(vm);
  for (uint32_t id : {0u,1u,0x10u,0x11u,0x13u,0x14u,0x15u,0x16u,0x19u,0x1Cu,0x24u,
        0x25u,0x26u,0x27u,0x28u,0x2Au,0x2Du,0x31u,0x35u,0x40u,0x4Cu,0x52u,0x66u,
        0x7Eu,0x7Fu,0x84u,0x86u,0x94u,0x95u,0x96u,0x98u,0xA6u,0xB3u,0xB8u,0xBDu,
        0xC1u,0xD0u,0xD1u,0xD2u,0x10Cu,0x124u,0x160u,0x163u}) {
    vm.Registrar(b2vm::kTabla,id,[&,id](auto& m,auto& h) {
      if (id == 0x25 || id == 0x26 || id == 0x27) {
        const uint32_t lado = m.Arg(h,1);
        if (lado >= 2 || b2vm::Signo(m.Arg(h,0)) != -1) m.Fallar("auditoria: consulta de hueso necesita consumidor real");
        h.f[0] = b2vm::Bits(pos[lado][id - 0x25]);
      } else if (id == 0x28) {
        const uint32_t lado = m.Arg(h,0);
        if (lado >= 2) m.Fallar("auditoria: lado inválido");
        h.r[0] = codigo[lado];
      } else if (id == 0x11) {
        const uint32_t lado = m.Arg(h,2);
        if (lado < 2) codigo[lado] = m.Arg(h,0);
      } else if (id == 0x16) {
        const uint32_t lado = m.Arg(h,1);
        if (lado < 2) aire[lado] = m.Arg(h,0);
      } else if (id == 0x2A) {
        const uint32_t lado = m.Arg(h,0);
        if (lado >= 2) m.Fallar("auditoria: lado inválido");
        h.r[0] = aire[lado]; // ASM1FAD30: byte9EF2, no reloj de animación
      } else if (id == 0x66) {
        // ASM1C7A10 es literalmente return1, no modifica registros.
      } else if (id == 0xBD) {
        h.r[0] = 0; // byteA07=0 en los cuatro estados originales disponibles
      } else if (id == 0x14) {
        h.r[0]=0; // fixture original en entrenamiento14304=3: sin daño
      } else if (id == 0x84 && m.Arg(h,1)==200) {
        h.r[0]=3; // ASM1F86F0: banco200 devuelve canal3; no se simula audio
      } else if (id == 0x84) {
        // Consultas fuera de la evidencia inicial: se detiene, no se inventa
        // un valor para obtener el hito esperado.
        m.Fallar("auditoria requiere estado real para nativa " + std::to_string(id));
      }
      if (id == 0x35 || id == 0x40 || id == 0xC1 || id == 0x96) {
        const uint32_t a0 = id == 0x40 ? 0 : m.Arg(h,0);
        const uint32_t a1 = id == 0x40 ? 0 : m.Arg(h,1);
        hitos.push_back({uint32_t(reloj.cuadro), id, h.r[9] - m.Base(), a0, a1});
      }
      return 1u;
    });
  }
  vm.Iniciar(0);
  for (unsigned n = 0; n < 800 && vm.Viva() && !vm.Bloqueada(); ++n) {
    if (!vm.Tick()) break;
    reloj.cuadro += 1;
  }
  for (const auto& x : hitos)
    REXLOG_INFO("dbz3 B2 VM registro (NO ejecucion visual): cuadro={} nativa={:X} PC={:X} a0={:X} a1={:X}", x[0],x[1],x[2],x[3],x[4]);
  REXLOG_INFO("dbz3 B2 VM auditoria: cuadro={} vivos={} error='{}'; NO aceptación visual/fisica", reloj.cuadro,vm.Vivos(),vm.Error());
}
