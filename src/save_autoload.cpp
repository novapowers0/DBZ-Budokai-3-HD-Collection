#include "save_autoload.h"

#include <rex/logging.h>
#include <rex/hook.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "generated/dbz3_init.h"
#include "eu_map.h"
#include "guest_region.h"
#include "launcher/settings.h"

namespace fs = std::filesystem;

#if defined(DBZ3_DUAL_REGION)
REX_EXTERN(dbz3eu_sub_82089F20);
REX_EXTERN(dbz3eu_sub_82089BD8);
REX_EXTERN(dbz3eu_sub_821D2290);
REX_EXTERN(dbz3eu_sub_821D1D68);
REX_EXTERN(dbz3eu_sub_821D2C40);
REX_EXTERN(dbz3eu_sub_8218C5B0);
REX_EXTERN(dbz3eu_sub_82197068);
REX_EXTERN(dbz3eu_sub_821D1620);
REX_EXTERN(dbz3eu_sub_82195D10);
#endif

namespace {

constexpr size_t kExpectedSaveSize = 56416;

std::atomic<bool> g_bg_started{false};
std::atomic<bool> g_bg_finished{false};
std::atomic<bool> g_has_save{false};
std::atomic<bool> g_save_applied{false};

std::mutex g_save_mutex;
std::vector<uint8_t> g_save_data;

fs::path FindSavePath() {
  const fs::path root = dbz3::settings::UserDataRoot();
  std::error_code ec;
  for (auto [it, iec] = std::pair{fs::recursive_directory_iterator(root, ec), std::error_code{}};
       !iec && it != fs::recursive_directory_iterator(); it.increment(iec)) {
    const auto& p = it->path();
    if (p.filename() == "data.bin" && p.parent_path().filename() == "DBZ3" &&
        p.string().find("_online") == std::string::npos) {
      return p;
    }
  }
  return {};
}

void DoBackgroundRead() {
  const fs::path path = FindSavePath();
  if (path.empty()) {
    REXLOG_INFO("save_autoload: No se encontro ninguna partida guardada previa (data.bin).");
    g_bg_finished = true;
    return;
  }

  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) != kExpectedSaveSize) {
    REXLOG_WARN("save_autoload: Archivo '{}' no tiene el tamano esperado ({} bytes).",
                path.string(), kExpectedSaveSize);
    g_bg_finished = true;
    return;
  }

  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    REXLOG_WARN("save_autoload: No se pudo abrir '{}' para lectura.", path.string());
    g_bg_finished = true;
    return;
  }

  std::vector<uint8_t> buf(kExpectedSaveSize);
  f.read(reinterpret_cast<char*>(buf.data()), kExpectedSaveSize);
  if (f.gcount() != static_cast<std::streamsize>(kExpectedSaveSize)) {
    REXLOG_WARN("save_autoload: Lectura incompleta de '{}'.", path.string());
    g_bg_finished = true;
    return;
  }

  // Comprobar cabecera magica (#SPF 1.0)
  if (std::memcmp(buf.data(), "#SPF 1.0", 8) != 0) {
    REXLOG_WARN("save_autoload: Cabecera magica invalida en '{}'.", path.string());
    g_bg_finished = true;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_save_mutex);
    g_save_data = std::move(buf);
    g_has_save = true;
  }

  REXLOG_INFO("save_autoload: Partida guardada encontrada y leida en background: '{}'", path.string());
  g_bg_finished = true;
}

}  // namespace

namespace dbz3::save_autoload {

void StartBackgroundRead() {
  bool expected = false;
  if (g_bg_started.compare_exchange_strong(expected, true)) {
    std::thread t([] { DoBackgroundRead(); });
    t.detach();
  }
}

bool HasSave() {
  return g_has_save.load();
}

bool IsApplied() {
  return g_save_applied.load();
}

bool ApplySave(PPCContext& ctx, uint8_t* base) {
  if (g_save_applied.load()) return true;

  std::vector<uint8_t> save_bytes;
  {
    std::lock_guard<std::mutex> lock(g_save_mutex);
    if (!g_has_save.load() || g_save_data.size() != kExpectedSaveSize) {
      return false;
    }
    save_bytes = g_save_data;
  }

  // 1. Reservar memoria guest para el buffer temporal de la partida
  PPCContext lctx{};
  lctx.r3.u64 = kExpectedSaveSize;
  lctx.r4.u64 = 0;
  lctx.r5.u64 = dbz3::GuestAddr(0x8202072C, 0x82020734);
  lctx.r6.u64 = 1;
  DBZ3_CALL2(sub_82089F58, dbz3eu_sub_82089F20)(lctx, base);
  const uint32_t gbuf = lctx.r3.u32;
  if (!gbuf) {
    REXLOG_WARN("save_autoload: No se pudo reservar memoria en el guest para desempaquetar la partida.");
    return false;
  }

  // 2. Copiar los bytes leidos al espacio guest
  std::memcpy(base + gbuf, save_bytes.data(), kExpectedSaveSize);

  // 3. Descifrar y validar el checksum (sub_821D34A8)
  lctx.r3.u64 = gbuf;
  lctx.r4.u64 = kExpectedSaveSize;
  DBZ3_CALL2(sub_821D34A8, dbz3eu_sub_821D2290)(lctx, base);
  if (lctx.r3.s32 != 0) {
    REXLOG_WARN("save_autoload: Checksum o descifrado de partida invalido (codigo={}).", lctx.r3.s32);
    lctx.r3.u64 = gbuf;
    DBZ3_CALL2(sub_82089C10, dbz3eu_sub_82089BD8)(lctx, base);
    return false;
  }

  // 4. Desempaquetar la partida en UI_STATE (0x824BA110)
  lctx.r3.u64 = dbz3::GA(0x824BA110);
  lctx.r4.u64 = gbuf;
  DBZ3_CALL2(sub_821D2F80, dbz3eu_sub_821D1D68)(lctx, base);

  // 5. Marcar bandera 0x82375610 = 1 (partida cargada en el motor)
  DBZ3_CALL2(sub_821D3E58, dbz3eu_sub_821D2C40)(lctx, base);

  // 6. Configurar dispositivo de almacenamiento activo (HDD = 1)
  // Al saltarse la pantalla de Pan, el juego no pasa por la selección interactiva
  // de almacenamiento, dejando el dispositivo en -1. Establecerlo en 1 (HDD)
  // permite que el motor guarde ajustes (como opciones de duelo) directamente.
  REX_STORE_U32(0x8238AFD8, 1);
  REX_STORE_U32(0x8238AFDC, 1);
  REX_STORE_U32(0x82373328, 1);

  // 7. Liberar el buffer guest temporal
  lctx.r3.u64 = gbuf;
  DBZ3_CALL2(sub_82089C10, dbz3eu_sub_82089BD8)(lctx, base);

  g_save_applied = true;
  REXLOG_INFO("save_autoload: Partida guardada aplicada con exito en memoria (UI_STATE).");
  return true;
}

void Tick(PPCContext& ctx, uint8_t* base) {
  if (!g_bg_started.load()) {
    StartBackgroundRead();
  }
}

}  // namespace dbz3::save_autoload

// =============================================================================
// Trace Hooks & Screen Interceptors
// =============================================================================

#define SAVE_TRACE_HOOK(us)                                                                        \
  static void save_trace_##us(PPCContext& ctx, uint8_t* base, [[maybe_unused]] Dbz3GuestFn orig); \
  REX_HOOK_RAW(us) { save_trace_##us(ctx, base, __imp__##us); }                                   \
  static void save_trace_##us(PPCContext& ctx, uint8_t* base, [[maybe_unused]] Dbz3GuestFn orig)

// -----------------------------------------------------------------------------
// Gancho primordial: sub_82184138 (Punto de creación de la pantalla de Pan)
// Llamado desde sub_8219F9E8 cuando el jugador pulsa START en la Pantalla de Inicio.
// En lugar de cargar el escenario 3D de Pan y hacer el fundido a Pan:
// 1. Inyectamos la partida en memoria (ApplySave).
// 2. Aplicamos la configuración del motor (sub_8218F8B8 + sub_82182C98 + sub_821D2820).
// 3. Ordenamos la transición inmediata al Menú Principal (sub_821D2808(5)).
// 4. Retornamos sin llamar a orig(ctx, base), evitando que Pan se cargue o renderice.
SAVE_TRACE_HOOK(sub_82184138) {
  REXLOG_INFO("save_autoload: sub_82184138 CALLED (skip_load={}, has_save={}, applied={})",
              dbz3::settings::SkipLoadScreen(), dbz3::save_autoload::HasSave(),
              dbz3::save_autoload::IsApplied());

  if (dbz3::settings::SkipLoadScreen() && dbz3::save_autoload::HasSave()) {
    REXLOG_INFO("save_autoload: Saltando creacion de pantalla de Pan. Aplicando partida y pasando a Menu Principal...");
    if (!dbz3::save_autoload::IsApplied()) {
      dbz3::save_autoload::ApplySave(ctx, base);
    }
    REX_STORE_U32(0x8238AFD8, 1);
    REX_STORE_U32(0x8238AFDC, 1);
    REX_STORE_U32(0x82373328, 1);
    PPCContext lctx{};
    lctx.r3.u64 = 0x82182C98;
    sub_8218F8B8(lctx, base);
    sub_82182C98(lctx, base);
    sub_821D2820(lctx, base);
    lctx.r3.u64 = 5;
    sub_821D2808(lctx, base);
    return;
  }
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Red de seguridad 1: sub_82183FD0 (Entrada y voz de Pan)
// Por si alguna ruta alternativa llega a instanciar la tarea de Pan.
SAVE_TRACE_HOOK(sub_82183FD0) {
  REXLOG_INFO("save_autoload: sub_82183FD0 CALLED (skip_load={}, has_save={}, applied={})",
              dbz3::settings::SkipLoadScreen(), dbz3::save_autoload::HasSave(),
              dbz3::save_autoload::IsApplied());

  if (dbz3::settings::SkipLoadScreen() && dbz3::save_autoload::HasSave()) {
    REXLOG_INFO("save_autoload: Interceptado sub_82183FD0. Saltando pantalla de Pan directamente al Menu Principal...");
    if (!dbz3::save_autoload::IsApplied()) {
      dbz3::save_autoload::ApplySave(ctx, base);
    }
    PPCContext lctx{};
    lctx.r3.u64 = 0x82182C98;
    sub_8218F8B8(lctx, base);
    sub_82182C98(lctx, base);
    sub_821D2820(lctx, base);
    const uint32_t pan_task = ctx.r3.u32;
    if (pan_task != 0) {
      REX_STORE_U32(pan_task + 28, 0x82183970);
    }
    return;
  }
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Red de seguridad 2: sub_821839B8 (Menú interactivo de Pan)
SAVE_TRACE_HOOK(sub_821839B8) {
  REXLOG_INFO("save_autoload: sub_821839B8 CALLED (skip_load={}, has_save={}, applied={})",
              dbz3::settings::SkipLoadScreen(), dbz3::save_autoload::HasSave(),
              dbz3::save_autoload::IsApplied());

  if (dbz3::settings::SkipLoadScreen() && dbz3::save_autoload::HasSave()) {
    REXLOG_INFO("save_autoload: Interceptado sub_821839B8. Saltando pantalla de Pan directamente al Menu Principal...");
    if (!dbz3::save_autoload::IsApplied()) {
      dbz3::save_autoload::ApplySave(ctx, base);
    }
    PPCContext lctx{};
    lctx.r3.u64 = 0x82182C98;
    sub_8218F8B8(lctx, base);
    sub_82182C98(lctx, base);
    sub_821D2820(lctx, base);
    const uint32_t pan_task = ctx.r3.u32;
    if (pan_task != 0) {
      REX_STORE_U32(pan_task + 28, 0x82183970);
    }
    return;
  }
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Red de seguridad 3: sub_82183E60 (Polling del diálogo de carga en pantalla de Pan)
SAVE_TRACE_HOOK(sub_82183E60) {
  if (dbz3::settings::SkipLoadScreen() && dbz3::save_autoload::HasSave()) {
    REXLOG_INFO("save_autoload: Interceptado sub_82183E60. Saltando pantalla de Pan...");
    if (!dbz3::save_autoload::IsApplied()) {
      dbz3::save_autoload::ApplySave(ctx, base);
    }
    PPCContext lctx{};
    lctx.r3.u64 = 0x82182C98;
    sub_8218F8B8(lctx, base);
    sub_82182C98(lctx, base);
    // Destruir diálogo únicamente si se creó realmente
    const uint32_t dlg = REX_LOAD_U32(0x82460000 - 3496);
    if (dlg != 0) {
      sub_8219BC88(lctx, base);
    }
    sub_821D2820(lctx, base);
    const uint32_t pan_task = ctx.r3.u32;
    if (pan_task != 0) {
      REX_STORE_U32(pan_task + 28, 0x82183970);
    }
    return;
  }
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Sonda: sub_82183970 (Transición al Menú Principal)
SAVE_TRACE_HOOK(sub_82183970) {
  REXLOG_INFO("save_autoload: sub_82183970 CALLED (task={:08X})", ctx.r3.u32);
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Guardia de audio: sub_821DB0F8 (Detener stream de voz)
// Al entrar al Menú Principal (Screen 5), el constructor invoca sub_821FACF8 -> sub_821FA8B8 -> sub_821DB0F8
// para detener cualquier línea de voz activa que viniera de la pantalla previa (la voz de Pan).
// Si Pan fue saltada, no hay ninguna pista de voz abierta (r3 == 0).
// sub_821DB0F8 desreferencia r3 + 12 sin comprobar nulo, causando una excepción en guest 0x0000000C.
// Esta guardia evita el crash si r3 es nulo.
SAVE_TRACE_HOOK(sub_821DB0F8) {
  if (ctx.r3.u32 == 0) {
    return;
  }
  orig(ctx, base);
}

// -----------------------------------------------------------------------------
// Sonda: sub_821840B0 (Fundido de Pantalla de Inicio a Pan)
SAVE_TRACE_HOOK(sub_821840B0) {
  static bool logged = false;
  if (!logged) {
    REXLOG_INFO("save_autoload: sub_821840B0 (Title fade to Pan) CALLED from {:08X}",
                static_cast<uint32_t>(ctx.lr));
    logged = true;
  }
  orig(ctx, base);
}

// Sonda: sub_8219BC68 (creación de diálogos, r3=6 es Pan/Cargar partida)
SAVE_TRACE_HOOK(sub_8219BC68) {
  REXLOG_INFO("save_autoload: sub_8219BC68(r3={}, r4={}, r5={}) DIALOG CREATED from caller={:08X}",
              ctx.r3.s32, ctx.r4.s32, ctx.r5.s32, static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_8219BCE0 (polling de estado de diálogo)
SAVE_TRACE_HOOK(sub_8219BCE0) {
  orig(ctx, base);
  static int32_t s_last_val = -999;
  if (ctx.r3.s32 != s_last_val) {
    REXLOG_INFO("save_autoload: sub_8219BCE0 dialog status: {} -> {} (caller={:08X})",
                s_last_val, ctx.r3.s32, static_cast<uint32_t>(ctx.lr));
    s_last_val = ctx.r3.s32;
  }
}

// Sonda: sub_821983E8 (manejador de START en TitleObj)
SAVE_TRACE_HOOK(sub_821983E8) {
  REXLOG_INFO("save_autoload: sub_821983E8 (Title START handler) CALLED from {:08X}",
              static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
  REXLOG_INFO("save_autoload: sub_821983E8 finished (r3={})", ctx.r3.s32);
}

// Sonda: sub_82197020 (transición a Menú Principal)
SAVE_TRACE_HOOK(sub_82197020) {
  REXLOG_INFO("save_autoload: sub_82197020 (MainMenu transition) CALLED from {:08X}",
              static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_821A4848 (ruta alternativa de diálogo)
SAVE_TRACE_HOOK(sub_821A4848) {
  REXLOG_INFO("save_autoload: sub_821A4848 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_821A11E8 (comprobación en sub_821A4848)
SAVE_TRACE_HOOK(sub_821A11E8) {
  REXLOG_INFO("save_autoload: sub_821A11E8 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
  REXLOG_INFO("save_autoload: sub_821A11E8 returned r3={}", ctx.r3.s32);
}

// Sonda: sub_821A39C0 (tarea 2 de diálogo en sub_821A4848)
SAVE_TRACE_HOOK(sub_821A39C0) {
  static uint32_t s_last_task = 0;
  if (ctx.r3.u32 != s_last_task) {
    REXLOG_INFO("save_autoload: sub_821A39C0 task callback CALLED from {:08X}, task={:08X}",
                static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
    s_last_task = ctx.r3.u32;
  }
  orig(ctx, base);
}

// Sonda: sub_821A6AB0
SAVE_TRACE_HOOK(sub_821A6AB0) {
  REXLOG_INFO("save_autoload: sub_821A6AB0 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_821A70B0
SAVE_TRACE_HOOK(sub_821A70B0) {
  REXLOG_INFO("save_autoload: sub_821A70B0 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_82175D60 (diálogo en menú de opciones)
SAVE_TRACE_HOOK(sub_82175D60) {
  REXLOG_INFO("save_autoload: sub_82175D60 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

// Sonda: sub_821717C0 (diálogo en menú de opciones)
SAVE_TRACE_HOOK(sub_821717C0) {
  REXLOG_INFO("save_autoload: sub_821717C0 CALLED from {:08X}", static_cast<uint32_t>(ctx.lr));
  orig(ctx, base);
}

