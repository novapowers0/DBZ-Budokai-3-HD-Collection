// PUL-IW-FX-05: tinte de golpe de IW tal como lo hace el original (EU SLES-55347).
// - s_UTL_CHRFADE EU2EF640 copia 0x1C B {actor, reloj, total, intensidad, 0, color1, color2} a una tarea.
// - Fx_UpdateFadeTimer EU2F2D70: reloj -= fRam49C7DC (1 por cuadro a 60 Hz); si < 0 acaba; si no, actor+18F4 =
//   intensidad * reloj/total, +18F8 = 0, +18FC = color1 (luz), +1900 = color2 (niebla).
// - Minigame_DrawCharacters EU2F4450: k = (+18F4)^2; si k > 0, luz = color1 con peso k (VisualEffects(-1,2)) y
//   niebla = lerp(niebla base EU47DB30, color2, k). Es el CUERPO ENTERO: no hay eleccion por hueso. El hueso solo
//   decide si Piccolo recibe el golpe (Viewer_HitBoneEnterPose3) en EU32B2A0, no el color.
// - Argumentos: QTE (QteMg_PlayHitPoses EU330410, Mg_SetClashStepPoses EU352D40): 15, 15, 0.95, 0, FFFFFFFF, FFFFFFFF
//   (blanco). Piccolo (Viewer_HitSparkUpdate EU32B2A0, tabla EU480C54): 20, 20, 0.95, 0, DC903C00, DC903C00.
// B3 (sub_82109D90) lee los mismos campos en +6260/+6264/+6268/+6272 con k = max(destello, (+6260)^2): PasoFade del
// port ya replica el reloj (0.95*(t-1)/total durante total cuadros); aqui solo se eligen color y duracion.
#pragma once
#include <atomic>
#include <cstdint>
#include <rex/logging.h>

namespace dbz3::iwtinte {
constexpr uint32_t kBlancoQte = 0xFFFFFFFFu;   // uStack_2c..25 = 0xFF en EU330410 / EU352D40
constexpr int kCuadrosQte = 15;                // 0x41700000

inline void Qte(std::atomic<uint32_t> (&paleta)[2], std::atomic<int> (&fade)[2], std::atomic<int>& total, int rol,
                const char* origen) {
  paleta[rol] = kBlancoQte;
  total = kCuadrosQte;
  fade[rol] = kCuadrosQte;
  REXLOG_INFO("dbz3 pulfx05: tinte {} actor {} color {:08X} cuadros {}", origen, rol, kBlancoQte, kCuadrosQte);
}
}  // namespace dbz3::iwtinte
