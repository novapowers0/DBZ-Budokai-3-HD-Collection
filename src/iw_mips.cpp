// SPDX-License-Identifier: MIT
// Port en C++ de tools/iw_mips/ee.py (codigo propio). Misma semantica: el oraculo Python es la referencia.
// No usa ni copia Unicorn, PS2Recomp ni PCSX2 (GPL). El codigo que se ejecuta se lee del ELF del jugador.
#include "iw_mips.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <xmmintrin.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "pasarela.h"
#include "pasarela_iw.h"

// 6/11 20:00: encendida de serie con OK del usuario (INFORME_dia1E 7-11: Bubbles/Freezer/Piccolo/MSN019 = original, EU y USA, 0 vueltas al port).
REXCVAR_DEFINE_BOOL(dbz3_iw_logica_original, true, "DBZ3/IW",
                    "IW: ejecutar la logica original del minijuego (IA de Bubbles) desde el ELF de la ISO del jugador");
// 7/11 tarde: ENCENDIDO de serie con OK del usuario (studies 5/ENTREGAS/IW_FASE1.md). Arreglado el giro de Piccolo
// (+0x50 al empezar el cuadro y la tarea antes de la Update): atrapa a un jugador quieto 9/9 como el original (9/9 en
// PCSX2), tardando de media ~2x mas; en el exe 3913/3913 ticks iguales al espejo. Apagado = el Piccolo propio del port.
// 7/11 21h: de serie APAGADO por decision del usuario (tarda el doble que el original).
REXCVAR_DEFINE_BOOL(dbz3_iw_7dd_original, false, "DBZ3/IW",
                    "IW 0x7DD: Piccolo con su CPU original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
// T2B (7/11): piloto de la familia de botones. Apagado de serie hasta que el usuario lo pruebe.
REXCVAR_DEFINE_BOOL(dbz3_iw_7dc_original, true, "DBZ3/IW",
                    "IW 0x7DC (NB014): carriles, juicio, vida y rango con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");
REXCVAR_DEFINE_BOOL(dbz3_iw_7da_original, true, "DBZ3/IW",
                    "IW 0x7DA (NB012): rondas, iconos y juicio con la logica original del ELF (en pruebas; requiere dbz3_iw_logica_original)");

namespace dbz3::iwmips {
namespace {
constexpr uint64_t M32 = 0xFFFFFFFFull;
constexpr double MAXF = 3.4028234663852886e38, MINF = 1.1754943508222875e-38;
inline uint64_t Sx(uint64_t x) { return uint64_t(int64_t(int32_t(uint32_t(x)))); }

// ---- FPU/VU del R5900: sin inf/NaN (exp 255 = numero), denormal = 0, hacia cero exacto (ee.py fv/fb) ----
double Fv(uint32_t b) {
  const uint32_t e = (b >> 23) & 0xFF;
  if (e == 0) return (b >> 31) ? -0.0 : 0.0;
  if (e == 255) {
    const double v = std::ldexp(1.0 + (b & 0x7FFFFF) / 8388608.0, 128);
    return (b >> 31) ? -v : v;
  }
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
uint32_t Fb(double x, double err = 0, bool cercano = false) {
  const bool neg = x < 0 || (x == 0 && std::signbit(x));
  const double ax = std::fabs(x);
  uint32_t b;
  if (ax >= MAXF) b = 0x7F7FFFFF;
  else if (ax < MINF) b = 0;
  else {
    const float f = float(ax);  // al mas cercano (MXCSR de fabrica, Run)
    std::memcpy(&b, &f, 4);
    const double y = f;
    if (cercano) {
    } else if (y > ax) b -= 1;
    else if (y == ax && err != 0 && ((err < 0) != neg) && (b & 0x7FFFFFFF)) b -= 1;
  }
  return b | (neg ? 0x80000000u : 0);
}
uint32_t Add(double a, double b) {
  const double s = a + b, bb = s - a;
  return Fb(s, (a - (s - bb)) + (b - bb));
}
// PCSX2 FPU_ADD_SUB: el operando de exponente menor pierde los bits que el sumador del R5900 no ve (deja uno de guarda).
void SumaPs2(uint32_t& a, uint32_t& b) {
  const int d = int((a >> 23) & 0xFF) - int((b >> 23) & 0xFF);
  if (d >= 25) b &= 0x80000000u;
  else if (d > 0) b &= 0xFFFFFFFFu << (d - 1);
  else if (d <= -25) a &= 0x80000000u;
  else if (d < 0) a &= 0xFFFFFFFFu << (-d - 1);
}
uint32_t Mul(double a, double b) { return Fb(a * b); }  // producto de dos float32 en double: exacto
uint32_t Div(double a, double b) {
  if (b == 0) return 0x7F7FFFFF | ((std::signbit(a) != std::signbit(b)) ? 0x80000000u : 0);
  const double q = a / b, r = std::fma(-q, b, a);
  return Fb(q, r != 0 ? r / b : 0);
}
uint32_t Sqrt(double a) {
  a = std::fabs(a);
  const double q = std::sqrt(a), r = std::fma(-q, q, a);
  return Fb(q, r);
}
uint32_t Ftoi(uint32_t b, int k) {
  const double x = std::ldexp(Fv(b), k);
  if (x >= 2147483647.0) return 0x7FFFFFFF;
  if (x <= -2147483648.0) return 0x80000000u;
  return uint32_t(int32_t(x));
}
uint32_t Itof(uint32_t b, int k) { return Fb(std::ldexp(double(int32_t(b)), -k)); }

// ---- dia1E6: COP1 con reglas del R5900 medidas con los vectores de HARDWARE de ps2autotests (= ee.py fpu_h;
// tools/iw_mips/fpu_hw.py compila este bloque aparte y lo coteja). de serie kFpuHwSerie (suma, mul, div, sqrt, rsqrt, madd); hw = 0: lo de arriba, como PCSX2. ----
enum FOp : uint8_t { kAdd, kSub, kMul, kDiv, kSqrt, kRsqrt, kMax, kMin, kMadd, kMsub, kCvtWS, kCvtSW, kAbs, kNeg, kMov,
                     kCeq, kClt, kCle };
enum : uint32_t { kHwSuma = 1, kHwMul = 2, kHwDiv = 4, kHwSqrt = 8, kHwRsqrt = 16, kHwMadd = 32, kHwOvf = 64,
                  kHwMaxMin = 128, kHwFlags = 256 };  // mismo orden que ee.FPU_HW_NOMBRES
// De serie las que se ejecutan en los 4 minijuegos y no cambian ningun dorado de PCSX2 (INFORME_dia1E 12).
// Sin ejercitar, apagadas (= PCSX2): kHwOvf, kHwMaxMin, kHwFlags.
constexpr uint32_t kFpuHwSerie = kHwSuma | kHwMul | kHwDiv | kHwSqrt | kHwRsqrt | kHwMadd;
constexpr double P128 = 3.4028236692093846e38;  // 2^128

// 'ovf': exponente 255 es un numero y el desborde da +-0x7FFFFFFF (PCSX2: 0x7F7FFFFF). Banderas: O 0x8010.
uint32_t FbH(double x, double err, bool cercano, uint32_t hw, uint32_t& fcr) {
  const double ax = std::fabs(x);
  if ((hw & kHwOvf) && ax >= MAXF) {
    const uint32_t r = Fb(x / P128, err / P128, cercano);
    if (((r >> 23) & 0xFF) < 128) return r + 0x40000000u;
    fcr |= 0x8010;
    return (r & 0x80000000u) | 0x7FFFFFFFu;
  }
  if (ax >= P128) fcr |= 0x8010;
  return Fb(x, err, cercano);
}
uint32_t SumaH(uint32_t a, uint32_t b, bool guarda, uint32_t hw, uint32_t& fcr) {
  if (guarda) SumaPs2(a, b);
  const double x = Fv(a), y = Fv(b), s = x + y, bb = s - x;
  return FbH(s, (x - (s - bb)) + (y - bb), false, hw, fcr);
}
// 'mul': el multiplicador quita 1 al producto de 48 bits si el bit 1 de la mantisa de ft esta (-1 ulp si es exacto).
bool Booth(uint32_t b, uint32_t hw) { return (hw & kHwMul) && ((b >> 1) & 1) && (b & 0x7F800000); }
int64_t Orden(uint32_t a) { return (a >> 31) ? -int64_t(a & 0x7FFFFFFFu) - 1 : int64_t(a); }  // 'maxmin': -0 < +0
uint32_t FpuH(FOp op, uint32_t a, uint32_t b, uint32_t c, uint32_t hw, bool guarda, uint32_t& fcr) {
  switch (op) {
    case kAdd: case kSub: return SumaH(a, op == kSub ? b ^ 0x80000000u : b, guarda || (hw & kHwSuma), hw, fcr);
    case kMul: {
      const double p = Fv(a) * Fv(b);
      const bool booth = Booth(b, hw);
      const uint32_t r = FbH(p, booth ? -p : 0, false, hw, fcr);
      return booth && !(r & 0x7F800000) ? r & 0x80000000u : r;
    }
    case kDiv: {
      const double x = Fv(a), y = Fv(b);
      if (y == 0) {
        fcr |= x == 0 ? 0x20040 : 0x10020;
        return ((a ^ b) & 0x80000000u) | ((hw & kHwOvf) ? 0x7FFFFFFFu : 0x7F7FFFFFu);
      }
      const double q = x / y;
      if (hw & kHwDiv) return FbH(q, 0, true, hw, fcr);  // 'div': al mas cercano (doble redondeo inocuo)
      const double r = std::fma(-q, y, x);
      return FbH(q, r != 0 ? r / y : 0, false, hw, fcr);
    }
    case kSqrt: {
      if ((a >> 31) && (a & 0x7F800000)) fcr |= 0x20040;
      const double x = std::fabs(Fv(a)), q = std::sqrt(x);
      return (hw & kHwSqrt) ? FbH(q, 0, true, hw, fcr) : FbH(q, std::fma(-q, q, x), false, hw, fcr);
    }
    case kRsqrt: {
      double s;
      if (hw & kHwRsqrt) {
        if ((b >> 31) && (b & 0x7F800000)) fcr |= 0x20040;
        s = Fv(FbH(std::sqrt(std::fabs(Fv(b))), 0, true, hw, fcr));
      } else {
        s = Fv(FpuH(kSqrt, b, 0, 0, hw, false, fcr));
      }
      const double x = Fv(a);
      if (s == 0) {
        fcr |= x == 0 ? 0x20040 : 0x10020;
        return (a & 0x80000000u) | ((hw & kHwOvf) ? 0x7FFFFFFFu : 0x7F7FFFFFu);
      }
      // 'rsqrt': fs * (1/raiz), raiz e inverso al mas cercano, producto truncado
      if (hw & kHwRsqrt) return FbH(x * Fv(FbH(1.0 / s, 0, true, hw, fcr)), 0, false, hw, fcr);
      const double q = x / s, r = std::fma(-q, s, x);
      return FbH(q, r != 0 ? r / s : 0, false, hw, fcr);
    }
    case kMax: case kMin:
      if (hw & kHwMaxMin) return ((Orden(a) >= Orden(b)) == (op == kMax)) ? a : b;  // bits, sin pasar denormales a 0
      return FbH(op == kMax ? std::max(Fv(a), Fv(b)) : std::min(Fv(a), Fv(b)), 0, false, hw, fcr);
    case kMadd: case kMsub: {
      const uint32_t neg = op == kMsub ? 0x80000000u : 0;
      if (!(hw & kHwMadd)) return SumaH(c, FpuH(kMul, a, b, 0, hw, false, fcr) ^ neg, false, hw, fcr);
      // 'madd': producto truncado a 24 bits con exponente libre; la suma alinea con un bit de guarda
      double p = Fv(a) * Fv(b);
      if (p != 0) {
        int e;
        const double m = std::frexp(p, &e);
        double q = std::trunc(m * 16777216.0);
        if (Booth(b, hw) && q == m * 16777216.0) q -= q > 0 ? 1 : -1;
        p = std::ldexp(q, e - 24);
      }
      if (neg) p = -p;
      double x = Fv(c);
      if (x != 0 && p != 0) {
        int ex, ep;
        std::frexp(x, &ex);
        std::frexp(p, &ep);
        const double u = std::ldexp(1.0, std::max(ex, ep) - 25);
        if (ex < ep) x = std::trunc(x / u) * u;
        else if (ep < ex) p = std::trunc(p / u) * u;
      }
      return FbH(x + p, 0, false, hw, fcr);
    }
    default: return 0;
  }
}
// Una operacion COP1 en bits. guarda = suma_ps2 de la llamada (add.s/sub.s); fcr recibe las banderas.
uint32_t Fpu(FOp op, uint32_t a, uint32_t b, uint32_t c, uint32_t hw, bool guarda, uint32_t& fcr) {
  switch (op) {
    case kCvtSW: return Fb(double(int32_t(a)));
    case kCvtWS: return (a & 0x7F800000) >= 0x4F000000 ? ((a >> 31) ? 0x80000000u : 0x7FFFFFFFu) : uint32_t(int32_t(Fv(a)));
    case kAbs: return a & 0x7FFFFFFF;
    case kNeg: return a ^ 0x80000000u;
    case kMov: return a;
    default: break;
  }
  if (hw) return FpuH(op, a, b, c, hw, guarda, fcr);
  switch (op) {
    case kAdd: case kSub:
      if (guarda) SumaPs2(a, b);
      return Add(Fv(a), op == kAdd ? Fv(b) : -Fv(b));
    case kMul: return Mul(Fv(a), Fv(b));
    case kDiv: return Div(Fv(a), Fv(b));
    case kSqrt: return Sqrt(Fv(a));
    case kRsqrt: return Div(Fv(a), Fv(Sqrt(Fv(b))));
    case kMax: return Fb(std::max(Fv(a), Fv(b)));
    case kMin: return Fb(std::min(Fv(a), Fv(b)));
    case kMadd: return Add(Fv(c), Fv(Mul(Fv(a), Fv(b))));
    case kMsub: return Add(Fv(c), -Fv(Mul(Fv(a), Fv(b))));
    default: return 0;
  }
}
bool FpuCmp(FOp op, uint32_t a, uint32_t b, uint32_t) {
  const double x = Fv(a), y = Fv(b);
  return op == kCeq ? x == y : op == kClt ? x < y : x <= y;
}
// ---- fin FPU ----

// Reglas activas: de serie kFpuHwSerie; DBZ3_TEST_FPU_HW=div,ovf,... solo en pruebas (tools/pruebas.py), nunca en el juego.
uint32_t FpuHwActivas() {
  static const uint32_t hw = [] {
    static constexpr const char* kNom[] = {"suma", "mul", "div", "sqrt", "rsqrt", "madd", "ovf", "maxmin", "flags"};
    uint32_t m = kFpuHwSerie;
    if (const char* e = std::getenv("DBZ3_TEST_FPU_HW")) {
      const std::string s = std::string(e) + ',';
      for (size_t i = 0, j; (j = s.find_first_of(", ", i)) != std::string::npos; i = j + 1) {
        std::string t = s.substr(i, j - i);
        if (t.rfind("fpu_hw_", 0) == 0) t = t.substr(7);
        for (int k = 0; k < 9; ++k)
          if (t == kNom[k]) m |= 1u << k;
      }
    }
    return m;
  }();
  return hw;
}

enum Op : uint16_t {
  NADA, NOP, LUI, ADDIU, DADDIU, ANDI, ORI, XORI, SLTI, SLTIU,
  ADDU, SUBU, DADDU, DSUBU, AND, OR, XOR, NOR, SLT, SLTU, MOVZ, MOVN,
  SLL, SRL, SRA, SLLV, SRLV, SRAV, DSLL, DSLL32, DSRL, DSRL32, DSRA, DSRA32, DSLLV, DSRLV, DSRAV,
  MULT, MULTU, MULT1, MULTU1, MADD, MADDU, MADD1, MADDU1, DIV, DIVU, DIV1, DIVU1,
  MFLO, MFHI, MFLO1, MFHI1, MTLO, MTHI, MTLO1, MTHI1,
  LB, LBU, LH, LHU, LW, LWU, LD, LQ, SB, SH, SW, SD, SQ, LWL, LWR, SWL, SWR, LDL, LDR, SDL, SDR,
  BEQ, BNE, BLEZ, BGTZ, BEQL, BNEL, BLEZL, BGTZL, BLTZ, BGEZ, BLTZL, BGEZL, BLTZAL, BGEZAL,
  J, JAL, JR, JALR, MFC0,
  PCPYLD, PCPYUD, PCPYH, POR, PAND, PXOR, PNOR, PEXTLW, PLZCW,
  MFC1, MTC1, CFC1, CTC1, LWC1, SWC1,
  ADDS, SUBS, MULS, DIVS, SQRTS, RSQRTS, ABSS, NEGS, MOVS, MAXS, MINS, ADDAS, SUBAS, MULAS,
  MADDS, MSUBS, MADDAS, MSUBAS, CVTSW, CVTWS, CF, CEQ, CLT, CLE, BC1F, BC1T, BC1FL, BC1TL,
  LQC2, SQC2, QMFC2, QMTC2, CFC2, CTC2,
  VADD, VSUB, VMUL, VMADD, VMSUB, VMOVE, VMR32, VABS, VOPMULA, VOPMSUB, VDIV, VSQRT, VRSQRT, VFTOI, VITOF,
};
// VU: Ins.sa = 0-3 difusion x/y/z/w, 4 = Q, 5 = vector; Ins.simm = 1 si el destino es ACC.
uint16_t DecodificarOp(uint32_t w, uint8_t& sa, int32_t& extra) {
  const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, fn = w & 63, s5 = (w >> 6) & 31;
  extra = 0;
  switch (op) {
    case 0x00:
      if (w == 0) return NOP;
      switch (fn) {
        case 0x00: return SLL; case 0x02: return SRL; case 0x03: return SRA; case 0x04: return SLLV;
        case 0x06: return SRLV; case 0x07: return SRAV; case 0x08: return JR; case 0x09: return JALR;
        case 0x0A: return MOVZ; case 0x0B: return MOVN; case 0x0F: return NOP;
        case 0x10: return MFHI; case 0x11: return MTHI; case 0x12: return MFLO; case 0x13: return MTLO;
        case 0x14: return DSLLV; case 0x16: return DSRLV; case 0x17: return DSRAV;
        case 0x18: return MULT; case 0x19: return MULTU; case 0x1A: return DIV; case 0x1B: return DIVU;
        case 0x20: case 0x21: return ADDU; case 0x22: case 0x23: return SUBU;
        case 0x24: return AND; case 0x25: return OR; case 0x26: return XOR; case 0x27: return NOR;
        case 0x2A: return SLT; case 0x2B: return SLTU; case 0x2C: case 0x2D: return DADDU;
        case 0x2E: case 0x2F: return DSUBU; case 0x38: return DSLL; case 0x3A: return DSRL; case 0x3B: return DSRA;
        case 0x3C: return DSLL32; case 0x3E: return DSRL32; case 0x3F: return DSRA32;
      }
      return NADA;  // syscall/break/mfsa...: fuera del subconjunto
    case 0x01:
      switch (rt) {
        case 0x00: return BLTZ; case 0x01: return BGEZ; case 0x02: return BLTZL; case 0x03: return BGEZL;
        case 0x10: return BLTZAL; case 0x11: return BGEZAL;
      }
      return NADA;
    case 0x02: return J; case 0x03: return JAL; case 0x04: return BEQ; case 0x05: return BNE;
    case 0x06: return BLEZ; case 0x07: return BGTZ; case 0x08: case 0x09: return ADDIU;
    case 0x0A: return SLTI; case 0x0B: return SLTIU; case 0x0C: return ANDI; case 0x0D: return ORI;
    case 0x0E: return XORI; case 0x0F: return LUI;
    case 0x10:
      if (rs == 0) return MFC0;
      if (rs == 4) return NOP;
      if (rs == 0x10 && (fn == 0x38 || fn == 0x39)) return NOP;  // ei/di
      return NADA;
    case 0x11:
      switch (rs) {
        case 0x00: return MFC1; case 0x02: return CFC1; case 0x04: return MTC1; case 0x06: return CTC1;
        case 0x08: return rt == 0 ? BC1F : rt == 1 ? BC1T : rt == 2 ? BC1FL : rt == 3 ? BC1TL : NADA;
        case 0x14: return fn == 0x20 ? CVTSW : NADA;
        case 0x10:
          switch (fn) {
            case 0x00: return ADDS; case 0x01: return SUBS; case 0x02: return MULS; case 0x03: return DIVS;
            case 0x04: return SQRTS; case 0x05: return ABSS; case 0x06: return MOVS; case 0x07: return NEGS;
            case 0x16: return RSQRTS; case 0x18: return ADDAS; case 0x19: return SUBAS; case 0x1A: return MULAS;
            case 0x1C: return MADDS; case 0x1D: return MSUBS; case 0x1E: return MADDAS; case 0x1F: return MSUBAS;
            case 0x24: return CVTWS; case 0x28: return MAXS; case 0x29: return MINS;
            case 0x30: return CF; case 0x32: return CEQ; case 0x34: return CLT; case 0x36: return CLE;
          }
      }
      return NADA;
    case 0x12:
      if (!((w >> 25) & 1)) {
        switch (rs) { case 0x01: return QMFC2; case 0x02: return CFC2; case 0x05: return QMTC2; case 0x06: return CTC2; }
        return NADA;
      }
      if (fn < 0x3C) {
        sa = uint8_t(fn & 3);
        switch (fn >> 2) { case 0: return VADD; case 1: return VSUB; case 2: return VMADD; case 3: return VMSUB; case 6: return VMUL; }
        sa = 4;
        switch (fn) { case 0x1C: return VMUL; case 0x20: return VADD; case 0x21: return VMADD; case 0x24: return VSUB; case 0x25: return VMSUB; }
        sa = 5;
        switch (fn) { case 0x28: return VADD; case 0x29: return VMADD; case 0x2A: return VMUL; case 0x2C: return VSUB; case 0x2D: return VMSUB; case 0x2E: return VOPMSUB; }
        return NADA;
      } else {
        const uint32_t o2 = (w & 3) | ((w >> 4) & 0x7C);
        extra = 1;  // destino ACC
        sa = uint8_t(o2 & 3);
        switch (o2 >> 2) { case 0: return VADD; case 1: return VSUB; case 2: return VMADD; case 3: return VMSUB; case 6: return VMUL; }
        static constexpr uint8_t kK[4] = {0, 4, 12, 15};
        extra = 0;
        if ((o2 >> 2) == 4) { sa = kK[o2 & 3]; return VITOF; }
        if ((o2 >> 2) == 5) { sa = kK[o2 & 3]; return VFTOI; }
        extra = 1;
        sa = 4;
        switch (o2) { case 0x1C: return VMUL; case 0x20: return VADD; case 0x21: return VMADD; case 0x24: return VSUB; case 0x25: return VMSUB; }
        sa = 5;
        switch (o2) { case 0x28: return VADD; case 0x29: return VMADD; case 0x2A: return VMUL; case 0x2C: return VSUB; case 0x2D: return VMSUB; case 0x2E: return VOPMULA; }
        extra = 0;
        switch (o2) {
          case 0x1D: return VABS; case 0x2F: case 0x3B: return NOP; case 0x30: return VMOVE; case 0x31: return VMR32;
          case 0x38: return VDIV; case 0x39: return VSQRT; case 0x3A: return VRSQRT;
        }
        return NADA;
      }
    case 0x14: return BEQL; case 0x15: return BNEL; case 0x16: return BLEZL; case 0x17: return BGTZL;
    case 0x18: case 0x19: return DADDIU; case 0x1A: return LDL; case 0x1B: return LDR;
    case 0x1C:
      switch (fn) {
        case 0x00: return MADD; case 0x01: return MADDU; case 0x04: return PLZCW;
        case 0x08: return s5 == 0x12 ? PEXTLW : NADA;
        case 0x09: return s5 == 0x0E ? PCPYLD : s5 == 0x12 ? PAND : s5 == 0x13 ? PXOR : NADA;
        case 0x10: return MFHI1; case 0x11: return MTHI1; case 0x12: return MFLO1; case 0x13: return MTLO1;
        case 0x18: return MULT1; case 0x19: return MULTU1; case 0x1A: return DIV1; case 0x1B: return DIVU1;
        case 0x20: return MADD1; case 0x21: return MADDU1;
        case 0x29: return s5 == 0x0E ? PCPYUD : s5 == 0x12 ? POR : s5 == 0x13 ? PNOR : s5 == 0x1B ? PCPYH : NADA;
      }
      return NADA;
    case 0x1E: return LQ; case 0x1F: return SQ; case 0x20: return LB; case 0x21: return LH; case 0x22: return LWL;
    case 0x23: return LW; case 0x24: return LBU; case 0x25: return LHU; case 0x26: return LWR; case 0x27: return LWU;
    case 0x28: return SB; case 0x29: return SH; case 0x2A: return SWL; case 0x2B: return SW; case 0x2C: return SDL;
    case 0x2D: return SDR; case 0x2E: return SWR; case 0x2F: case 0x33: return NOP; case 0x31: return LWC1;
    case 0x36: return LQC2; case 0x37: return LD; case 0x39: return SWC1; case 0x3E: return SQC2; case 0x3F: return SD;
  }
  return NADA;
}
}  // namespace

int64_t Ee::Fis(uint32_t a) {
  if (a < kRam) return a;
  if (a >= 0x70000000u && a < 0x70004000u) return kSpr + (a - 0x70000000u);
  if (a >= 0x80000000u || (a >= 0x20000000u && a < 0x40000000u)) {
    const uint32_t b = a & 0x1FFFFFFF;
    if (b < kRam) return b;
  }
  if (a >= kPila && a < kSpr) return a;
  return -1;
}
template <class T> T Ee::Lee(uint32_t a) {
  const int64_t p = Fis(a);
  T v{};
  if (p < 0 || size_t(p) + sizeof(T) > mem_.size()) { ++io; return v; }
  std::memcpy(&v, mem_.data() + p, sizeof(T));
  return v;
}
template <class T> void Ee::Escribe(uint32_t a, T v) {
  const int64_t p = Fis(a);
  if (p < 0 || size_t(p) + sizeof(T) > mem_.size()) { ++io; return; }
  std::memcpy(mem_.data() + p, &v, sizeof(T));
}
uint32_t Ee::R32(uint32_t a) { return Lee<uint32_t>(a); }
float Ee::Rf(uint32_t a) { const uint32_t b = R32(a); float f; std::memcpy(&f, &b, 4); return f; }
void Ee::W32(uint32_t a, uint32_t v) { Escribe<uint32_t>(a, v); }
void Ee::W16(uint32_t a, uint16_t v) { Escribe<uint16_t>(a, v); }
void Ee::Wf(uint32_t a, float v) { uint32_t b; std::memcpy(&b, &v, 4); W32(a, b); }
void Ee::Cero(uint32_t a, uint32_t n) {
  const int64_t p = Fis(a);
  if (p >= 0 && size_t(p) + n <= mem_.size()) std::memset(mem_.data() + p, 0, n);
}

bool Ee::Preparar(uint32_t texto0, uint32_t texto1) {
  mem_.assign(kTotal, 0);
  t0_ = texto0; t1_ = texto1;
  code_.assign((texto1 - texto0) / 4, Ins{});
  vf_[0][3] = 0x3F800000;
  return true;
}

bool Ee::Cargar(const std::vector<uint8_t>& elf, uint32_t texto0, uint32_t texto1) {
  auto u32 = [&](size_t p) { uint32_t v = 0; if (p + 4 <= elf.size()) std::memcpy(&v, elf.data() + p, 4); return v; };
  if (elf.size() < 52 || u32(0) != 0x464C457F) return Fallo("ELF no valido", 0);
  Preparar(texto0, texto1);
  const uint32_t ph = u32(28), pn = u32(44) & 0xFFFF;
  for (uint32_t i = 0; i < pn; ++i) {
    const size_t p = ph + 32ull * i;
    const uint32_t tipo = u32(p), off = u32(p + 4), va = u32(p + 8), fsz = u32(p + 16);
    if (tipo != 1 || !fsz) continue;
    if (size_t(off) + fsz > elf.size() || size_t(va) + fsz > kRam) return Fallo("PT_LOAD fuera de rango", va);
    std::memcpy(mem_.data() + va, elf.data() + off, fsz);  // bss (memsz > filesz) queda a cero
  }
  return true;
}

uint64_t Ee::HashTexto() const {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint32_t a = t0_; a < t1_; ++a) h = (h ^ mem_[a]) * 0x100000001b3ull;
  return h;
}

bool Ee::Fallo(const char* que, uint32_t pc) {
  char b[128];
  std::snprintf(b, sizeof b, "%s en %08X", que, pc);
  error_ = b;
  return false;
}

const Ee::Ins* Ee::Decodificar(uint32_t pc) {
  if (pc < t0_ || pc >= t1_ || (pc & 3)) return nullptr;
  Ins& in = code_[(pc - t0_) / 4];
  if (in.op) return &in;
  const uint32_t w = R32(pc);
  in.w = w; in.rs = (w >> 21) & 31; in.rt = (w >> 16) & 31; in.rd = (w >> 11) & 31; in.sa = (w >> 6) & 31;
  int32_t extra = 0;
  const uint16_t op = DecodificarOp(w, in.sa, extra);
  if (!op) return nullptr;
  in.simm = int16_t(w & 0xFFFF);
  if (op >= VADD && op <= VMSUB) in.simm = extra;
  in.t = op == J || op == JAL ? (((pc + 4) & 0xF0000000u) | ((w & 0x3FFFFFF) << 2)) : uint32_t(pc + 4 + (int32_t(int16_t(w & 0xFFFF)) * 4));
  // Cargas a r0: nop (como ee.py, sin tocar memoria).
  if (in.rt == 0 && (op == LB || op == LBU || op == LH || op == LHU || op == LW || op == LWU || op == LD || op == LQ ||
                     op == LWL || op == LWR || op == LDL || op == LDR)) in.op = NOP;
  else in.op = op;
  return &in;
}

bool Ee::Llamar(uint32_t dir, std::initializer_list<uint32_t> args, uint64_t limite) {
  int k = 4;
  for (uint32_t a : args) r[k++] = Sx(a);
  r[28] = Sx(gp); r[29] = kPila + 0xFF000; r[31] = kRetorno;
  error_.clear(); io = 0;
  // El hilo del juego puede llevar otro MXCSR (redondeo/FTZ del runtime PPC): las cuentas double van en el de fabrica.
  struct Csr { unsigned c = _mm_getcsr(); Csr() { _mm_setcsr(0x1F80); } ~Csr() { _mm_setcsr(c); } } csr;
  const uint32_t hw = FpuHwActivas();  // dia1E6: reglas de hardware de la FPU (de serie kFpuHwSerie, 6 reglas)
  auto entrar = [&](uint32_t t) -> uint32_t {
    if (auto it = puentes.find(t); it != puentes.end()) { it->second(*this); return uint32_t(r[31]); }
    if (auto it = observadores.find(t); it != observadores.end()) it->second(*this);
    return t;
  };
  uint32_t pc = dir, dt = 0;
  bool hay_dt = false, dt_llamada = false;
  uint64_t cuenta = 0;
  auto vu = [&](const Ins& in, auto op) {  // op(a,b) -> bits; dest x/y/z/w = bits 24..21
    uint32_t* a = vf_[in.rd];
    uint32_t b[4];
    for (int i = 0; i < 4; ++i) b[i] = in.sa == 4 ? q_ : in.sa == 5 ? vf_[in.rt][i] : vf_[in.rt][in.sa];
    uint32_t out[4];
    std::memcpy(out, in.simm ? vacc_ : vf_[(in.w >> 6) & 31], 16);
    for (int i = 0; i < 4; ++i)
      if ((in.w >> (24 - i)) & 1) out[i] = op(i, Fv(a[i]), Fv(b[i]));
    if (in.simm) std::memcpy(vacc_, out, 16);
    else if ((in.w >> 6) & 31) std::memcpy(vf_[(in.w >> 6) & 31], out, 16);
  };
  for (;;) {
    const Ins* ip = Decodificar(pc);
    if (!ip) return Fallo(pc < t0_ || pc >= t1_ ? "pc fuera del texto" : "instruccion fuera del subconjunto", pc);
    const Ins& in = *ip;
    const uint32_t rs = in.rs, rt = in.rt, rd = in.rd, sa = in.sa;
    const int64_t simm = in.simm;
    uint64_t* R = r;
    auto ea = [&] { return uint32_t(R[rs] + uint64_t(simm)); };
    const uint32_t fs = rd, ft = rt, fd = sa;  // campos COP1 (sa = (w>>6)&31 salvo en VU)
    int res = 0;  // 0 sigue, 1 likely no tomado (anula hueco), 2 salto a `dest`
    uint32_t dest = 0;
    bool llama = false;
    auto br = [&](bool c, bool likely, bool link = false) {
      if (link) R[31] = pc + 8;
      if (c) { res = 2; dest = in.t; } else if (likely) res = 1;
    };
    switch (in.op) {
      case NOP: break;
      case LUI: R[rt] = Sx(uint32_t(in.w << 16)); break;
      case ADDIU: R[rt] = Sx(R[rs] + uint64_t(simm)); break;
      case DADDIU: R[rt] = R[rs] + uint64_t(simm); break;
      case ANDI: R[rt] = R[rs] & (in.w & 0xFFFF); break;
      case ORI: R[rt] = R[rs] | (in.w & 0xFFFF); break;
      case XORI: R[rt] = R[rs] ^ (in.w & 0xFFFF); break;
      case SLTI: R[rt] = int64_t(R[rs]) < simm; break;
      case SLTIU: R[rt] = R[rs] < uint64_t(simm); break;
      case ADDU: R[rd] = Sx(R[rs] + R[rt]); break;
      case SUBU: R[rd] = Sx(R[rs] - R[rt]); break;
      case DADDU: R[rd] = R[rs] + R[rt]; break;
      case DSUBU: R[rd] = R[rs] - R[rt]; break;
      case AND: R[rd] = R[rs] & R[rt]; break;
      case OR: R[rd] = R[rs] | R[rt]; break;
      case XOR: R[rd] = R[rs] ^ R[rt]; break;
      case NOR: R[rd] = ~(R[rs] | R[rt]); break;
      case SLT: R[rd] = int64_t(R[rs]) < int64_t(R[rt]); break;
      case SLTU: R[rd] = R[rs] < R[rt]; break;
      case MOVZ: if (R[rt] == 0) R[rd] = R[rs]; break;
      case MOVN: if (R[rt] != 0) R[rd] = R[rs]; break;
      case SLL: R[rd] = Sx(uint32_t(R[rt]) << sa); break;
      case SRL: R[rd] = Sx(uint32_t(R[rt]) >> sa); break;
      case SRA: R[rd] = Sx(uint32_t(int32_t(R[rt]) >> sa)); break;
      case SLLV: R[rd] = Sx(uint32_t(R[rt]) << (R[rs] & 31)); break;
      case SRLV: R[rd] = Sx(uint32_t(R[rt]) >> (R[rs] & 31)); break;
      case SRAV: R[rd] = Sx(uint32_t(int32_t(R[rt]) >> (R[rs] & 31))); break;
      case DSLL: R[rd] = R[rt] << sa; break;
      case DSLL32: R[rd] = R[rt] << (sa + 32); break;
      case DSRL: R[rd] = R[rt] >> sa; break;
      case DSRL32: R[rd] = R[rt] >> (sa + 32); break;
      case DSRA: R[rd] = uint64_t(int64_t(R[rt]) >> sa); break;
      case DSRA32: R[rd] = uint64_t(int64_t(R[rt]) >> (sa + 32)); break;
      case DSLLV: R[rd] = R[rt] << (R[rs] & 63); break;
      case DSRLV: R[rd] = R[rt] >> (R[rs] & 63); break;
      case DSRAV: R[rd] = uint64_t(int64_t(R[rt]) >> (R[rs] & 63)); break;
      case MULT: case MULTU: case MULT1: case MULTU1: case MADD: case MADDU: case MADD1: case MADDU1: {
        const bool uns = in.op == MULTU || in.op == MULTU1 || in.op == MADDU || in.op == MADDU1;
        const bool p1 = in.op == MULT1 || in.op == MULTU1 || in.op == MADD1 || in.op == MADDU1;
        const bool acum = in.op == MADD || in.op == MADDU || in.op == MADD1 || in.op == MADDU1;
        uint64_t& LO = p1 ? lo1_ : lo_;
        uint64_t& HI = p1 ? hi1_ : hi_;
        uint64_t p = uns ? (R[rs] & M32) * (R[rt] & M32) : uint64_t(int64_t(int32_t(R[rs])) * int64_t(int32_t(R[rt])));
        if (acum) p += ((HI & M32) << 32) | (LO & M32);
        LO = Sx(p); HI = Sx(p >> 32);
        if (rd) R[rd] = LO;
        break;
      }
      case DIV: case DIVU: case DIV1: case DIVU1: {
        const bool p1 = in.op == DIV1 || in.op == DIVU1;
        int64_t qv, mv;
        if (in.op == DIVU || in.op == DIVU1) {
          const uint64_t a = R[rs] & M32, b = R[rt] & M32;
          if (b == 0) { qv = int64_t(M32); mv = int64_t(a); } else { qv = int64_t(a / b); mv = int64_t(a % b); }
        } else {
          const int64_t a = int32_t(R[rs]), b = int32_t(R[rt]);
          if (b == 0) { qv = a < 0 ? 1 : -1; mv = a; }
          else if (a == -0x80000000ll && b == -1) { qv = a; mv = 0; }
          else { qv = a / b; mv = a - qv * b; }
        }
        (p1 ? lo1_ : lo_) = Sx(uint64_t(qv));
        (p1 ? hi1_ : hi_) = Sx(uint64_t(mv));
        break;
      }
      case MFLO: R[rd] = lo_; break;
      case MFHI: R[rd] = hi_; break;
      case MFLO1: R[rd] = lo1_; break;
      case MFHI1: R[rd] = hi1_; break;
      case MTLO: lo_ = R[rs]; break;
      case MTHI: hi_ = R[rs]; break;
      case MTLO1: lo1_ = R[rs]; break;
      case MTHI1: hi1_ = R[rs]; break;
      case LB: R[rt] = uint64_t(int64_t(int8_t(Lee<uint8_t>(ea())))); break;
      case LBU: R[rt] = Lee<uint8_t>(ea()); break;
      case LH: R[rt] = uint64_t(int64_t(int16_t(Lee<uint16_t>(ea())))); break;
      case LHU: R[rt] = Lee<uint16_t>(ea()); break;
      case LW: R[rt] = Sx(Lee<uint32_t>(ea())); break;
      case LWU: R[rt] = Lee<uint32_t>(ea()); break;
      case LD: R[rt] = Lee<uint64_t>(ea()); break;
      case LQ: { const uint32_t a = ea() & 0xFFFFFFF0u; R[rt] = Lee<uint64_t>(a); up[rt] = Lee<uint64_t>(a + 8); break; }
      case SB: Escribe<uint8_t>(ea(), uint8_t(R[rt])); break;
      case SH: Escribe<uint16_t>(ea(), uint16_t(R[rt])); break;
      case SW: Escribe<uint32_t>(ea(), uint32_t(R[rt])); break;
      case SD: Escribe<uint64_t>(ea(), R[rt]); break;
      case SQ: { const uint32_t a = ea() & 0xFFFFFFF0u; Escribe<uint64_t>(a, R[rt]); Escribe<uint64_t>(a + 8, up[rt]); break; }
      case LWL: {
        const uint32_t a = ea(), kk = a & 3, m = Lee<uint32_t>(a & ~3u), sh = (3 - kk) * 8;
        const uint32_t keep = uint32_t((1ull << sh) - 1);
        R[rt] = Sx((uint32_t(R[rt]) & keep) | uint32_t(uint64_t(m) << sh));
        break;
      }
      case LWR: {
        const uint32_t a = ea(), kk = a & 3, m = Lee<uint32_t>(a & ~3u), sh = kk * 8;
        const uint32_t keep = sh ? uint32_t(M32 << (32 - sh)) : 0;
        const uint32_t v = (uint32_t(R[rt]) & keep) | (m >> sh);
        R[rt] = kk == 0 ? Sx(v) : ((R[rt] & 0xFFFFFFFF00000000ull) | v);
        break;
      }
      case SWL: {
        const uint32_t a = ea(), kk = a & 3, m = Lee<uint32_t>(a & ~3u), v = uint32_t(R[rt]), sh = (3 - kk) * 8;
        const uint32_t keep = kk < 3 ? uint32_t(M32 << (8 * (kk + 1))) : 0;
        Escribe<uint32_t>(a & ~3u, (m & keep) | (v >> sh));
        break;
      }
      case SWR: {
        const uint32_t a = ea(), kk = a & 3, m = Lee<uint32_t>(a & ~3u), v = uint32_t(R[rt]), sh = kk * 8;
        const uint32_t keep = uint32_t((1ull << sh) - 1);
        Escribe<uint32_t>(a & ~3u, (m & keep) | uint32_t(uint64_t(v) << sh));
        break;
      }
      case LDL: {
        const uint32_t a = ea(), kk = a & 7, sh = (7 - kk) * 8;
        const uint64_t m = Lee<uint64_t>(a & ~7u), keep = (1ull << sh) - 1;
        R[rt] = (R[rt] & keep) | (m << sh);
        break;
      }
      case LDR: {
        const uint32_t a = ea(), kk = a & 7, sh = kk * 8;
        const uint64_t m = Lee<uint64_t>(a & ~7u), keep = sh ? ~0ull << (64 - sh) : 0;
        R[rt] = (R[rt] & keep) | (m >> sh);
        break;
      }
      case SDL: {
        const uint32_t a = ea(), kk = a & 7;
        const uint64_t m = Lee<uint64_t>(a & ~7u), keep = kk < 7 ? ~0ull << (8 * (kk + 1)) : 0;
        Escribe<uint64_t>(a & ~7u, (m & keep) | (R[rt] >> ((7 - kk) * 8)));
        break;
      }
      case SDR: {
        const uint32_t a = ea(), kk = a & 7, sh = kk * 8;
        const uint64_t m = Lee<uint64_t>(a & ~7u);
        Escribe<uint64_t>(a & ~7u, (m & ((1ull << sh) - 1)) | (R[rt] << sh));
        break;
      }
      case BEQ: br(R[rs] == R[rt], false); break;
      case BNE: br(R[rs] != R[rt], false); break;
      case BEQL: br(R[rs] == R[rt], true); break;
      case BNEL: br(R[rs] != R[rt], true); break;
      case BLEZ: br(int64_t(R[rs]) <= 0, false); break;
      case BGTZ: br(int64_t(R[rs]) > 0, false); break;
      case BLEZL: br(int64_t(R[rs]) <= 0, true); break;
      case BGTZL: br(int64_t(R[rs]) > 0, true); break;
      case BLTZ: br(int64_t(R[rs]) < 0, false); break;
      case BGEZ: br(int64_t(R[rs]) >= 0, false); break;
      case BLTZL: br(int64_t(R[rs]) < 0, true); break;
      case BGEZL: br(int64_t(R[rs]) >= 0, true); break;
      case BLTZAL: br(int64_t(R[rs]) < 0, false, true); break;
      case BGEZAL: br(int64_t(R[rs]) >= 0, false, true); break;
      case J: res = 2; dest = in.t; llama = puentes.count(in.t) || observadores.count(in.t); break;  // salto de cola
      case JAL: R[31] = pc + 8; res = 2; dest = in.t; llama = true; break;
      case JR: res = 2; dest = uint32_t(R[rs]); break;
      case JALR: res = 2; dest = uint32_t(R[rs]); if (rd) R[rd] = pc + 8; llama = true; break;
      case MFC0: R[rt] = rd == 9 ? Sx(uint32_t(n + cuenta)) : 0; break;
      case PCPYLD: { const uint64_t a = R[rs], b = R[rt]; up[rd] = a; R[rd] = b; break; }
      case PCPYUD: { const uint64_t a = up[rs], b = up[rt]; R[rd] = a; up[rd] = b; break; }
      case PCPYH: { const uint64_t a = R[rt] & 0xFFFF, b = up[rt] & 0xFFFF; R[rd] = a * 0x0001000100010001ull; up[rd] = b * 0x0001000100010001ull; break; }
      case POR: { const uint64_t a = R[rs] | R[rt], b = up[rs] | up[rt]; R[rd] = a; up[rd] = b; break; }
      case PAND: { const uint64_t a = R[rs] & R[rt], b = up[rs] & up[rt]; R[rd] = a; up[rd] = b; break; }
      case PXOR: { const uint64_t a = R[rs] ^ R[rt], b = up[rs] ^ up[rt]; R[rd] = a; up[rd] = b; break; }
      case PNOR: { const uint64_t a = ~(R[rs] | R[rt]), b = ~(up[rs] | up[rt]); R[rd] = a; up[rd] = b; break; }
      case PEXTLW: {
        const uint64_t a = R[rs], b = R[rt];
        R[rd] = (b & M32) | ((a & M32) << 32); up[rd] = (b >> 32) | ((a >> 32) << 32);
        break;
      }
      case PLZCW: {
        auto lz = [](uint32_t x) { uint64_t c = 0; const uint32_t s = x >> 31; for (int i = 30; i >= 0 && ((x >> i) & 1) == s; --i) ++c; return c; };
        R[rd] = lz(uint32_t(R[rs])) | (lz(uint32_t(R[rs] >> 32)) << 32);
        break;
      }
      case MFC1: R[rt] = Sx(f_[fs]); break;
      case MTC1: f_[fs] = uint32_t(R[rt]); break;
      case CFC1: R[rt] = fs == 31 ? (0x01000001u | (c_ ? 0x800000u : 0) | ((hw & kHwFlags) ? fcr_ : 0)) : 0x2E30u; break;
      case CTC1: if (fs == 31) { c_ = (R[rt] & 0x800000) != 0; fcr_ = uint32_t(R[rt]) & 0x3C078; } break;
      case LWC1: f_[ft] = Lee<uint32_t>(ea()); break;
      case SWC1: Escribe<uint32_t>(ea(), f_[ft]); break;
      // dia1E6: todo el COP1 por Fpu() (hw = 0: la de PCSX2; de serie kFpuHwSerie)
      case ADDS: f_[fd] = Fpu(kAdd, f_[fs], f_[ft], 0, hw, suma_ps2, fcr_); break;
      case SUBS: f_[fd] = Fpu(kSub, f_[fs], f_[ft], 0, hw, suma_ps2, fcr_); break;
      case MULS: f_[fd] = Fpu(kMul, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case DIVS: f_[fd] = Fpu(kDiv, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case SQRTS: f_[fd] = Fpu(kSqrt, f_[ft], 0, 0, hw, false, fcr_); break;
      case RSQRTS: f_[fd] = Fpu(kRsqrt, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case ABSS: f_[fd] = Fpu(kAbs, f_[fs], 0, 0, hw, false, fcr_); break;
      case NEGS: f_[fd] = Fpu(kNeg, f_[fs], 0, 0, hw, false, fcr_); break;
      case MOVS: f_[fd] = Fpu(kMov, f_[fs], 0, 0, hw, false, fcr_); break;
      case MAXS: f_[fd] = Fpu(kMax, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case MINS: f_[fd] = Fpu(kMin, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case ADDAS: acc_ = Fpu(kAdd, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case SUBAS: acc_ = Fpu(kSub, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case MULAS: acc_ = Fpu(kMul, f_[fs], f_[ft], 0, hw, false, fcr_); break;
      case MADDS: f_[fd] = Fpu(kMadd, f_[fs], f_[ft], acc_, hw, false, fcr_); break;
      case MSUBS: f_[fd] = Fpu(kMsub, f_[fs], f_[ft], acc_, hw, false, fcr_); break;
      case MADDAS: acc_ = Fpu(kMadd, f_[fs], f_[ft], acc_, hw, false, fcr_); break;
      case MSUBAS: acc_ = Fpu(kMsub, f_[fs], f_[ft], acc_, hw, false, fcr_); break;
      case CVTSW: f_[fd] = Fpu(kCvtSW, f_[fs], 0, 0, hw, false, fcr_); break;
      case CVTWS: f_[fd] = Fpu(kCvtWS, f_[fs], 0, 0, hw, false, fcr_); break;
      case CF: c_ = false; break;
      case CEQ: c_ = FpuCmp(kCeq, f_[fs], f_[ft], hw); break;
      case CLT: c_ = FpuCmp(kClt, f_[fs], f_[ft], hw); break;
      case CLE: c_ = FpuCmp(kCle, f_[fs], f_[ft], hw); break;
      case BC1F: br(!c_, false); break;
      case BC1T: br(c_, false); break;
      case BC1FL: br(!c_, true); break;
      case BC1TL: br(c_, true); break;
      case LQC2: { const uint32_t a = ea() & 0xFFFFFFF0u; if (rt) for (int i = 0; i < 4; ++i) vf_[rt][i] = Lee<uint32_t>(a + 4 * i); break; }
      case SQC2: { const uint32_t a = ea() & 0xFFFFFFF0u; for (int i = 0; i < 4; ++i) Escribe<uint32_t>(a + 4 * i, vf_[rt][i]); break; }
      case QMFC2: { const uint32_t* v = vf_[rd]; R[rt] = v[0] | (uint64_t(v[1]) << 32); up[rt] = v[2] | (uint64_t(v[3]) << 32); break; }
      case QMTC2: if (rd) { vf_[rd][0] = uint32_t(R[rt]); vf_[rd][1] = uint32_t(R[rt] >> 32); vf_[rd][2] = uint32_t(up[rt]); vf_[rd][3] = uint32_t(up[rt] >> 32); } break;
      case CFC2: R[rt] = rd < 16 ? Sx(vi_[rd]) : (rd == 22 ? q_ : 0); break;
      case CTC2: if (rd > 0 && rd < 16) vi_[rd] = uint16_t(R[rt]); break;
      case VADD: vu(in, [](int, double a, double b) { return Add(a, b); }); break;
      case VSUB: vu(in, [](int, double a, double b) { return Add(a, -b); }); break;
      case VMUL: vu(in, [](int, double a, double b) { return Mul(a, b); }); break;
      case VMADD: { uint32_t c[4]; std::memcpy(c, vacc_, 16); vu(in, [&](int i, double a, double b) { return Add(Fv(c[i]), Fv(Mul(a, b))); }); break; }
      case VMSUB: { uint32_t c[4]; std::memcpy(c, vacc_, 16); vu(in, [&](int i, double a, double b) { return Add(Fv(c[i]), -Fv(Mul(a, b))); }); break; }
      case VMOVE: case VMR32: case VABS: case VFTOI: case VITOF:
        if (ft) {
          const uint32_t* s = vf_[fs];
          uint32_t o[4];
          std::memcpy(o, vf_[ft], 16);
          for (int i = 0; i < 4; ++i) {
            if (!((in.w >> (24 - i)) & 1)) continue;
            o[i] = in.op == VMOVE ? s[i] : in.op == VMR32 ? s[(i + 1) & 3] : in.op == VABS ? s[i] & 0x7FFFFFFF
                 : in.op == VFTOI ? Ftoi(s[i], sa) : Itof(s[i], sa);
          }
          std::memcpy(vf_[ft], o, 16);
        }
        break;
      case VOPMULA: {
        const uint32_t *A = vf_[fs], *B = vf_[ft];
        const uint32_t x = Mul(Fv(A[1]), Fv(B[2])), y = Mul(Fv(A[2]), Fv(B[0])), z = Mul(Fv(A[0]), Fv(B[1]));
        vacc_[0] = x; vacc_[1] = y; vacc_[2] = z;
        break;
      }
      case VOPMSUB: {
        const uint32_t *A = vf_[fs], *B = vf_[ft];
        const uint32_t v = (in.w >> 6) & 31;
        uint32_t o[4];
        std::memcpy(o, vf_[v], 16);
        static constexpr int jk[3][2] = {{1, 2}, {2, 0}, {0, 1}};
        for (int i = 0; i < 3; ++i) o[i] = Add(Fv(vacc_[i]), -Fv(Mul(Fv(A[jk[i][0]]), Fv(B[jk[i][1]]))));
        if (v) std::memcpy(vf_[v], o, 16);
        break;
      }
      case VDIV: case VSQRT: case VRSQRT: {
        const double a = Fv(vf_[fs][(in.w >> 21) & 3]), b = Fv(vf_[ft][(in.w >> 23) & 3]);
        q_ = in.op == VDIV ? Div(a, b) : in.op == VSQRT ? Sqrt(b) : Div(a, Fv(Sqrt(b)));
        break;
      }
      default: return Fallo("operacion sin implementar", pc);
    }
    R[0] = 0; up[0] = 0;
    ++cuenta;
    if (io) { n += cuenta; return Fallo("acceso fuera de la RAM", pc); }
    if (hay_dt) {
      pc = dt; hay_dt = false;
      if (dt_llamada) pc = entrar(pc);
      if (pc == kRetorno) break;
      if (cuenta > limite) { n += cuenta; return Fallo("presupuesto de instrucciones", pc); }
      continue;
    }
    if (res == 0) pc += 4;
    else if (res == 1) pc += 8;
    else { dt = dest; dt_llamada = llama; hay_dt = true; pc += 4; }
  }
  n += cuenta;
  return true;
}

// ---------------- autocomprobacion sin datos del juego (= ee.py demo) ----------------
bool Ee::Autocomprobacion() {
  if (Fb(1.0 / 3) != 0x3EAAAAAA || Add(1.0, -std::ldexp(1.0, -40)) != 0x3F7FFFFF || Mul(Fv(0x7F7FFFFF), 2.0) != 0x7F7FFFFF ||
      Fv(0x7F800000) != std::ldexp(1.0, 128) || Fb(1e-40) != 0 || Div(1.0, 3.0) != 0x3EAAAAAA || Sqrt(2.0) != 0x3FB504F3)
    return false;
  Ee c;
  c.Preparar(0x1000, 0x1100);
  static constexpr uint32_t prog[] = {0x8F828018, 0x08000409, 0x2084FFFF, 0x00021FC2, 0x30210001, 0x00021040, 0x00611826,
                                      0x2084FFFF, 0x00431025, 0x0481FFF9, 0x00020902, 0xAF828018, 0x2442FFFF, 0x03E00008, 0x00000000};
  for (uint32_t i = 0; i < std::size(prog); ++i) c.W32(0x1000 + 4 * i, prog[i]);
  c.gp = 0x10000;
  uint32_t st = 3635744825u;
  c.W32(c.gp - 0x7FE8, st);
  for (uint32_t k : {1u, 32u, 255u, 7u}) {
    if (!c.Llamar(0x1000, {k})) return false;
    for (uint32_t i = 0; i < k; ++i) st = (st << 1) | (((st >> 31) ^ (st >> 4)) & 1);
    if (uint32_t(c.r[2]) != st - 1) return false;
  }
  return true;
}

// ---------------- Bubbles (IW EU SLES_553.47) ----------------
namespace bubbles {
namespace {
// Direcciones medidas (INFORME_fase0E): gp, semilla Memory, puntero al gestor, IA s_len_3_2f, updates 0x4995A0.
// dia1E5: las direcciones del ELF (no las del espejo) son variables: valen las del EU y MapaUsa() las pasa a SLUS_218.42.
uint32_t kGp = 0x4A3DF0, kSemilla = kGp - 0x7FE8, kPMgr = kGp - 0x7380, kIA = 0x31A940, kUpdate = 0x31B260;
uint32_t kEntrar = 0x499560;  // tabla de entradas a estado (las direcciones se leen del ELF al cargar)
constexpr uint32_t kTexto0 = 0x100000, kTexto1 = 0x37E000;
constexpr uint64_t kHashEu = 0x5d867004bae2b6c9ull;  // FNV-1a del texto [0x100000,0x37E000) de SLES_553.47 (solo verificacion)
constexpr uint64_t kHashUsa = 0x7ec0c08a821c1c9dull;  // dia1E5: el mismo hash de SLUS_218.42 (D:/tmp/dia1E5/mapa.py)
void MapaUsa();                                        // dia1E5: al final del archivo (ve todas las direcciones)
// Espejo propio fuera del ELF (PT_LOAD acaba en 0x5D9F00): gestor, tabla work (0x260 por actor), contexto, tareas.
constexpr uint32_t kMgr = 0x1000000, kBub = kMgr + 0x28D0, kJug = kMgr + 0x800;
constexpr uint32_t kObjs = 0x1010000, kWork = kObjs + 0x260, kCtx = 0x1012000, kTareas = 0x1020000, kActiva = 0x1030000;
constexpr uint32_t kClip = 0x1031000;  // descriptor de clip a cero (flags 0: sin multiplicador de velocidad)
constexpr uint32_t kMgw = 0x1032000;   // MiniGame_GetWork (+0x238 = normal del barrido para la tarea 5)
uint32_t kPasoT = 0x49C7DC;  // paso de tiempo de las tareas (global del ELF; en juego 1,2 PAL)
uint32_t kTray = 0x31A570, kCrearIA = 0x3172E0;
// Puentes (lo que no decide la logica o vive en sistemas que el espejo no monta):
uint32_t kObjsA = 0x29A5D0, kObjsB = 0x318400, kCtxF = 0x28A9D0, kCrear = 0x140300;
uint32_t kVoz = 0x22FD20, kBoca = 0x207080, kPrintf = 0x127DE0, kIcono = 0x317B70;
uint32_t kBones = 0x238670, kRaiz = 0x2F5DA0, kPlatano = 0x3177F0, kHitbox = 0x2403F0, kGetWork = 0x296F20;
uint32_t kBorrar = 0x140460;  // READ_BattleMain (borra la tarea): la lista de tareas es del port
// Tick (tabla 0x4995E0): globales que el original lee sin pasar por funcion (gp-0x7238/-0x7230/-0x7000), rejilla propia,
// puentes de colision (Collision_MoveSphere/Raycast -> wcf::Backend del port) y huesos (CameraLocation -> ancla del port).
uint32_t kTick = 0x31B240, kSweep = 0x2E7640, kRay = 0x2E77D0, kCamLoc = 0x208390;
uint32_t kPMgw = 0x49CBB8, kPObjs = 0x49CBC0, kPGrid = 0x49CDB8;
// dia1E5: globales del ELF que Freezer, MSN019 y Piccolo escriben (antes literales; EU) y el paso de Freezer/Piccolo:
// fRam0049c7dc del original = 1,2 con PAL (EU, 50 campos/s) y 1,0 con NTSC (USA, 60 campos/s; src/modo_original.h).
uint32_t kPCam = 0x49CD6C, kPView = 0x49CE78, kPAncho = 0x49C84C, kPAlto = 0x49C850, kPUia = 0x49CD8C, kPNUia = 0x49CD90,
         kPCtx19 = 0x49CD68, kPasos19D = 0x5D9162, kAnchos19D = 0x5D9164, kY19D = 0x5D90D8, kPVctx = 0x49CDF0;
float kPasoCampo = 1.2f;
constexpr uint32_t kGrid = 0x1033000, kB84 = 0x1034000;  // rejilla (+0x630..+0xAF8) y la altura *(actor+0xB84)
constexpr uint32_t kColCtx = 0x1035000;                   // MGW+0x1E0: contexto de colision (solo lo leen los puentes)
// Palabras del invitado que la tick lee de cuadros anteriores y el port no tiene con nombre (EntradaIA::kGuarda).
constexpr uint32_t kGuardaDir[EntradaIA::kGuarda] = {
    kWork + 0xB4,  kWork + 0x40,  kWork + 0x44,  kWork + 0x48,  kWork + 0x4C,  kWork + 0x60,  kWork + 0x64,
    kWork + 0x68,  kWork + 0x6C,  kWork + 0x1CC, kMgw + 0x238,  kMgw + 0x23C,  kMgw + 0x240,  kMgw + 0x244,
    kMgw + 0x248,  kMgw + 0x24C,  kMgw + 0x250,  kMgw + 0x254,  kMgw + 0x258,  kMgw + 0x25C};

// ---------------- T1C (7/11): piezas comunes de los puentes (studies 5/PLAN_AGILIZAR.md, mejora 3) ----------------
// Lo que repetian Bubbles, Freezer 7D7, Piccolo 7D8, MSN019 7E1, Cell 7DE y Piccolo 7DD, sacado sin cambiar lo que
// calcula. La plantilla para un minijuego nuevo esta junto a Ambito (mas abajo); inventario en T1C_INTERPRETE.md.
using Contacto = EntradaIA::Contacto;
using FnRayo = bool (*)(void*, const float*, const float*, unsigned, float*, Contacto&);
using FnBarrido = bool (*)(void*, const float*, const float*, float, unsigned, float*, Contacto&);

void V0(Ee& c, uint32_t v) { c.r[2] = uint64_t(int64_t(int32_t(v))); }  // valor devuelto (v0, con signo como el EE)
float Fpr(const Ee& c, int i) { const uint32_t b = c.F(i); float f; std::memcpy(&f, &b, 4); return f; }  // f12 = 1er float

// Collision_MoveSphere(ctx a0, desde a1, hasta a2, punto a3, contacto t0, mascara t1; radio f12) y
// Collision_Raycast(ctx, desde, hasta, punto, contacto t0, t1, mascara t2) -> wcf::Backend del port. Como el original:
// punto = choque o `hasta`; contacto normal +0, d +0xC, nodo +0x10 (!= 0 si choca), material +0x20, tipo +0x22.
// Devuelve 1 si choca, 0 si no, -1 si el port no dio ese puente (cuenta como sin choque). `mascara_rayo`: bits de t2 que
// se pasan al rayo (Bubbles todos; 7DD los 16 bajos). punto y k quedan para el registro del llamador.
int Choque(Ee& c, bool rayo, void* host, FnRayo fr, FnBarrido fb, uint32_t mascara_rayo, float punto[3], Contacto& k) {
  const uint32_t a = uint32_t(c.r[5]), b = uint32_t(c.r[6]), pt = uint32_t(c.r[7]), ct = uint32_t(c.r[8]);
  float desde[3], hasta[3];
  for (int i = 0; i < 3; ++i) { desde[i] = c.Rf(a + 4 * i); hasta[i] = c.Rf(b + 4 * i); punto[i] = hasta[i]; }
  const bool falta = rayo ? !fr : !fb;
  bool hit = false;
  if (falta) {}
  else if (rayo) hit = fr(host, desde, hasta, unsigned(c.r[10]) & mascara_rayo, punto, k);
  else hit = fb(host, desde, hasta, Fpr(c, 12), unsigned(c.r[9]), punto, k);
  if (!hit) for (int i = 0; i < 3; ++i) punto[i] = hasta[i];
  for (int i = 0; i < 3; ++i) c.Wf(pt + 4 * i, punto[i]);
  c.W32(pt + 12, c.R32(b + 12));
  c.W32(ct + 0x10, hit ? 1u : 0u);  // ponytail: nodo = 1 (solo se mira != 0); el puntero real seria del WCF del invitado
  if (hit) {
    for (int i = 0; i < 3; ++i) c.Wf(ct + 4 * i, k.normal[i]);
    c.Wf(ct + 0xC, k.d); c.W16(ct + 0x20, k.material); c.W16(ct + 0x22, k.tipo);
  }
  c.r[2] = hit ? 1 : 0;
  return falta ? -1 : int(hit);
}

// BonesMovement -> AnimationsStatic(actor+0x640): clip nuevo de `largo` cuadros desde el 0, sin mezclas, velocidad 1
// (los bancos AMB no estan en el espejo; la pose la evalua el port).
void ClipNuevo(Ee& c, uint32_t x, int32_t largo) {
  c.W32(x + 0xC68, 0); c.W32(x + 0xC6C, uint32_t(largo)); c.Wf(x + 0xC70, float(largo)); c.W32(x + 0xC74, 0);
  for (uint32_t off : {0xC88u, 0xC90u, 0xC9Cu, 0xCB0u}) c.W32(x + off, 0);
  c.Wf(x + 0xAC4, 1.f);
}
// Instancia de animacion (actor+0x640): autopuntero +0x580, descriptor de clip a cero, velocidad 1, sin mezclas secundarias.
void InstanciaAnim(Ee& e, uint32_t x) {
  e.W32(x + 0xBC0, x); e.W32(x + 0xB64, kClip); e.Wf(x + 0xAC4, 1.f); e.Wf(x + 0x18D8, 1.f);
  e.W32(x + 0xC84, 0xFFFFFFFFu); e.W32(x + 0xC8C, 0xFFFFFFFFu);
}
// Node_WrapPositionInGrid: rejilla del campo (lado x lado en celdas x celdas desde `origen`, medida en la RAM de PCSX2).
void Rejilla(Ee& e, float lado, uint32_t celdas, float origen) {
  e.Cero(kGrid, 0xB00);
  e.W32(kPGrid, kGrid); e.W32(kGrid + 0x630, 7); e.Wf(kGrid + 0xAD4, lado); e.Wf(kGrid + 0xAD8, lado);
  e.W32(kGrid + 0xADC, celdas); e.W32(kGrid + 0xAE0, celdas); e.Wf(kGrid + 0xAF0, origen); e.Wf(kGrid + 0xAF8, origen);
}
// Pasos del LFSR de Memory (para puentes que gastan azar sin ejecutar su funcion).
uint32_t Lfsr(uint32_t st, int pasos) {
  for (int i = 0; i < pasos; ++i) st = (st << 1) | (((st >> 31) ^ (st >> 4)) & 1);
  return st;
}
// Registro de pruebas: palabra en hex con coma; lista de clips "a,b,c" o "-".
void Hx(std::string& s, uint32_t w) { char t[16]; std::snprintf(t, sizeof t, "%X,", w); s += t; }
void Hx(std::string& s, float f) { uint32_t w; std::memcpy(&w, &f, 4); Hx(s, w); }
std::string Lista(const std::vector<uint32_t>& ev) {
  std::string v;
  for (uint32_t c : ev) Hx(v, c);
  if (v.empty()) return "-";
  v.pop_back();
  return v;
}

// Quien uso el espejo por ultima vez: un minijuego con espejo persistente (7DD) sabe si sigue siendo suyo.
enum Dueno { kNadie, kDBubbles, kD7D7, kD7D8, kD7E1, kD7DE, kD7DD, kD7DA, kD7DC };

struct Estado {
  Ee ee;
  const EntradaIA* in = nullptr;
  SalidaIA* out = nullptr;
  bool sin_puente = false;  // la tick pidio una colision y el port no dio el puente: esa pasada usa el port
  std::string col;          // solo pruebas: resultados de los puentes de la tick, para reejecutarlos en ee.py
  bool log = false, largos_log = false;
  uint64_t llamadas = 0;
  double us_total = 0, us_max = 0;
  Dueno dueno = kNadie;
  void Cuenta(double us) { ++llamadas; us_total += us; us_max = std::max(us_max, us); }
};

void Evento(SalidaIA& o, char tipo, int n, int cc4, float desvio = 0) {
  if (o.n_eventos < 16) o.eventos[o.n_eventos++] = {tipo, n, cc4, desvio};
}

Estado* Iniciar() {
  auto* s = new Estado;
  std::string motivo;
  if (!Ee::Autocomprobacion()) motivo = "autocomprobacion del interprete";
  else {
    const auto elf = pasarela::LeerElf(pasarela::BuscarIW());
    if (elf.empty()) motivo = "no se encuentra el ELF de IW";
    else if (!s->ee.Cargar(elf, kTexto0, kTexto1)) motivo = s->ee.error();
    else if (s->ee.HashTexto() == kHashUsa) MapaUsa();  // dia1E5: SLUS_218.42 verificado
    else if (s->ee.HashTexto() != kHashEu) motivo = "ELF distinto del EU SLES_553.47 y del USA SLUS_218.42 verificados";
  }
  if (!motivo.empty()) {
    REXLOG_WARN("dbz3 iwmips: logica original de Bubbles DESACTIVADA ({}); se usa el port", motivo);
    delete s;
    return nullptr;
  }
  Ee& e = s->ee;
  e.gp = kGp;
  auto v0 = V0;
  e.puentes[kObjsA] = e.puentes[kObjsB] = [v0](Ee& c) { v0(c, kObjs); };
  e.puentes[kCtxF] = [v0](Ee& c) { v0(c, kCtx); };
  e.puentes[kGetWork] = [v0](Ee& c) { v0(c, kMgw); };
  e.puentes[kBorrar] = [s](Ee&) { s->out->tarea5 = true; };  // la lista es del port; en la tick = choque con tarea
  auto choque = [s](Ee& c, bool rayo) {  // Choque(): colisiones -> wcf::Backend del port
    float punto[3];
    Contacto k;
    const EntradaIA& in = *s->in;
    const int r = Choque(c, rayo, in.host, in.rayo, in.barrido, 0xFFFFFFFFu, punto, k);
    if (r < 0) s->sin_puente = true;
    const bool hit = r > 0;
    if (s->log) {
      char t[200];
      std::snprintf(t, sizeof t, "%c%d/%.9g,%.9g,%.9g/%.9g,%.9g,%.9g/%.9g/%u/%u;", rayo ? 'Y' : 'S', int(hit), punto[0], punto[1],
                    punto[2], k.normal[0], k.normal[1], k.normal[2], k.d, unsigned(k.material), unsigned(k.tipo));
      s->col += t;
    }
  };
  e.puentes[kSweep] = [choque](Ee& c) { choque(c, false); };
  e.puentes[kRay] = [choque](Ee& c) { choque(c, true); };
  // CameraLocation(actor+0x640, mascara): los huesos los pone el modelo HD. Hueso 0 (ancla) = raiz +0x40 + desplazamiento de
  // la pose del port girado por el frente work+0x50: la misma cuenta que Maquina::ActualizarAncla.
  e.puentes[kCamLoc] = [s](Ee& c) {
    const uint32_t x = uint32_t(c.r[4]) - 0x640;
    const float yaw = std::atan2(c.Rf(kWork + 0x50), c.Rf(kWork + 0x58)), co = std::cos(yaw), si = std::sin(yaw);
    const float* l = s->in->local50;
    const float a[3] = {c.Rf(x + 0x40) + (co * l[0] + 0.f * l[1] + si * l[2]), c.Rf(x + 0x44) + (0.f * l[0] + 1.f * l[1] + 0.f * l[2]),
                        c.Rf(x + 0x48) + (-si * l[0] + 0.f * l[1] + co * l[2])};
    for (int i = 0; i < 3; ++i) c.Wf(x + 0x640 + 4 * i, a[i]);
    c.Wf(x + 0x64C, 1.f);
    if (s->log) {
      char t[100];
      std::snprintf(t, sizeof t, "C/%.9g,%.9g,%.9g;", a[0], a[1], a[2]);
      s->col += t;
    }
  };
  // READ_SYSTEMFunctions (Task_Create): bloque a cero propio; la IA rellena modo/fase/duracion/vel/centro.
  e.puentes[kCrear] = [s, v0](Ee& c) {
    const int i = s->out->n_tareas;
    if (i >= 4) { v0(c, kTareas + 0x300); return; }  // ponytail: >4 tareas por llamada no se ve en las capturas
    s->out->n_tareas = i + 1;
    v0(c, kTareas + 0x100 * uint32_t(i));
  };
  e.puentes[kVoz] = [s](Ee& c) { s->out->voz = int(c.r[5] & 0xFFFF); };
  e.puentes[kBoca] = e.puentes[kPrintf] = e.puentes[kHitbox] = [](Ee&) {};
  // s_BG_ICON_CTRL: el efecto 0xB del icono gasta 62 pasos del LFSR (7 x Memory(8) + 6 x Memory(1), medido 6/11)
  // y marca work+0x228. Lo dibuja el port. ponytail: si otro efecto/dificultad gastara otra cuenta, ejecutarlo en el invitado.
  e.puentes[kIcono] = [s](Ee& c) {
    c.W32(kSemilla, Lfsr(c.R32(kSemilla), 62));
    c.W32(kWork + 0x228, 1);
    s->out->icono = true;
  };
  // BonesMovement: AnimationsStatic(actor+0x640) con la duracion del clip que tiene el port (los bancos AMB no estan
  // en el espejo); los efectos y el sonido de carga del ataque no aplican a Bubbles. La pose la evalua el port.
  e.puentes[kBones] = [s](Ee& c) {
    const uint32_t x = uint32_t(c.r[4]), cod = c.R32(x + 0xB60);
    ClipNuevo(c, x, s->in->largos && cod < 0xE3 ? int(s->in->largos[cod]) : 0);
    Evento(*s->out, 'M', int(cod), int16_t(c.R16(x + 0xCC4)));
  };
  e.puentes[kRaiz] = [s](Ee& c) { Evento(*s->out, 'R', 0, int16_t(c.R16(kBub + 0xCC4))); };
  e.puentes[kPlatano] = [s](Ee& c) {  // s_BG_B77_DRAW(desvio f12, actor a0, tipo a1)
    Evento(*s->out, 'B', int(c.r[5] & 0xFFFF), 0, Fpr(c, 12));
  };
  for (int n = 0; n < 10; ++n)
    e.observadores[e.R32(kEntrar + 4 * uint32_t(n))] = [s, n](Ee& c) { Evento(*s->out, 'E', n, int16_t(c.R16(kBub + 0xCC4))); };
  s->log = std::getenv("DBZ3_TEST_IW_SEMILLA") != nullptr;
  REXLOG_INFO("dbz3 iwmips: logica original de Bubbles ACTIVA (ELF {} verificado, interprete autocomprobado)",
              freezer::g_ntsc ? "USA" : "EU");
  return s;
}

Estado* St() {
  static Estado* const st = Iniciar();  // ponytail: la primera llamada lee el ELF (una vez, ~ms); Bubbles y Freezer comparten imagen
  return st;
}
// Interprete listo para un minijuego: cvar dbz3_iw_logica_original y ELF verificado.
Estado* Listo() { return REXCVAR_GET(dbz3_iw_logica_original) ? St() : nullptr; }

// Ambito de una llamada de un minijuego que no es Bubbles (T1C): aparta las entradas a estado de Bubbles
// (observadores) y los puentes que el minijuego declara, y los repone al salir, como hacian Freezer, Piccolo y el 7DD a
// mano. `continua` = el espejo del invitado sigue siendo de este minijuego (nadie mas lo uso desde su ultima llamada):
// asi un minijuego puede guardar su estado en el invitado entre cuadros, como el 7DD.
//
// PLANTILLA para pasar un minijuego nuevo al interprete (pasos y cifras en studies 5/ENTREGAS/T1C_INTERPRETE.md):
//  1. iw_mips.h: namespace <mini> { struct Entrada; struct Salida; inline bool (*g_x)(const Entrada&, Salida&, uint32_t&); }
//  2. Direcciones EU como `uint32_t kX = 0x...;` (variables) y su pareja USA en MapaUsa() (tools/dif_mapa_iw_usa.py).
//  3. bool X(const Entrada& in, Salida& out, uint32_t& semilla) {
//       Estado* const st = Listo(); if (!st) return false;
//       Ee& e = st->ee;
//       Ambito amb(*st, kDxxx);                          // + un valor nuevo en Dueno
//       amb.Puente(kBones, ...); amb.Mudo({kSonidoFz, kHueso});   // los puentes son la unica parte propia
//       if (in.inicio || !amb.continua) { espejo: Rejilla(), InstanciaAnim(), punteros globales... }
//       entradas del port -> invitado; e.W32(kSemilla, semilla);
//       Cronometro cr(e); const bool ok = e.Llamar(kFn, {args});
//       if (!ok) { REXLOG_WARN("... usa el port"); return false; }   // el llamador sigue con el port
//       invitado -> out; semilla = e.R32(kSemilla); st->Cuenta(cr.Us());
//       registro solo con DBZ3_TEST_*: "dbz3 iwmips <mini> ..." que tools/iw_mips/<mini>.py juego --log reejecuta en ee.py
//     }
//  4. Registrar g_x en kRegistrado; el llamador del port (iw_mini_*.cpp) prueba g_x y, si da false, usa lo suyo.
//  Piezas: V0, Fpr, Choque (rayo/barrido -> wcf::Backend), ClipNuevo (BonesMovement), InstanciaAnim, Rejilla, Lfsr, Hx, Lista.
class Ambito {
 public:
  Ambito(Estado& st, Dueno quien) : e_(st.ee), continua(st.dueno == quien) {
    st.dueno = quien;
    std::swap(obs_, e_.observadores);
  }
  ~Ambito() {
    for (uint32_t a : propios_) e_.puentes.erase(a);
    for (auto& [a, f] : guardados_) e_.puentes[a] = std::move(f);
    std::swap(obs_, e_.observadores);
  }
  Ambito(const Ambito&) = delete;
  Ambito& operator=(const Ambito&) = delete;
  void Puente(uint32_t dir, std::function<void(Ee&)> f) {
    if (std::find(propios_.begin(), propios_.end(), dir) == propios_.end()) {
      propios_.push_back(dir);
      if (auto it = e_.puentes.find(dir); it != e_.puentes.end()) guardados_.emplace_back(dir, std::move(it->second));
    }
    e_.puentes[dir] = std::move(f);
  }
  void Mudo(std::initializer_list<uint32_t> dirs) { for (uint32_t a : dirs) Puente(a, [](Ee&) {}); }  // sin efecto
 private:
  Ee& e_;
  std::unordered_map<uint32_t, std::function<void(Ee&)>> obs_;
  std::vector<uint32_t> propios_;
  std::vector<std::pair<uint32_t, std::function<void(Ee&)>>> guardados_;
 public:
  const bool continua;
};
// Instrucciones y microsegundos de una llamada (solo registro/estadistica).
struct Cronometro {
  explicit Cronometro(const Ee& e) : n0(e.n), t0(std::chrono::steady_clock::now()) {}
  double Us() const { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(); }
  uint64_t n0;
  std::chrono::steady_clock::time_point t0;
};

// dia1E5: `dir` por referencia: en la primera llamada St() carga el ELF y MapaUsa() cambia kIA/kUpdate/... despues de
// evaluar el argumento (por valor, la primera pasada con el ELF USA saltaba a la direccion EU).
bool Llamada(const uint32_t& dir, const char* que, const EntradaIA& in, SalidaIA& out, uint32_t& semilla,
             std::initializer_list<uint32_t> args = {kBub}) {
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  out = SalidaIA{};
  st->in = &in;
  st->out = &out;
  st->dueno = kDBubbles;
  e.Cero(kMgr, 0x9000); e.Cero(kObjs, 0x800); e.Cero(kCtx, 0x40); e.Cero(kTareas, 0x400); e.Cero(kActiva, 0x100);
  e.Cero(kClip, 0x40); e.Cero(kMgw, 0x300);
  e.Wf(kPasoT, 1.2f);
  e.W32(kPMgr, kMgr);
  e.Wf(kMgr + 0x8B80, 1.2f);  // factor PAL del gestor (EU)
  e.W32(kBub + 0x1380, 1); e.W32(kBub + 0x1384, kJug); e.Wf(kBub + 0xCAC, 1.f);
  e.W16(kCtx + 0xC, uint16_t(in.dificultad));
  e.W32(kSemilla, semilla);
  e.W16(kBub + 0xCBC, uint16_t(in.estado)); e.W16(kBub + 0xCBE, uint16_t(in.anterior)); e.W16(kBub + 0xCC4, uint16_t(in.cc4));
  e.W16(kJug + 0xCBC, uint16_t(in.estado_jugador));
  for (int i = 0; i < 3; ++i) {
    e.Wf(kBub + 0x640 + 4 * i, in.bub[i]); e.Wf(kJug + 0x640 + 4 * i, in.jug[i]);
    e.Wf(kBub + 0x40 + 4 * i, in.p[i]);
    e.Wf(kJug + 0x40 + 4 * i, in.jug[i]);  // ponytail: raiz del jugador = su ancla (solo la usa el giro hacia el al tirar)
  }
  e.Wf(kBub + 0x64C, 1.f); e.Wf(kJug + 0x64C, 1.f); e.Wf(kBub + 0x4C, 1.f); e.Wf(kJug + 0x4C, 1.f);
  InstanciaAnim(e, kBub);
  e.W32(kBub + 0xB60, in.codigo); e.Wf(kBub + 0xC68, in.cuadro); e.W32(kBub + 0xC6C, uint32_t(in.duracion));
  e.W32(kBub + 0xC74, in.fin); e.W32(kBub + 0x18E8, in.flags18e8);
  e.W32(kWork + 0xB8, in.tarea ? kActiva : 0);
  e.Wf(kWork + 0x50, in.frente_x); e.Wf(kWork + 0x58, in.frente_z);
  e.Wf(kWork + 0x1F4, in.stick_x); e.Wf(kWork + 0x1F8, in.stick_y); e.Wf(kWork + 0x1A0, in.reloj);
  e.W32(kWork + 0x218, in.f218); e.W32(kWork + 0x224, in.latch224);
  e.W32(kObjs + 0x248, in.bloqueo248); e.W32(kObjs + 0x180, in.objetivo180);
  e.W32(kWork + 0x228, in.icono);
  // Tarea en movimiento (bloque propio en kActiva) y normal del barrido.
  for (int i = 0; i < 3; ++i) {
    e.Wf(kActiva + 0x30 + 4 * i, in.tm_centro[i]); e.Wf(kActiva + 0x40 + 4 * i, in.tm_vel[i]);
    e.Wf(kMgw + 0x238 + 4 * i, in.normal238[i]);
  }
  e.Wf(kActiva + 0x50, in.tm_tiempo); e.Wf(kActiva + 0x54, in.tm_duracion); e.W32(kActiva + 0x58, 1);
  e.W16(kActiva + 0x60, uint16_t(in.tm_modo)); e.W16(kActiva + 0x62, uint16_t(in.tm_fase));
  // Tick: rejilla 1800 x 1800 en 6 x 6 celdas centrada en -750 (medida en la RAM de la captura = -900..900 del port),
  // punteros globales, altura B84, aire, raiz al empezar el cuadro (+0x50, la copia GroundMinigame...0x239A00),
  // work+0x90, suelo y el frente de referencia work+0x30 = (0,0,1) medido.
  Rejilla(e, 1800.f, 6, -750.f); e.Cero(kB84, 0x10);
  e.W32(kPMgw, kMgw); e.W32(kPObjs, kObjs); e.W32(kMgw + 0x1E0, kColCtx);
  e.W32(kBub + 0xB84, kB84); e.Wf(kB84, in.altura_b84); e.W32(kBub + 0x2060, in.aire ? 0x100u : 0u);
  e.Wf(kWork + 0x38, 1.f); e.Wf(kWork + 0x3C, 1.f); e.Wf(kBub + 0x18D0, in.suelo18d0);
  for (int i = 0; i < 3; ++i) { e.Wf(kBub + 0x50 + 4 * i, in.p_prev[i]); e.Wf(kWork + 0x90 + 4 * i, in.anterior90[i]); }
  e.Wf(kBub + 0x5C, 1.f); e.Wf(kWork + 0x9C, 1.f);
  if (in.guarda_ok)
    for (int k = 0; k < EntradaIA::kGuarda; ++k) e.W32(kGuardaDir[k], in.guarda[k]);
  st->sin_puente = false;
  st->col.clear();
  const uint32_t w50 = e.R32(kWork + 0x50), w58 = e.R32(kWork + 0x58);
  const Cronometro cr(e);
  const bool ok = e.Llamar(dir, args);
  const double us = cr.Us();
  st->out = nullptr;
  st->in = nullptr;
  if (!ok || st->sin_puente) {
    REXLOG_WARN("dbz3 iwmips bubbles {}: {} (tick={}); esta pasada usa el port", que,
                ok ? std::string("colision sin puente del port") : e.error(), in.tick);
    if (st->log)  // dia1E5: entradas de la pasada fallida (para reproducirla en ee.py)
      REXLOG_WARN("dbz3 iwmips bubbles fallo {} ra={:08X} estado={} anterior={} jugador={} codigo={:X} cuadro={:.9g} dur={} fin={} "
                  "f18e8={:X} cc4={} reloj={:.9g} tarea={} p={:.9g},{:.9g},{:.9g} largos={}", que, uint32_t(e.r[31]), in.estado,
                  in.anterior, in.estado_jugador, in.codigo, in.cuadro, in.duracion, int(in.fin), in.flags18e8, in.cc4, in.reloj,
                  int(in.tarea), in.p[0], in.p[1], in.p[2], in.largos ? 1 : 0);
    return false;
  }
  for (int i = 0; i < 3; ++i) out.a90[i] = e.Rf(kWork + 0x90 + 4 * i);
  out.suelo18d0 = e.Rf(kBub + 0x18D0); out.f184c = e.Rf(kBub + 0x184C);
  out.tipo1f0 = e.R32(kMgw + 0x1F0 + 4); out.tipo1e8 = e.R32(kMgw + 0x1E8 + 4);  // actor 1 (+0x1380)
  for (int k = 0; k < EntradaIA::kGuarda; ++k) out.guarda[k] = e.R32(kGuardaDir[k]);
  out.f200 = e.R32(kWork + 0x200) != 0; out.f210 = e.R32(kWork + 0x210) != 0;
  out.f214 = e.R32(kWork + 0x214) != 0; out.f218 = e.R32(kWork + 0x218) != 0;
  out.stick_x = e.Rf(kWork + 0x1F4);
  for (int i = 0; i < out.n_tareas; ++i) {
    const uint32_t t = kTareas + 0x100 * uint32_t(i);
    auto& o = out.tareas[i];
    o.modo = int16_t(e.R16(t + 0x60)); o.fase = int16_t(e.R16(t + 0x62)); o.duracion = e.Rf(t + 0x54);
    for (int j = 0; j < 3; ++j) { o.vel[j] = e.Rf(t + 0x40 + 4 * j); o.centro[j] = e.Rf(t + 0x30 + 4 * j); }
  }
  out.estado = int16_t(e.R16(kBub + 0xCBC)); out.anterior = int16_t(e.R16(kBub + 0xCBE)); out.cc4 = int16_t(e.R16(kBub + 0xCC4));
  out.codigo = e.R32(kBub + 0xB60); out.cuadro = e.Rf(kBub + 0xC68); out.fin = e.R32(kBub + 0xC74) != 0;
  out.reloj = e.Rf(kWork + 0x1A0); out.latch224 = e.R32(kWork + 0x224) != 0;
  out.frente_x = e.Rf(kWork + 0x50); out.frente_z = e.Rf(kWork + 0x58);
  out.frente_cambia = e.R32(kWork + 0x50) != w50 || e.R32(kWork + 0x58) != w58;
  for (int i = 0; i < 3; ++i) {
    out.p[i] = e.Rf(kBub + 0x40 + 4 * i); out.tm_vel[i] = e.Rf(kActiva + 0x40 + 4 * i); out.tm_centro[i] = e.Rf(kActiva + 0x30 + 4 * i);
  }
  out.tm_tiempo = e.Rf(kActiva + 0x50); out.tm_fase = int16_t(e.R16(kActiva + 0x62)); out.tm_fin = e.R32(kWork + 0xB8) == 0;
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  st->Cuenta(us);
  if (st->log) {  // solo pruebas: entradas + salida (tools/iw_mips/bubbles.py juego <log> la reejecuta en ee.py)
    char b[160];
    if (!st->largos_log && in.largos) {
      std::string l;
      for (int i = 0; i < 0xE3; ++i)
        if (in.largos[i] != 0) { std::snprintf(b, sizeof b, "%X:%.9g;", i, in.largos[i]); l += b; }
      REXLOG_INFO("dbz3 iwmips bubbles largos={}", l);
      st->largos_log = true;
    }
    std::string ts, ev;
    for (int i = 0; i < out.n_tareas; ++i) {
      const auto& o = out.tareas[i];
      std::snprintf(b, sizeof b, "%d/%d/%.9g/%.9g,%.9g,%.9g;", o.modo, o.fase, o.duracion, o.vel[0], o.vel[1], o.vel[2]);
      ts += b;
    }
    for (int i = 0; i < out.n_eventos; ++i) {
      const auto& v = out.eventos[i];
      std::snprintf(b, sizeof b, "%c%d/%d/%.9g;", v.tipo, v.n, v.cc4, v.desvio);
      ev += b;
    }
    // Tick: entradas de cuadros anteriores, resultados de los puentes (col) y salidas, para `bubbles.py juego`.
    char t[400];
    std::snprintf(t, sizeof t,
                  " pprev=%.9g,%.9g,%.9g a90in=%.9g,%.9g,%.9g suelo_in=%.9g b84=%.9g aire=%d l50=%.9g,%.9g,%.9g"
                  " a90=%.9g,%.9g,%.9g suelo=%.9g f184c=%.9g t1f0=%u t1e8=%u t5=%d fout=%.9g,%.9g",
                  in.p_prev[0], in.p_prev[1], in.p_prev[2], in.anterior90[0], in.anterior90[1], in.anterior90[2], in.suelo18d0,
                  in.altura_b84, int(in.aire), in.local50[0], in.local50[1], in.local50[2],
                  out.a90[0], out.a90[1], out.a90[2], out.suelo18d0, out.f184c, out.tipo1f0, out.tipo1e8, int(out.tarea5),
                  out.frente_x, out.frente_z);
    std::string tk = t;
    tk += " col=";
    tk += st->col.empty() ? "-" : st->col;
    for (int g = 0; g < 2; ++g) {
      if (g == 0 && !in.guarda_ok) continue;
      tk += g ? " gout=" : " gin=";
      for (int k = 0; k < EntradaIA::kGuarda; ++k) {
        std::snprintf(b, sizeof b, "%X,", g ? out.guarda[k] : in.guarda[k]);
        tk += b;
      }
    }
    REXLOG_INFO("dbz3 iwmips bubbles {} tick={} seed_in={:08X} seed_out={:08X} estado={} anterior={} jugador={} dif={} tarea={} "
                "gates={}/{} icono_in={} bub={:.9g},{:.9g},{:.9g} jug={:.9g},{:.9g},{:.9g} frente={:.9g},{:.9g} stick={:.9g},{:.9g} "
                "p={:.9g},{:.9g},{:.9g} cc4={} codigo={:X} cuadro={:.9g} dur={} fin={} f18e8={} reloj={:.9g} f218in={} latch={} "
                "flags={}{}{}{} tareas={} voz={} icono={} sale={}/{}/{:X}/{:.9g}/{}/{}/{:.9g}/{} ev={} instr={} us={:.1f} "
                "tm={}/{}/{:.9g}/{:.9g}/{:.9g},{:.9g},{:.9g}/{:.9g},{:.9g},{:.9g} n238={:.9g},{:.9g},{:.9g} "
                "pout={:.9g},{:.9g},{:.9g} tout={}/{:.9g}/{:.9g},{:.9g},{:.9g}/{} media_us={:.2f} max_us={:.1f}{}",
                que, in.tick, semilla_in, semilla, in.estado, in.anterior, in.estado_jugador, in.dificultad, int(in.tarea),
                int(in.bloqueo248), int(in.objetivo180), int(in.icono), in.bub[0], in.bub[1], in.bub[2], in.jug[0], in.jug[1],
                in.jug[2], in.frente_x, in.frente_z, in.stick_x, in.stick_y, in.p[0], in.p[1], in.p[2], in.cc4, in.codigo, in.cuadro,
                in.duracion, int(in.fin), in.flags18e8, in.reloj, int(in.f218), int(in.latch224), int(out.f200), int(out.f210),
                int(out.f214), int(out.f218), ts.empty() ? "-" : ts, out.voz, int(out.icono), out.estado, out.anterior, out.codigo,
                out.cuadro, int(out.fin), out.cc4, out.reloj, int(out.latch224), ev.empty() ? "-" : ev, e.n - cr.n0, us,
                in.tm_modo, in.tm_fase, in.tm_duracion, in.tm_tiempo, in.tm_vel[0], in.tm_vel[1], in.tm_vel[2], in.tm_centro[0],
                in.tm_centro[1], in.tm_centro[2], in.normal238[0], in.normal238[1], in.normal238[2], out.p[0], out.p[1], out.p[2],
                out.tm_fase, out.tm_tiempo, out.tm_vel[0], out.tm_vel[1], out.tm_vel[2], int(out.tm_fin),
                st->us_total / double(st->llamadas), st->us_max, tk);
  }
  return true;
}

bool IA(const EntradaIA& in, SalidaIA& out, uint32_t& semilla) { return Llamada(kIA, "ia", in, out, semilla); }
bool Update(const EntradaIA& in, SalidaIA& out, uint32_t& semilla) {
  return in.largos && Llamada(kUpdate, "upd", in, out, semilla);
}

bool Trayectoria(const EntradaIA& in, SalidaIA& out, uint32_t& semilla) {
  return Llamada(kTray, "tray", in, out, semilla, {kActiva});
}
bool Tarea5(const EntradaIA& in, SalidaIA& out, uint32_t& semilla) {
  return Llamada(kCrearIA, "tarea5", in, out, semilla, {kBub, 1, 5});
}

// Tabla de ticks 0x4995E0 entera (andar con el stick, girar, colisiones, suelo, rejilla, tarea 5 por choque).
bool Tick(const EntradaIA& in, SalidaIA& out, uint32_t& semilla) {
  return in.barrido && in.rayo && Llamada(kTick, "tick", in, out, semilla);
}

// ---------------- Freezer 0x7D7: esquiva (Char_PoseInputTransition 0x35DF50) ----------------
// Se ejecutan del ELF: la funcion, Viewer_RollChance_B4c/B50, Viewer_GetRoundParamB48, Char_UpdateTargetDirectionTracker,
// Char_SelectBoneScreenDirection, Char_EnterPoseState1/2Transform, Memory, la matematica VU0/FPU y la doble precision
// por software. Puentes: BonesMovement (anota el clip; lo pone el port), el sonido de la esquiva (no-op),
// Char_GetBoneWorldPos (no-op) y Math_WorldToScreen (la misma cuenta de pantalla que el port, solo con apunta>0,95).
uint32_t kTrans = 0x35DF50, kSonidoFz = 0x240740, kHueso = 0x2EF6A0, kPantalla = 0x2EF870;
uint32_t kGlobFz = 0x5D9D60, kTablaFz = 0x5D9B48;
constexpr uint32_t kCamFz = 0x1036000, kViewFz = 0x1037000;
constexpr uint32_t kOffGlob[10] = {0, 4, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28};

bool ActivaFz() { return Listo() != nullptr; }

// BonesMovement que solo anota el clip pedido (+0xB60): el clip lo pone el port (Freezer, Piccolo 7D8).
std::function<void(Ee&)> AnotaClip(std::vector<uint32_t>& ev) {
  return [&ev](Ee& c) { ev.push_back(c.R32(uint32_t(c.r[4]) + 0xB60)); };
}

bool Esquiva(const freezer::EntradaEsq& in, freezer::SalidaEsq& out, uint32_t& semilla) {
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  std::vector<uint32_t> ev;
  std::string scr;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  Ambito amb(*st, kD7D7);  // las entradas a estado de Bubbles no aplican aqui
  amb.Puente(kBones, AnotaClip(ev));
  amb.Mudo({kSonidoFz, kHueso});
  // Math_WorldToScreen(sale a0, ...): x - ancho/2 y y - alto/2 = la cuenta del port (preparar de EsquivaFz).
  amb.Puente(kPantalla, [&](Ee& c) {
    const float azp = c.Rf(kGlobFz) - 1.5707964f, el = c.Rf(kGlobFz + 4);
    const float fx = c.Rf(kCamFz + 0x90), fy = c.Rf(kCamFz + 0x94), fz = c.Rf(kCamFz + 0x98);
    const float caz = std::atan2(-fx, fz), cel = std::asin(std::clamp(fy, -1.f, 1.f));
    const float sx = 320.f + (azp - caz) * std::cos(el), sy = 224.f + -(el - cel);
    const uint32_t a = uint32_t(c.r[4]);
    c.Wf(a, sx); c.Wf(a + 4, sy); c.W32(a + 8, 0); c.Wf(a + 12, 1.f);
    c.r[2] = 1;
    if (log) {
      char t[40];
      std::snprintf(t, sizeof t, "%X,%X,", c.R32(a), c.R32(a + 4));
      scr += t;
    }
  });
  e.Cero(kMgr, 0x9000); e.Cero(kCamFz, 0x200); e.Cero(kViewFz, 0x40);
  e.W32(kSemilla, semilla);
  e.W32(kPCam, kCamFz); e.W32(kPView, kViewFz); e.W32(kViewFz + 0x14, 0);
  e.Wf(kPasoT, kPasoCampo); e.Wf(kPAncho, 640.f); e.Wf(kPAlto, 448.f);
  e.Wf(kTablaFz, in.b48); e.W32(kTablaFz + 4, uint32_t(in.b4c)); e.W32(kTablaFz + 8, uint32_t(in.b50));
  for (int i = 0; i < 10; ++i) e.W32(kGlobFz + kOffGlob[i], in.glob[i]);
  e.W16(kBub + 0xCBC, uint16_t(in.estado)); e.W32(kBub + 0xC74, uint32_t(in.c74)); e.W16(kJug + 0xCBC, uint16_t(in.krillin));
  // Char_PoseModeUpdateAll: Freezer en la esfera de 150 (ojo en el origen); camara: ojo +0x80, objetivo +0x90.
  const float az = e.Rf(kGlobFz), el = e.Rf(kGlobFz + 4), ce = std::cos(el);
  const float pos[3] = {std::cos(az) * 150.f * ce, std::sin(el) * 150.f, std::sin(az) * 150.f * ce};
  for (int i = 0; i < 3; ++i) { e.Wf(kCamFz + 0x90 + 4 * i, in.f[i]); e.Wf(kBub + 0x40 + 4 * i, pos[i]); }
  e.Wf(kCamFz + 0x8C, 1.f); e.Wf(kCamFz + 0x9C, 1.f); e.Wf(kBub + 0x4C, 1.f);
  const Cronometro cr(e);
  e.suma_ps2 = true;
  const bool ok = e.Llamar(kTrans, {kBub, kJug});
  e.suma_ps2 = false;
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips freezer: {} (t={}); este campo usa el port", e.error(), in.t);
    return false;
  }
  for (int i = 0; i < 10; ++i) out.glob[i] = e.R32(kGlobFz + kOffGlob[i]);
  out.estado = int16_t(e.R16(kBub + 0xCBC));
  out.b60 = e.R32(kBub + 0xB60);
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  st->Cuenta(us);
  if (log) {  // solo pruebas: tools/iw_mips/freezer.py juego --log <dbz3.log> la reejecuta en ee.py
    std::string a, b, f, p;
    char t[24];
    for (int i = 0; i < 10; ++i) { Hx(a, in.glob[i]); Hx(b, out.glob[i]); }
    std::snprintf(t, sizeof t, "%X,%X,%X", unsigned(in.estado), unsigned(in.c74), unsigned(in.krillin)); a += t;
    std::snprintf(t, sizeof t, "%X,%X", unsigned(uint16_t(out.estado)), out.b60); b += t;
    for (int i = 0; i < 3; ++i) { Hx(f, in.f[i]); Hx(p, pos[i]); }
    REXLOG_INFO("dbz3 iwmips freezer t={} seed_in={:08X} seed_out={:08X} in={} f={} pos={} dif={} scr={} out={} ev={} "
                "instr={} us={:.1f} media_us={:.2f} max_us={:.1f}",
                in.t, semilla_in, semilla, a, f, p, in.dif, scr.empty() ? "-" : scr, b, Lista(ev), e.n - cr.n0, us,
                st->us_total / double(st->llamadas), st->us_max);
  }
  return true;
}

// ---------------- MSN019 0x7E1: reparto de la fila (Memory(32) + Results_RespawnItem 0x2EB000) ----------------
// Se ejecutan del ELF: Memory, Results_RespawnItem, UI_GetObjectDataPtr, UI_SetObjectPosVec, UI_SetObjectFlag2,
// UI_SetObjectField158AndEnable, UI_CheckTwoObjectsOverlap, Math_RectsOverlap y Results_ItemTypeToMask. Sin puentes:
// el espejo es la tabla de objetos UI (*(0x49CD8C), 400 B por objeto), el contexto *(0x49CD68) y las constantes de
// Minigame_InitConfigTable medidas en PCSX2 (cap_7e1, INFORME_dia1E §9).
uint32_t kMemoryFn = 0x127CA0, kRespawn = 0x2EB000;
constexpr uint32_t kUia = 0x1040000, kCtx19 = 0x1048000, kNObj = 0x30;
constexpr uint16_t kPasos19 = 4, kY19 = 395;   // medidos (cap_7e1): pasos 0x5D9162, y 0x5D90D8; pantalla 600 x 448
constexpr float kCaja19[4] = {-16.f, -16.f, 16.f, 16.f};

bool Fila19(const msn019::EntradaFila& in, msn019::SalidaFila& out, uint32_t& semilla) {
  if (in.n < 1 || in.n > 10) return false;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  Ambito amb(*st, kD7E1);
  e.Cero(kUia, kNObj * 400); e.Cero(kCtx19, 0x200);
  e.W32(kSemilla, semilla);
  e.W32(kPUia, kUia); e.W32(kPNUia, kNObj); e.W32(kPCtx19, kCtx19);
  e.W16(kCtx19 + 0x66, uint16_t(in.n)); e.W16(kCtx19 + 0x64, in.mascara); e.W32(kCtx19 + 0x114, uint32_t(in.dif));
  e.W16(kPasos19D, kPasos19); e.Wf(kAnchos19D + 4 * uint32_t(in.dif), in.ancho); e.Wf(kPAncho, 600.f); e.W16(kY19D, kY19);
  for (int i = 0; i < in.n; ++i) {
    const uint32_t b = kUia + uint32_t(0x11 + i) * 400;
    const auto& p = in.prev[i];
    e.W32(b + 0x150, 7); e.Wf(b + 0x20, p.x); e.Wf(b + 0x24, p.y); e.Wf(b + 0x28, 10.f); e.Wf(b + 0x2C, 1.f);
    for (int k = 0; k < 4; ++k) e.Wf(b + 0x30 + 4 * uint32_t(k), kCaja19[k]);
    e.W32(b + 0x44, p.estado); e.W32(b + 0x48, p.tipo); e.W32(b + 0x4C, p.hueco);
  }
  const uint32_t semilla_in = semilla;
  const Cronometro cr(e);
  bool ok = e.Llamar(kMemoryFn, {0x20});
  const uint32_t oculto = in.excluir ? uint32_t(e.r[2]) % uint32_t(in.n) + 0x11 : 0xFFFFFFFFu;
  for (int i = 0; ok && i < in.n; ++i)
    ok = e.Llamar(kRespawn, {uint32_t(0x11 + i), in.primera ? uint32_t(i) : 0xFFFFFFFFu, oculto});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips msn019: {} (fila={}); esta fila usa el port", e.error(), in.fila);
    return false;
  }
  out.oculto = in.excluir ? int(oculto - 0x11) : -1;
  for (int i = 0; i < in.n; ++i) {
    const uint32_t b = kUia + uint32_t(0x11 + i) * 400;
    out.obj[i] = {e.Rf(b + 0x20), e.Rf(b + 0x24), e.R32(b + 0x44), e.R32(b + 0x48), e.R32(b + 0x4C)};
  }
  semilla = e.R32(kSemilla);
  if (st->log || std::getenv("DBZ3_TEST_IWMINI")) {  // solo pruebas: tools/iw_mips/msn019.py juego --log <dbz3.log>
    std::string c, p, o;
    char t[64];
    uint32_t w;
    std::memcpy(&w, &in.ancho, 4);
    std::snprintf(t, sizeof t, "%X,%X,%X,%X,%X,%X,", unsigned(in.n), unsigned(in.mascara), unsigned(in.dif), unsigned(kPasos19), w,
                  unsigned(kY19));
    c = t;
    for (float k : kCaja19) { std::memcpy(&w, &k, 4); std::snprintf(t, sizeof t, "%X,", w); c += t; }
    std::snprintf(t, sizeof t, "%X,%X", unsigned(in.excluir), unsigned(in.primera)); c += t;
    auto obj = [&](std::string& s, const msn019::Objeto& b) {
      uint32_t x, y;
      std::memcpy(&x, &b.x, 4); std::memcpy(&y, &b.y, 4);
      std::snprintf(t, sizeof t, "%X,%X,%X,%X,%X,", x, y, b.estado, b.tipo, b.hueco);
      s += t;
    };
    for (int i = 0; i < in.n; ++i) { obj(p, in.prev[i]); obj(o, out.obj[i]); }
    REXLOG_INFO("dbz3 iwmips msn019 fila={} seed_in={:08X} seed_out={:08X} cfg={} prev={} out={} oculto={} instr={} us={:.1f}", in.fila,
                semilla_in, semilla, c, p, o, out.oculto, e.n - cr.n0, us);
  }
  return true;
}

// ---------------- dia1E4: Piccolo 0x7D8, esquiva (Viewer_UpdatePoseRotationInput 0x32DC80) ----------------
// Se ejecutan del ELF: Viewer_InitParamTables 0x3279F0 (vida/BC por dificultad, umbrales 0,25 y 0,4), la funcion,
// Viewer_PoseScheduler, Viewer_PickRandomDirection, Viewer_PoseEnterState1/2, Viewer_IsBelowThreshold(9878),
// Viewer_GetParamBC, Memory, la doble precision por software y la VU0. Puentes: BonesMovement (anota el clip),
// el sonido (no-op), Char_GetBoneWorldPos y Math_WorldToScreen (PickRandomDirection no usa su resultado).
// Dorado: tools/iw_mips/piccolo.py dorado (capturas solIWX, 756/756 en el estado 2).
uint32_t kRotPc = 0x32DC80, kInitPc = 0x3279F0, kGlobPc = 0x5D9940;
constexpr uint32_t kVctxPc = 0x1038000;

bool EsquivaPc(const piccolo::EntradaPc& in, piccolo::SalidaPc& out, uint32_t& semilla) {
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  std::vector<uint32_t> ev;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  Ambito amb(*st, kD7D8);
  amb.Puente(kBones, AnotaClip(ev));
  amb.Mudo({kSonidoFz, kHueso});
  amb.Puente(kPantalla, [](Ee& c) { c.r[2] = 1; });
  e.Cero(kMgr, 0x9000); e.Cero(kCamFz, 0x200); e.Cero(kVctxPc, 0x40);
  const Cronometro cr(e);
  bool ok = e.Llamar(kInitPc, {});
  e.W32(kSemilla, semilla); e.W32(kPCam, kCamFz); e.W32(kPVctx, kVctxPc); e.Wf(kPasoT, kPasoCampo);
  e.Wf(kVctxPc + 4, in.vida); e.W32(kVctxPc + 0x28, uint32_t(in.dif));
  for (int i = 0; i < 10; ++i) e.W32(kGlobPc + 4 * uint32_t(i), in.glob[i]);
  e.W16(kBub + 0xCBC, uint16_t(in.estado)); e.W32(kBub + 0xC74, uint32_t(in.c74));
  for (int i = 0; i < 3; ++i) {
    e.Wf(kBub + 0x40 + 4 * i, in.pos[i]); e.Wf(kCamFz + 0x80 + 4 * i, in.ojo[i]); e.Wf(kCamFz + 0x90 + 4 * i, in.obj[i]);
  }
  e.Wf(kBub + 0x4C, 1.f); e.Wf(kCamFz + 0x8C, 1.f); e.Wf(kCamFz + 0x9C, 1.f);
  e.suma_ps2 = true;
  ok = ok && e.Llamar(kRotPc, {kBub});
  e.suma_ps2 = false;
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips piccolo: {} (t={}); este campo usa el port", e.error(), in.t);
    return false;
  }
  for (int i = 0; i < 10; ++i) out.glob[i] = e.R32(kGlobPc + 4 * uint32_t(i));
  out.estado = int16_t(e.R16(kBub + 0xCBC));
  out.b60 = e.R32(kBub + 0xB60);
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  st->Cuenta(us);
  if (log) {  // solo pruebas: tools/iw_mips/piccolo.py juego --log <dbz3.log> la reejecuta en ee.py
    std::string a, b, k;
    char t[24];
    for (int i = 0; i < 10; ++i) { Hx(a, in.glob[i]); Hx(b, out.glob[i]); }
    std::snprintf(t, sizeof t, "%X,%X,%X,", unsigned(in.estado), unsigned(in.c74), unsigned(in.dif)); a += t;
    Hx(a, in.vida); a.pop_back();
    for (const float* p : {in.pos, in.ojo, in.obj}) for (int i = 0; i < 3; ++i) Hx(k, p[i]);
    k.pop_back();
    std::snprintf(t, sizeof t, "%X,%X", unsigned(uint16_t(out.estado)), out.b60); b += t;
    REXLOG_INFO("dbz3 iwmips piccolo t={} seed_in={:08X} seed_out={:08X} in={} cam={} out={} ev={} instr={} us={:.1f} "
                "media_us={:.2f} max_us={:.1f}",
                in.t, semilla_in, semilla, a, k, b, Lista(ev), e.n - cr.n0, us, st->us_total / double(st->llamadas),
                st->us_max);
  }
  return true;
}

// ---------------- N2 (noche 6-7/11): Cell 0x7DE, las 5 marcas de su carrera (Char_InitTrailMarkerRandom 0x31D8F0) ---
// Se ejecuta del ELF la funcion 5 veces sobre la tabla 0x5D9500 (como Char_EnterPoseState1Alt y el arranque, que antes
// ponen a cero cada marca), con Memory y la conversion entera/flotante del original. Sin puentes.
uint32_t kMarcaFn = 0x31D8F0, kMarcasD = 0x5D9500;

bool Marcas7DE(cell::Marca (&out)[5], uint32_t& semilla, uint64_t t) {
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  Ambito amb(*st, kD7DE);
  e.Cero(kMarcasD, 5 * 0x30);
  e.W32(kSemilla, semilla);
  e.Wf(kPasoT, kPasoCampo);
  const uint32_t semilla_in = semilla;
  const Cronometro cr(e);
  bool ok = true;
  for (uint32_t k = 0; ok && k < 5; ++k) ok = e.Llamar(kMarcaFn, {kMarcasD + 0x30 * k});
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips cell: {} (t={}); estas marcas las pone el port", e.error(), t);
    return false;
  }
  std::string m;
  for (uint32_t k = 0; k < 5; ++k) {
    const uint32_t a = kMarcasD + 0x30 * k;
    out[k] = {int(e.R32(a)), e.Rf(a + 0x10), e.Rf(a + 0x14), e.Rf(a + 0x18), e.Rf(a + 0x20), e.Rf(a + 0x24)};
    for (uint32_t o : {0u, 0x10u, 0x18u, 0x20u, 0x24u}) Hx(m, e.R32(a + o));
  }
  semilla = e.R32(kSemilla);
  if (st->log || std::getenv("DBZ3_TEST_IWMINI") || std::getenv("DBZ3_TEST_IW_SEMILLA"))  // tools/iw_mips/cell.py juego --log
    REXLOG_INFO("dbz3 iwmips cell t={} seed_in={:08X} seed_out={:08X} paso={:08X} m={} instr={}", t, semilla_in, semilla,
                e.R32(kPasoT), m, e.n - cr.n0);
  return true;
}

// ---------------- P1 (7/11): Piccolo 0x7DD, la CPU de la persecucion (iw_mips.h, namespace persecucion) ----------------
// Por campo, del ELF, lo que Battle_UpdateAllCharacters hace con el actor 1 del tipo 0xE: Minigame_ProcessPlayerInput
// (borra las banderas de accion) y Char_DispatchPoseTick (tabla 0x49B2C0: AnimationFramesLink, la CPU
// MinigameHideNSeekPiccolo_sCPU -> Piccolo_sCPUScript, MgChar_EvalPromptFlags, MiniGame_DrawPrompt, MgChar_IsTargetInFront,
// MgChar_CpuAttackDecision, Piccolo_sCPUSpottingSystem, Memory; los cambios de estado de la tabla 0x49B250,
// Char_CheckGroundProximity...), y despues MgObj_UpdateTrajectory con la tarea activa (mueve +0x40).
// Puentes: Collision_Raycast (rayo del port), READ_SYSTEMFunctions (tarea propia), BonesMovement (pone el clip con el
// largo medido, como Bubbles), y sin efecto: sonidos, voz (se anota), 0x11DEF8 (memset(tarea+0x30, 0, 0x40): el bloque
// de la tarea ya sale a cero del puente de READ_SYSTEMFunctions), AuraBurstGlowEffect,
// Char_UpdateRootAndBones, READ_BattleMain, botones y palanca (Piccolo no tiene mando), mano del #BSK y pista de eventos.
// Pegamento (el que el original hace en las Update, que no se ejecutan): matriz del actor = (derecha, arriba, work+0x50,
// +0x40) medida en las 380 muestras; work+0x1A0/+0x1A4 suben un paso por campo (medido: +1,2 por campo PAL).
// Mismo espejo que tools/iw_mips/persecucion.py (Espejo). Dorado: `persecucion.py puentes` (379/380 PCSX2).
uint32_t kCpu7DD = 0x363D10, kEntrar7DD = 0x363E90, kGuion = 0x11DEF8, kAura = 0x1D55A0;
uint32_t kEntradaJug = 0x29A1D0, kTick7DD = 0x363E70, kUpd7DD = 0x363E50, kBotones = 0x299520, kPalanca = 0x299250, kManoBsk = 0x23F7E0,
         kEventos = 0x23F520;
uint32_t kPCtxD = 0x49CBA4, kPCtxM = 0x49CDC0, kPPilaM = 0x49C700;
uint32_t kParamsPic = 0x44EAD0;  // *(actor+0xBB8): tabla de Piccolo en el ELF (+0xC = 9, altura del rayo de suelo)
constexpr uint32_t kCtxM = 0x1039000, kModelo = 0x103A000, kMats = kModelo + 0x100, kHueso7DD = kModelo + 0x800,
                   kPilaM = 0x103B000;
constexpr uint32_t kW1 = kObjs + 0x260;
// Cuadros de los clips de Piccolo (+0xB60 -> +0xC6C): BonesMovement del ELF sobre la RAM del savestate del dorado, codigos
// 0..0x3F y 0x600..0x62F (tools/iw_mips/persecucion.py LARGOS). Los que no tiene caen en la entrada 0x10036 = 30 cuadros,
// como en el original.
uint32_t Largo7DD(uint32_t c) {
  switch (c) {
    case 0x22: case 0x60F: return 2;
    case 0x1F: case 0x24: return 5;
    case 0x29: return 7;
    case 0xA: case 0x14: case 0x16: return 10;
    case 0x20: return 11;
    case 0x18: case 0x19: case 0x1A: case 0x1C: case 0x1D: case 0x1E: return 15;
    case 0x2: case 0x25: return 16;
    case 0x3: return 18;
    case 0x8: case 0x2A: case 0x2B: case 0x612: return 20;
    case 0x23: return 23;
    case 0x5: return 24;
    case 0x3F: return 26;
    case 0x3E: return 27;
    case 0x17: case 0x1B: return 30;
    case 0x607: case 0x627: return 31;
    case 0x4: return 32;
    case 0x7: return 34;
    case 0x6: return 38;
    case 0x21: case 0x610: case 0x611: case 0x624: return 40;
    case 0x3D: return 44;
    case 0x1: return 53;
    case 0x605: case 0x625: return 55;
    case 0x15: return 60;
    case 0x60E: return 70;
    case 0x0: case 0x9: case 0xB: return 100;
    case 0x606: case 0x626: return 115;
    case 0x623: return 170;
    default: return 30;
  }
}

// Raiz animada de Piccolo (= tools/iw_mips/raiz7dd.py; la prueba `persecucion.py juego` exige las mismas operaciones).
// Animations2 0x207180 mezcla cada campo el hueso vivo hacia el clip; Char_UpdateRootAndBones 0x2F5DA0 lo vuelca en +0x40
// segun +0x18E8; CameraLocation/Char_UpdateBoneTransforms ponen el ancla +0x640 = +0x40 + giro * hueso. Solo cuenta la
// posicion del hueso 1 (PIC_WAIST, modelo+0x40C): cada hueso se mezcla por separado.
namespace raiz7dd {
struct Vivo {
  bool ok = false;
  float v[3]{};
  int almacen = -1;
  unsigned indice = 0;
};
Vivo g;
uint32_t U(const std::vector<uint8_t>& b, size_t o) {
  uint32_t v = 0;
  if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4);
  return v;
}
float Fl(const std::vector<uint8_t>& b, size_t o) {
  const uint32_t w = U(b, o);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}
float Trunc(double x) {   // = iwbubbles::poses::Truncar (EE: hacia cero)
  float r = float(x);
  if (std::fabs(double(r)) > std::fabs(x)) {
    uint32_t u;
    std::memcpy(&u, &r, 4);
    if (u & 0x7fffffffu) { --u; std::memcpy(&r, &u, 4); }
  }
  return r;
}
// Animations 0x207E90 (codigo < 0x10000): la primera tabla que tenga el codigo -> almacen e indice; -1 = el reposo.
void Clip(const persecucion::RaizDatos& r, uint32_t codigo) {
  g.almacen = -1;
  for (const auto& b : r.bsk) {
    if (codigo >= U(b, 0x10)) continue;
    const uint32_t blk = U(b, U(b, 0x14) + 4 * size_t(codigo));
    if (!blk) continue;
    g.indice = U(b, blk) & 0xFFFF;
    g.almacen = int(U(b, blk) >> 16);
    return;
  }
}
// Posicion del hueso WAIST del clip en `cuadro` (= poses::Banco::Muestrear, curva de posicion).
void Muestra(const persecucion::RaizDatos& r, float cuadro, float out[3]) {
  out[0] = out[1] = out[2] = 0.f;
  if (g.almacen < 0 || g.almacen > 4) return;
  const auto& a = r.amm[g.almacen];
  if (a.size() < 0x20 || g.indice >= U(a, 16)) return;
  int hueso = -1;
  for (uint32_t j = 0; j < U(a, 24) && hueso < 0; ++j) {
    const size_t p = U(a, 28) + 32 * size_t(j);
    if (p + 32 > a.size()) break;
    std::string n(reinterpret_cast<const char*>(a.data() + p), strnlen(reinterpret_cast<const char*>(a.data() + p), 32));
    const size_t s = n.find('_');
    if ((s == std::string::npos ? n : n.substr(s + 1)) == "WAIST") hueso = int(j);
  }
  if (hueso < 0) return;
  const size_t desc = U(a, 20) + 16 * size_t(g.indice);
  const uint32_t flags = U(a, desc), off = U(a, desc + 12), pistas = (flags & 16) ? 3 : 2;
  const uint32_t p = U(a, off + 4 * (pistas * uint32_t(hueso) + ((flags & 1) ? 1 : 0)));
  if (!p) return;
  const uint32_t n = U(a, p + 8);
  if (!n) return;
  auto t = [&](uint32_t i) { return U(a, p + 12 + 16 * size_t(i)); };
  uint32_t ia = 0;
  while (ia + 1 < n && float(t(ia + 1)) <= cuadro) ++ia;
  const uint32_t ib = std::min(ia + 1, n - 1);
  const float delta = float(t(ib)) - float(t(ia));
  float k = 0.f;
  if (delta > 0) {
    const float d0 = cuadro - float(t(ia));
    k = std::clamp(d0 / delta, 0.f, 1.f);
  }
  for (int i = 0; i < 3; ++i) {
    const float va = Fl(a, p + 16 + 16 * size_t(ia) + 4 * i), vb = Fl(a, p + 16 + 16 * size_t(ib) + 4 * i);
    out[i] = Trunc(double(va) + (double(vb) - va) * k);
  }
}
// Giro de la matriz del actor (derecha (fz,0,-fx), arriba, frente (fx,0,fz)) sobre v; sin fusionar operaciones.
void Girar(float fx, float fz, const float v[3], float d[3]) {
  const float a = v[0] * fz, b = v[2] * fx, c = -v[0] * fx, e = v[2] * fz;
  d[0] = a + b; d[1] = v[1]; d[2] = c + e;
}
}  // namespace raiz7dd

bool Activa7DD() {
  return REXCVAR_GET(dbz3_iw_7dd_original) && Listo() != nullptr;
}

bool Tick7DD(const persecucion::Entrada& in, persecucion::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7dd_original) || !in.rayo || !in.barrido) return false;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = persecucion::Salida{};
  std::vector<uint32_t> ev;
  std::string col;
  bool sin_puente = false;
  // Puentes propios (los de Bubbles miran su Entrada/Salida): se ponen y se quitan en cada llamada (Ambito).
  Ambito amb(*st, kD7DD);
  // El espejo guarda a Piccolo entre campos: si otro minijuego uso el invitado desde la ultima llamada (no deberia
  // pasar en una partida), el estado guardado ya no vale y este campo usa el port en vez de seguir con datos pisados.
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;  // hasta la proxima partida (inicio)
    REXLOG_WARN("dbz3 iwmips persecucion: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  // Rayo y barrido -> el #WCF del campo (Choque(); el rayo pasa los 16 bits bajos de la mascara).
  auto choque = [&](Ee& c, bool es_rayo) {
    float punto[3];
    persecucion::Contacto k;
    const uint32_t pt = uint32_t(c.r[7]), ct = uint32_t(c.r[8]);
    const bool hit = Choque(c, es_rayo, in.host, in.rayo, in.barrido, 0xFFFFu, punto, k) > 0;
    char t[160];
    std::snprintf(t, sizeof t, "%c%d/%X,%X,%X/%X,%X,%X/%X/%X/%X;", es_rayo ? 'Y' : 'S', int(hit), c.R32(pt), c.R32(pt + 4),
                  c.R32(pt + 8), c.R32(ct), c.R32(ct + 4), c.R32(ct + 8), c.R32(ct + 0xC), unsigned(c.R16(ct + 0x20)),
                  unsigned(c.R16(ct + 0x22)));
    col += t;
  };
  amb.Puente(kRay, [&](Ee& c) { choque(c, true); });
  amb.Puente(kSweep, [&](Ee& c) { choque(c, false); });
  // CameraLocation(actor+0x640, mascara): el ancla = raiz + 0,97 en el aire / + 9,2 en el suelo (medido en el dorado;
  // los huesos los pone el modelo HD de B3).
  namespace rz = raiz7dd;
  const persecucion::RaizDatos* const rdat = in.raiz;
  if (in.inicio) rz::g = {};
  // Ancla +0x640 de Piccolo = +0x40 + giro(work+0x50) * hueso vivo (CameraLocation / Char_UpdateBoneTransforms).
  auto ancla = [&](Ee& c) {
    float d[3];
    rz::Girar(c.Rf(kW1 + 0x50), c.Rf(kW1 + 0x58), rz::g.v, d);
    for (int i = 0; i < 3; ++i) c.Wf(kBub + 0x640 + 4 * i, c.Rf(kBub + 0x40 + 4 * i) + d[i]);
    c.Wf(kBub + 0x64C, 1.f);
  };
  // Char_UpdateRootAndBones(actor, work): vuelca el hueso vivo en +0x40 segun +0x18E8 (bits 1 xz, 2 y, 4 y con resto,
  // 8 altura del rayo de la tabla del personaje) y pone el ancla. Sin recursos, sin efecto (como antes).
  auto raiz = [&](Ee& c, uint32_t x, uint32_t w) {
    if (x != kBub || !rz::g.ok) return;
    const uint32_t fl = c.R32(x + 0x18E8);
    float* v = rz::g.v;
    float d[3];
    rz::Girar(c.Rf(w + 0x50), c.Rf(w + 0x58), v, d);
    if (fl & 1) { c.Wf(x + 0x40, c.Rf(x + 0x40) + d[0]); c.Wf(x + 0x48, c.Rf(x + 0x48) + d[2]); v[0] = 0.f; v[2] = 0.f; }
    if (fl & 2) { c.Wf(x + 0x44, c.Rf(x + 0x44) + d[1]); v[1] = 0.f; }
    if (fl & 4) { c.Wf(x + 0x44, c.Rf(x + 0x44) + d[1]); v[1] = v[1] - d[1]; }
    else if (fl & 8) v[1] = c.Rf(c.R32(x + 0xBB8) + 0xC);
    c.W32(x + 0x18E8, 0);
    ancla(c);
  };
  amb.Puente(kCamLoc, [&](Ee& c) {
    const uint32_t x = uint32_t(c.r[4]) - 0x640;
    if (x == kBub && rz::g.ok) { ancla(c); return; }
    const bool v = (c.R32(x + 0x2060) >> 8 & 0xFF) != 0;
    c.Wf(x + 0x640, c.Rf(x + 0x40)); c.Wf(x + 0x644, c.Rf(x + 0x44) + (v ? 0.97f : 9.2f)); c.Wf(x + 0x648, c.Rf(x + 0x48));
    c.Wf(x + 0x64C, 1.f);
  });
  // +0x1C = la Update de la tarea (a0; Piccolo: MgChar_UpdateMovement, no la MgObj_UpdateTrajectory de Bubbles)
  amb.Puente(kCrear, [&](Ee& c) {
    c.Cero(kTareas, 0x100); c.W32(kTareas + 0x1C, uint32_t(c.r[4])); ev.push_back(0x7A000000u); c.r[2] = kTareas;
  });
  amb.Puente(kVoz, [&](Ee& c) { out.voz = int(c.r[5] & 0xFFFF); ev.push_back(0x7B000000u | uint32_t(out.voz)); });
  amb.Puente(kBones, [&](Ee& c) {  // ClipNuevo con el largo del clip (= el de Bubbles); la pose la pone B3
    const uint32_t x = uint32_t(c.r[4]), cod = c.R32(x + 0xB60), n = Largo7DD(cod);
    ClipNuevo(c, x, int32_t(n));
    // MgChar_PoseTickRise lee los cuadros del clip en el banco del modelo (+0xB78 +0x46C, entrada +0xC64): banco propio
    c.W32(x + 0xC64, 0x10000u); c.W32(kModelo + 0x708, n);
    ev.push_back(cod);
    if (x == kBub && rdat) rz::Clip(*rdat, cod);
  });
  amb.Puente(kRaiz, [&](Ee& c) { raiz(c, uint32_t(c.r[4]), uint32_t(c.r[5])); });
  amb.Mudo({kGuion, kSonidoFz, kAura, kBorrar, kBotones, kPalanca, kManoBsk, kEventos});
  for (uint32_t a : {kIcono, kPlatano}) amb.Puente(a, [&](Ee& c) { sin_puente = true; c.r[2] = 0; });
  auto W8 = [&](uint32_t a, uint8_t v) {
    const uint32_t w = e.R32(a & ~3u), sh = (a & 3u) * 8u;
    e.W32(a & ~3u, (w & ~(0xFFu << sh)) | (uint32_t(v) << sh));
  };
  const uint32_t semilla_in = semilla;
  const Cronometro cr(e);
  bool ok = true;
  e.W32(kSemilla, semilla);
  if (in.inicio) {  // = persecucion.py Espejo.preparar() + la entrada del estado 13 (en el aire)
    e.Cero(kMgr, 0x9000); e.Cero(kObjs, 0x800); e.Cero(kCtx, 0x40); e.Cero(kTareas, 0x100); e.Cero(kMgw, 0x300);
    e.Cero(kCtxM, 0x800); e.Cero(kModelo, 0x1000); e.Cero(kPilaM, 0x1000); e.Cero(kClip, 0x40);
    e.W32(kPMgr, kMgr); e.W32(kPCtxD, kCtx); e.W32(kPMgw, kMgw); e.W32(kPObjs, kObjs); e.W32(kPCtxM, kCtxM);
    e.W32(kPPilaM, kPilaM); e.W32(kPPilaM + 4, 0); e.Wf(kPasoT, kPasoCampo); e.Wf(kMgr + 0x8B80, kPasoCampo);
    e.W32(kCtx + 4, 0xE);   // tipo de minijuego: Minigame_MovePlayerByStick da la palanca virtual en la embestida (9)
    e.W32(kBub + 0x1380, 1); e.W32(kBub + 0x1384, kJug); e.W32(kJug + 0x1384, kBub); e.Wf(kBub + 0xCAC, 1.f);
    e.W32(kBub + 0xB78, kModelo); e.W32(kModelo + 0x408, kMats); e.W32(kModelo + 0x46C, kModelo + 0x600);
    e.W32(kModelo + 0x614, kModelo + 0x700); e.W32(kBub + 0xB7C, kHueso7DD); e.W16(kHueso7DD, 1);
    e.W32(kMgw + 0x1E0, kColCtx); e.W32(kBub + 0xBB8, kParamsPic);
    // Hueso 1 de Piccolo (MgChar_IsTargetInFront): pose fija = identidad subida 1 en y. El original lleva la pose
    // animada; en las 380 muestras del dorado decide igual (persecucion.py puentes, v3).
    for (uint32_t i = 0; i < 16; ++i) e.W32(kMats + 0x40 + 4 * i, (i % 5 == 0 || i == 13) ? 0x3F800000u : 0u);
    // Instancia de animacion (actor+0x640) como Bubbles: autopuntero, descriptor a cero, velocidad 1, sin mezclas.
    e.W32(kBub + 0xBC0, kBub); e.W32(kBub + 0xB64, kClip); e.Wf(kBub + 0xAC4, 1.f); e.Wf(kBub + 0x18D8, 1.f);
    e.W32(kBub + 0xC84, 0xFFFFFFFFu); e.W32(kBub + 0xC8C, 0xFFFFFFFFu);
    for (int i = 0; i < 3; ++i) e.Wf(kBub + 0x40 + 4 * i, in.pic0[i]);
    e.W32(kBub + 0x4C, 0x3F800000u);
    e.W32(kW1 + 0x58, 0x3F800000u);  // frente (0,0,1) hasta el primer giro
    W8(kBub + 0x2061, 1);             // sale volando (estado 13 con +0x2061: clip 0x606)
    for (int i = 0; i < 3; ++i) e.Wf(kBub + 0x640 + 4 * i, in.pic0[i] + (i == 1 ? 0.97f : 0.f));
    e.Wf(kBub + 0x64C, 1.f);
    // Node_WrapPositionInGrid: rejilla del campo 906 (3000 x 3000 en 10 x 10 desde -1350, medida en el savestate)
    e.Cero(kGrid, 0xB00);
    e.W32(kPGrid, kGrid); e.W32(kGrid + 0x630, 7); e.Wf(kGrid + 0xAD4, 3000.f); e.Wf(kGrid + 0xAD8, 3000.f);
    e.W32(kGrid + 0xADC, 10); e.W32(kGrid + 0xAE0, 10); e.Wf(kGrid + 0xAF0, -1350.f); e.Wf(kGrid + 0xAF8, -1350.f);
    ok = e.Llamar(kEntrar7DD, {kBub, 13});
    if (rdat) { rz::Muestra(*rdat, e.Rf(kBub + 0xC68), rz::g.v); rz::g.ok = true; }   // vivo de partida: el clip de entrada
  }
  // Entradas del port y pegamento. p640/p770: ancla y punto del rayo de Piccolo sobre su raiz (medidos en el dorado:
  // +0,97 en el aire y +9,2 en el suelo; el punto, ~5,5 por encima del ancla). dist = |ancla jugador - ancla Piccolo|.
  float p640[3], p770[3];
  for (int i = 0; i < 3; ++i) p640[i] = p770[i] = e.Rf(kBub + 0x640 + 4 * i);
  p770[1] = p640[1] + 5.5f;
  const float dx = in.jug640[0] - p640[0], dy = in.jug640[1] - p640[1], dz = in.jug640[2] - p640[2];
  const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  const uint32_t fx = e.R32(kW1 + 0x50), fz = e.R32(kW1 + 0x58);
  const uint32_t mat[16] = {fz, 0, fx ^ 0x80000000u, 0, 0, 0x3F800000u, 0, 0, fx, 0, fz, 0,
                            e.R32(kBub + 0x40), e.R32(kBub + 0x44), e.R32(kBub + 0x48), 0x3F800000u};
  for (uint32_t i = 0; i < 16; ++i) e.W32(kBub + 4 * i, mat[i]);
  for (int i = 0; i < 3; ++i) {
    e.Wf(kBub + 0x770 + 4 * i, p770[i]);
    e.Wf(kJug + 0x40 + 4 * i, in.jug40[i]); e.Wf(kJug + 0x640 + 4 * i, in.jug640[i]);
  }
  e.Wf(kBub + 0x77C, 1.f); e.Wf(kJug + 0x4C, 1.f); e.Wf(kJug + 0x64C, 1.f);
  e.W16(kJug + 0xCBC, uint16_t(in.jcbc));
  e.Wf(kCtxM + 0x75C, dist); e.W16(kCtx + 0xC, uint16_t(in.dif)); e.W16(kMgw + 0x25A, uint16_t(in.tipo25a));
  e.W32(kObjs + 0x180, in.w180 ? 1u : 0u);
  e.Wf(kW1 + 0x1A0, e.Rf(kW1 + 0x1A0) + kPasoCampo); e.Wf(kW1 + 0x1A4, e.Rf(kW1 + 0x1A4) + kPasoCampo);
  const int codigo = int16_t(e.R16(kBub + 0xCBC));   // estado antes del tick (se registra)
  // Fase 1 (7/11): +0x50 = la raiz al EMPEZAR el cuadro (dorado del tick: +0x50 == +0x40 en A y C de las 272 parejas) y
  // la tarea mueve ANTES de la Update de pose (MgChar_PoseTickMoveTracked -> Minigame_RotatePlayerToMoveDir 0x299D80 gira
  // a Piccolo hacia +0x40 - +0x50). Antes +0x50 no se escribia y la Update iba primero: el frente salia (0,0,0) o "del
  // centro hacia fuera", Piccolo nunca tenia al jugador delante (MgChar_IsTargetInFront) y no lo alcanzaba (0 de 9
  // sesiones de 150 s; el original 3 de 3). tools/iw_mips/persecucion.py Espejo.raiz_previa, sintetica7dd.py.
  for (int i = 0; i < 4; ++i) e.W32(kBub + 0x50 + 4 * i, e.R32(kBub + 0x40 + 4 * i));
  ok = ok && e.Llamar(kEntradaJug, {kBub}) && e.Llamar(kTick7DD, {kBub});
  if (ok && e.R32(kW1 + 0xB8)) {   // la Update que la tarea guardo al crearse
    const uint32_t t = e.R32(kW1 + 0xB8), f = e.R32(t + 0x1C);
    ok = e.Llamar(f ? f : kTray, {t});
  }
  ok = ok && e.Llamar(kUpd7DD, {kBub});
  // ExplorationMinigameBodyAnimationsMerge 0x2AA0A0 tras las Update: AllAnimations (Animations2 sobre el hueso 1) y
  // CameraLocation(+0x640, 1) + Char_UpdateRootAndBones(actor, work), que consume el +0x18E8 que dejo la Update.
  if (ok && rz::g.ok && !(e.R32(kBub + 0xCCC) & 0xFF) && e.R32(kBub + 0xCAC) != 0) {
    const float paso = kPasoCampo * e.Rf(kBub + 0xCAC), u = e.Rf(kBub + 0xCB4), rate = e.Rf(kBub + 0xCB8);
    const float a = rz::Trunc(double(u) - 1), t = rz::Trunc(double(a) * a);
    const float beta = paso > 0 ? std::clamp(float((1 - double(t)) * u + double(t) * u * paso), 0.f, 1.f) : 0.f;
    float m[3];
    rz::Muestra(*rdat, e.Rf(kBub + 0xC68), m);
    for (int i = 0; i < 3; ++i)
      if (beta == 1.f) rz::g.v[i] = m[i];
      else if (beta > 0) rz::g.v[i] = rz::Trunc(double(rz::g.v[i]) + (double(m[i]) - rz::g.v[i]) * beta);
    if (u < 1.f) e.Wf(kBub + 0xCB4, std::min(1.f, rz::Trunc(double(u) + double(rate) * paso)));
  }
  if (ok && rz::g.ok) { ancla(e); raiz(e, kBub, kW1); }
  const double us = cr.Us();
  if (!ok || sin_puente) {
    REXLOG_WARN("dbz3 iwmips persecucion: {} (t={}); este campo usa el port", ok ? "colision sin puente" : e.error(), in.t);
    return false;
  }
  out.codigo = codigo;
  out.estado = int16_t(e.R16(kBub + 0xCBC));
  out.clip = e.R32(kBub + 0xB60);
  out.tarea = e.R32(kW1 + 0xB8) != 0;
  out.vuela = (e.R32(kBub + 0x2060) >> 8 & 0xFF) != 0;
  for (int i = 0; i < 3; ++i) { out.p[i] = e.Rf(kBub + 0x40 + 4 * i); out.frente[i] = e.Rf(kW1 + 0x50 + 4 * i); }
  semilla = e.R32(kSemilla);
  st->Cuenta(us);
  if (log) {  // solo pruebas: tools/iw_mips/persecucion.py juego --log <dbz3.log> la reejecuta en ee.py
    std::string a, b;
    char t[32];
    for (const float* p : std::initializer_list<const float*>{in.pic0, in.jug40, in.jug640, p640, p770})
      for (int i = 0; i < 3; ++i) Hx(a, p[i]);
    Hx(a, dist);
    std::snprintf(t, sizeof t, "%X,%X,%X,%X", unsigned(in.jcbc) & 0xFFFF, unsigned(in.dif), in.tipo25a, unsigned(in.w180));
    a += t;
    std::snprintf(t, sizeof t, "%X,%X,%X,", unsigned(codigo & 0xFFFF), unsigned(uint16_t(out.estado)), out.clip); b += t;
    for (int i = 0; i < 3; ++i) Hx(b, out.p[i]);
    std::snprintf(t, sizeof t, "%X,%X,%X,%X", e.R32(kW1 + 0xB8) ? 1u : 0u, unsigned(e.R16(kTareas + 0x60)), unsigned(out.vuela),
                  e.R32(kW1 + 0x1A4)); b += t;
    REXLOG_INFO("dbz3 iwmips persecucion t={} ini={} seed_in={:08X} seed_out={:08X} in={} col={} out={} ev={} instr={} us={:.1f} "
                "media_us={:.2f} max_us={:.1f}",
                in.t, int(in.inicio), semilla_in, semilla, a, col.empty() ? "-" : col, b, Lista(ev), e.n - cr.n0, us,
                st->us_total / double(st->llamadas), st->us_max);
  }
  return true;
}

// ---------------- T2B (7/11) NB012 0x7DA: el minijuego entero con su logica original (iw_mips.h nb012) ----------------
// Funciones del ELF (EU; USA en MapaUsa con tools/dif_mapa_iw_usa.py pc_eu_a_usa, sin dorado USA todavia).
uint32_t kMainDA = 0x315010, kScrollDA = 0x315540, kMissDA = 0x315230, kResetDA = 0x315B40, kInitDA = 0x312070;
uint32_t kAudioPlay = 0x35EB60, kAudioStop = 0x35EA50, kUiAnim = 0x2ED8F0, kCamTimer = 0x2ED8A0, kCamF164 = 0x2ED8D0;
uint32_t kPoseDA = 0x311DF0, kEngine = 0x28AB00, kJadeo = 0x22FCB0, kReady = 0x381F80, kScr158 = 0x31E1F0;
uint32_t kEfectoFinDA = 0x313920, kResultDA = 0x312D90, kAnimResDA = 0x3130F0, kPremio = 0x28A020, kFlag4 = 0x28A300;
uint32_t kGolpeSnd = 0x240740, kTransSnd = 0x371E30, kVibra = 0x2EFA10, kQteUpd = 0x2EEFB0;
// Punteros globales que el original lee (trabajo, vibracion, actor, QTE, contexto de carga) y el ancho de la UI.
uint32_t kPWorkDA = 0x49CDC8, kPVibDA = 0x49CDD4, kPActor = 0x49CA70, kPQte = 0x49CD70, kPCdd0 = 0x49CDD0, kPAnchoUi = 0x49C85C,
         kFlancoDA = 0x4ADED2;
// Espejo propio: trabajo 0x114 B, vibracion, QTE, contexto, motor (+0x10 personaje), actor (+0x800 maquina de poses) y UI.
constexpr uint32_t kWDA = 0x1100000, kVDA = 0x1101000, kQDA = 0x1102000, kCdd0DA = 0x1103000, kEngDA = 0x1104000,
                   kADA = 0x1110000, kUiDA = 0x1120000, kNUiDA = 0x40;

bool ActivaDA() { return REXCVAR_GET(dbz3_iw_7da_original) && Listo() != nullptr; }

// Mg_UI_CreateObjects (EU 0x316060) para lo que la logica lee: objetos activos (+0x150 bit 0), iconos 1..10 caja +-16
// en (-16, 395) y el objetivo 0x20 caja +-28 en (ancho/2, 395). El resto de la UI la dibuja el port.
void UiDA(Ee& e) {
  e.Cero(kUiDA, kNUiDA * 400);
  for (uint32_t k = 0; k < 0x3D; ++k) e.W32(kUiDA + k * 400 + 0x150, 1);
  auto caja = [&](uint32_t k, float r, float x) {
    const uint32_t b = kUiDA + k * 400;
    e.Wf(b + 0x30, -r); e.Wf(b + 0x34, -r); e.Wf(b + 0x38, r); e.Wf(b + 0x3C, r);
    e.Wf(b + 0x20, x); e.Wf(b + 0x24, 395.f); e.Wf(b + 0x2C, 1.f);
  };
  for (uint32_t k = 1; k <= 10; ++k) caja(k, 16.f, -16.f);
  caja(0x20, 28.f, 300.f);
}

bool CampoDA(const nb012::Entrada& in, nb012::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7da_original)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = nb012::Salida{};
  Ambito amb(*st, kD7DA);
  if (!in.inicio && !amb.continua) {  // el espejo guarda la partida: si otro minijuego lo piso, se deja al port
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips nb012: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  auto ev = [&](char t, int n) { if (out.n_ev < 16) out.ev[out.n_ev++] = {t, n}; };
  auto v0 = [](Ee& c, uint32_t v) { V0(c, v); };
  amb.Puente(kQteUpd, [&](Ee& c) {  // DragonMissionQTEInputs: boton pulsado este campo (slot 0) o -1
    const uint32_t b = in.boton < 0 ? 0xFFFFFFFFu : uint32_t(in.boton);
    c.W32(kQDA, b); c.W32(kQDA + 4, b);
  });
  amb.Puente(kPoseDA, [&](Ee& c) {  // MgPly_SetPoseState(actor+0x800, n): la maquina de poses es del port
    const int n = int(c.r[5] & 0xFF);
    c.W16(kADA + 0x14BC, uint16_t(n)); c.W32(kADA + 0x1478, 0);
    ev('P', n);
  });
  bool cam_fin = in.cam_fin;   // un clip que empieza en este campo tiene su reloj a 0 (dorado T2B)
  amb.Puente(kUiAnim, [&](Ee& c) {   // UiAnim_Start(0, clip a1, play a2, ...) (medido en ee.py); play 0 -> n | 0x10000
    ev('C', int(c.r[5] & 0xFFFF) | ((c.r[6] & 0xFF) ? 0 : 0x10000));
    cam_fin = false;
  });
  amb.Puente(kCamTimer, [&](Ee& c) { v0(c, cam_fin ? 1u : 0u); });
  amb.Puente(kEngine, [&](Ee& c) { v0(c, kEngDA); });
  amb.Puente(kReady, [&](Ee& c) { ev('R', int(c.r[4] & 0xFF)); });
  amb.Puente(kResultDA, [&](Ee&) { ev('F', 0); });
  amb.Puente(kGolpeSnd, [&](Ee&) { ev('G', 0); });
  amb.Mudo({kAudioPlay, kAudioStop, kCamF164, kJadeo, kScr158, kEfectoFinDA, kAnimResDA, kPremio, kFlag4, kTransSnd, kVibra});
  if (in.inicio) {
    e.Cero(kWDA, 0x120); e.Cero(kVDA, 0x20); e.Cero(kQDA, 0x120); e.Cero(kCdd0DA, 0x20); e.Cero(kEngDA, 0x20);
    e.Cero(kADA, 0x2000);
    e.W32(kPWorkDA, kWDA); e.W32(kPVibDA, kVDA); e.W32(kPActor, kADA); e.W32(kPQte, kQDA); e.W32(kPCdd0, kCdd0DA);
    e.W32(kPUia, kUiDA); e.W32(kPNUia, kNUiDA);
    e.Wf(kPAnchoUi, 600.f);
    e.W16(kEngDA + 0x10, uint16_t(in.personaje));
    e.W8(kWDA + 0x14, uint8_t(in.dif));
    UiDA(e);
    if (!e.Llamar(kResetDA, {}) || !e.Llamar(kInitDA, {})) {
      REXLOG_WARN("dbz3 iwmips nb012: {} al preparar; se usa el port", e.error());
      st->dueno = kNadie;
      return false;
    }
    e.W32(kQDA + 0x110, 1);  // Mg_Start: QTE_SetEnabled(1)
    e.W32(kADA + 0x1474, 1);
  }
  e.Wf(kPasoT, kPasoCampo);
  e.W32(kSemilla, semilla);
  e.W16(kFlancoDA, in.flanco);
  e.W16(kADA + 0x14BC, uint16_t(in.pose)); e.W32(kADA + 0x1478, uint32_t(in.pose_t));
  const Cronometro cr(e);
  // orden medido en PCSX2 (dorado T2B): la tarea de Mg_UpdateFrame (iconos, cadena del fallo) va antes que Mg_UpdateMain
  const bool ok = e.Llamar(kScrollDA, {}) && e.Llamar(kMissDA, {}) && e.Llamar(kMainDA, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips nb012: {} (t={}); la partida sigue con el port", e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  out.fase = int(e.R32(kWDA)); out.sub = int(e.R32(kWDA + 4)); out.reloj = e.Rf(kWDA + 8); out.paso = int(e.R32(kWDA + 0xC));
  out.ronda = int(int32_t(e.R32(kWDA + 0x54))); out.actual = int(e.R32(kWDA + 0x5C)); out.juicio = int(e.R32(kWDA + 0xB4));
  out.botones = int(e.R32(kWDA + 0xB8)); out.nota = e.Rf(kWDA + 0x18); out.total = e.Rf(kWDA + 0x44);
  out.rango = e.R8(kWDA + 0x88);
  for (int i = 0; i < 10; ++i) {
    out.ronda_pts[i] = e.Rf(kWDA + 0x1C + 4 * uint32_t(i));
    out.x[i] = e.Rf(kUiDA + uint32_t(i + 1) * 400 + 0x20);
    out.vivo[i] = e.R8(kWDA + 0x89 + uint32_t(i)); out.fallado[i] = e.R8(kWDA + 0x9D + uint32_t(i));
    out.acierto[i] = e.R8(kWDA + 0x60 + uint32_t(i)); out.seq[i] = e.R8(kQDA + 0x28 + uint32_t(i));
  }
  for (int i = 0; i < 3; ++i) out.cat[i] = e.Rf(kWDA + 0x48 + 4 * uint32_t(i));
  st->Cuenta(us);
  if (log) {  // solo pruebas: E:/DBZ3HD_trabajo/T2B/qte7da.py juego --log <dbz3.log> la reejecuta en ee.py
    std::string w, x;
    for (uint32_t i = 0; i < 0x114 / 4; ++i) Hx(w, e.R32(kWDA + 4 * i));
    for (int i = 0; i < 10; ++i) Hx(x, out.x[i]);
    std::string evs;
    for (int i = 0; i < out.n_ev; ++i) { char t[16]; std::snprintf(t, sizeof t, "%c%d,", out.ev[i].tipo, out.ev[i].n); evs += t; }
    REXLOG_INFO("dbz3 iwmips nb012 t={} ini={} dif={} pj={} seed_in={:08X} seed_out={:08X} in={},{},{},{},{},{:X} w={} x={} ev={} "
                "instr={} us={:.1f}",
                in.t, int(in.inicio), in.dif, in.personaje, semilla_in, semilla, in.boton, int(in.cam_fin), in.pose, in.pose_t,
                int(in.pose_act), in.flanco, w, x, evs.empty() ? "-" : evs, e.n - cr.n0, us);
  }
  return true;
}

// ---------------- T2B (7/11) NB014 0x7DC: el minijuego entero con su logica original (iw_mips.h nb014) ----------------
uint32_t kUpdDC = 0x331EC0, kJuegoDC = 0x332CA0, kResetDC = 0x333080, kVelDC = 0x32EB30, k354DC = 0x32EA90, kStatsDC = 0x32E8D0;
uint32_t kPose1DC = 0x336490, kPose2DC = 0x3387F0, kUiModo = 0x2EDAE0, kUiTrans = 0x2EDB50, kCam170 = 0x2ED890, kCamVista = 0x2ED7E0,
         kCamUpd = 0x2EE340, kUiUpd = 0x2F1190, kFadeH = 0x31E880, kFadeR = 0x31E5A0, kFadeU = 0x31E3D0, kEfecto = 0x2F7950,
         kChrFade = 0x2EF640, kVidaSnd = 0x3722D0, kFinalDC = 0x32F0F0, kMmi2 = 0x11DEF8, kCfg = 0x28AA40, kCda4 = 0x2F5F30;
uint32_t kPWorkDC = 0x49CDF8, kPCe00 = 0x49CE00, kPCe04 = 0x49CE04, kPJug = 0x49CE08, kPRiv = 0x49CE0C;
constexpr uint32_t kWDC = 0x1140000, kCe00DC = 0x1141000, kCe04DC = 0x1141100, kMDC = 0x1150000, kCda4DC = 0x1142000;

bool ActivaDC() { return REXCVAR_GET(dbz3_iw_7dc_original) && Listo() != nullptr; }

bool CampoDC(const nb014::Entrada& in, nb014::Salida& out, uint32_t& semilla) {
  if (!REXCVAR_GET(dbz3_iw_7dc_original)) return false;
  Estado* const st = Listo();
  if (!st) return false;
  Ee& e = st->ee;
  const bool log = st->log || std::getenv("DBZ3_TEST_IWMINI") != nullptr;
  out = nb014::Salida{};
  Ambito amb(*st, kD7DC);
  if (!in.inicio && !amb.continua) {
    st->dueno = kNadie;
    REXLOG_WARN("dbz3 iwmips nb014: el espejo lo uso otro minijuego (t={}); este campo usa el port", in.t);
    return false;
  }
  auto ev = [&](char t, int n) { if (out.n_ev < 16) out.ev[out.n_ev++] = {t, n}; };
  // QTE_Update corre del ELF: DragonMissionQTEInputs lee los globales del mando (apretados 0x4ADECC, nuevos 0x4ADECE);
  // el combo (QTE_HasAnyInput / QTE_CheckHeldMatch) mira los apretados. Medido contra PCSX2: 2385/2386 campos iguales.
  amb.Puente(kPose2DC, [&](Ee& c) { ev('J', int(c.r[5] & 0xFF)); });   // Char_SetPoseState_P2(jugador, n)
  amb.Puente(kPose1DC, [&](Ee& c) { ev('R', int(c.r[5] & 0xFF)); });   // Char_SetPoseState(rival, n)
  bool cam_fin = in.cam_fin;   // un clip que empieza en este campo tiene su reloj a 0 (dorado T2B)
  amb.Puente(kUiAnim, [&](Ee& c) { ev('C', int(c.r[5] & 0xFFFF) | ((c.r[6] & 0xFF) ? 0 : 0x10000)); cam_fin = false; });
  amb.Puente(kCamTimer, [&](Ee& c) { V0(c, cam_fin ? 1u : 0u); });
  amb.Puente(kEngine, [&](Ee& c) { V0(c, kEngDA); });
  amb.Puente(kCfg, [&](Ee& c) { V0(c, kEngDA + 0x100); });
  amb.Puente(kCda4, [&](Ee& c) { V0(c, kCda4DC); });
  amb.Puente(kVidaSnd, [&](Ee&) { ev('V', 0); });
  amb.Mudo({kUiModo, kUiTrans, kCam170, kCamVista, kCamUpd, kUiUpd, kFadeH, kFadeR, kFadeU, kEfecto, kChrFade, kAudioPlay,
            kAudioStop, kJadeo, kVibra, kPremio, kFlag4, kScr158, kReady, kMmi2, kFinalDC, kGolpeSnd});
  if (in.inicio) {
    e.Cero(kWDC, 0x400); e.Cero(kCe00DC, 0x200); e.Cero(kQDA, 0x120); e.Cero(kEngDA, 0x200); e.Cero(kMDC, 0x5000);
    e.Cero(kCda4DC, 0x100);
    e.W32(kPWorkDC, kWDC); e.W32(kPCe00, kCe00DC); e.W32(kPCe04, kCe04DC); e.W32(kPActor, kMDC);
    e.W32(kPJug, kMDC + 0x800); e.W32(kPRiv, kMDC + 0x28D0); e.W32(kPQte, kQDA);
    e.W32(kPUia, kUiDA); e.W32(kPNUia, 0x60);
    e.Wf(kPAnchoUi, 600.f);
    e.Cero(kUiDA, 0x60 * 400);
    for (uint32_t k = 0; k < 0x60; ++k) {
      const uint32_t b = kUiDA + k * 400;
      e.W32(b + 0x150, 1);
      e.Wf(b + 0x30, -16.f); e.Wf(b + 0x34, -16.f); e.Wf(b + 0x38, 16.f); e.Wf(b + 0x3C, 16.f); e.Wf(b + 0x2C, 1.f);
    }
    // objetivos de los carriles (Mg_SetupUiObjects): objetos 1 y 2 en (ancho/2 + 150, 366 / 409), caja (-28, -28, 7, 28)
    for (uint32_t l = 0; l < 2; ++l) {
      const uint32_t b = kUiDA + (1 + l) * 400;
      e.Wf(b + 0x20, 450.f); e.Wf(b + 0x24, 366.f + 43.f * float(l));
      e.Wf(b + 0x30, -28.f); e.Wf(b + 0x34, -28.f); e.Wf(b + 0x38, 7.f); e.Wf(b + 0x3C, 28.f);
    }
    // s_MSN_NB014_Updata (sin sus tareas): QteMg_ResetState, umbrales del rango y parametros por dificultad (+0x18)
    bool ok = e.Llamar(kResetDC, {});
    constexpr uint32_t kUmbr[11] = {0x5F, 100, 0x51, 0x5E, 0x45, 0x50, 0x38, 0x44, 1, 0x37, 0};
    for (uint32_t i = 0; i < 11; ++i) e.W32(kWDC + 0x310 + 4 * i, kUmbr[i]);
    e.W32(kWDC + 0x18, uint32_t(in.dif));
    ok = ok && e.Llamar(kVelDC, {}) && e.Llamar(k354DC, {}) && e.Llamar(kStatsDC, {});
    if (!ok) {
      REXLOG_WARN("dbz3 iwmips nb014: {} al preparar; se usa el port", e.error());
      st->dueno = kNadie;
      return false;
    }
    e.W32(kQDA + 0x110, 1);
  }
  e.Wf(kPasoT, kPasoCampo);
  e.W32(kSemilla, semilla);
  {
    const uint16_t nuevo = in.boton < 0 ? 0 : uint16_t(1u << in.boton);
    e.W16(kFlancoDA, uint16_t(in.flanco | nuevo));
    e.W16(kFlancoDA - 6, uint16_t(in.mantenido | nuevo));   // 0x4ADECC
    e.W16(kFlancoDA - 4, nuevo);                            // 0x4ADECE
  }
  const Cronometro cr(e);
  // orden medido en PCSX2 (dorado T2B): la tarea de juego (QteMg_UpdateGameplay) va antes que la de fases (QteMg_Update)
  const bool ok = e.Llamar(kJuegoDC, {}) && e.Llamar(kUpdDC, {});
  const double us = cr.Us();
  if (!ok) {
    REXLOG_WARN("dbz3 iwmips nb014: {} (t={}); la partida sigue con el port", e.error(), in.t);
    st->dueno = kNadie;
    return false;
  }
  const uint32_t semilla_in = semilla;
  semilla = e.R32(kSemilla);
  out.fase = int(e.R32(kWDC + 4)); out.sub = int(e.R32(kWDC + 8)); out.paso = int(e.R32(kWDC + 0x10));
  out.ronda = int(int32_t(e.R32(kWDC + 0x24))); out.activo = int(e.R32(kWDC + 0x2E0));
  out.rango = int(e.R32(kWDC + 0x1C)); out.nota = e.Rf(kWDC + 0x20);
  out.vida[0] = e.Rf(kMDC + 0x800 + 0x1850); out.vida[1] = e.Rf(kMDC + 0x28D0 + 0x1850);
  for (int l = 0; l < 2; ++l) {   // QteMg_UpdatePlayerButtons: el icono k va en el objeto 0x15 + 15 l + k % 15
    out.aciertos[l] = int(e.R32(kWDC + 0x3C + 4 * uint32_t(l)));
    out.llegaron[l] = std::min(50, int(e.R32(kWDC + 0x2C + 4 * uint32_t(l))));
    const int cnt = int(e.R32(kWDC + 0x2C8 + 4 * uint32_t(l)));
    for (int k = std::max(0, cnt - 15); k < cnt && k < 50; ++k) {
      const uint32_t f = kWDC + uint32_t(l) * 100 + uint32_t(k);
      nb014::Icono& ic = out.ic[l][out.n_ic[l]++];
      ic.k = k;
      ic.boton = e.R8(kQDA + 0x28 + uint32_t(l) * 100 + uint32_t(k));
      ic.x = e.Rf(kUiDA + uint32_t(k % 15 + l * 15 + 0x15) * 400 + 0x20);
      ic.vivo = e.R8(f + 0x130) != 0;
      ic.fallado = e.R8(f + 0x1F8) != 0;
    }
  }
  st->Cuenta(us);
  if (log) {  // solo pruebas: E:/DBZ3HD_trabajo/T2B/qte7dc.py juego <dbz3.log> la reejecuta en ee.py
    std::string w;
    for (uint32_t i = 0; i < 0x360 / 4; ++i) Hx(w, e.R32(kWDC + 4 * i));
    std::string evs;
    for (int i = 0; i < out.n_ev; ++i) { char t[16]; std::snprintf(t, sizeof t, "%c%d,", out.ev[i].tipo, out.ev[i].n); evs += t; }
    REXLOG_INFO("dbz3 iwmips nb014 t={} ini={} dif={} seed_in={:08X} seed_out={:08X} in={},{},{:X},{:X} vida={:X},{:X} w={} ev={} "
                "instr={} us={:.1f}",
                in.t, int(in.inicio), in.dif, semilla_in, semilla, in.boton, int(in.cam_fin), in.flanco, in.mantenido,
                e.R32(kMDC + 0x800 + 0x1850), e.R32(kMDC + 0x28D0 + 0x1850), w, evs.empty() ? "-" : evs, e.n - cr.n0, us);
  }
  return true;
}

// dia1E5: SLUS_218.42 (IW USA). Mismo codigo que el EU salvo direcciones: los 429 arboles de llamada de las 10 entradas
// emparejados instruccion a instruccion con los inmediatos de direccion enmascarados, 0 diferencias de constantes ni de
// campos (D:/tmp/dia1E5/grafo.py). Funciones: la secuencia de cada una cerca de pc_eu_a_usa() y el destino de sus jal.
// Datos absolutos: EU - 0x800 (pares lui+lo del arbol). gp = 0x4A35F0 (crt0, EU 0x4A3DF0), y los gp-relativos se miden en
// las instrucciones emparejadas: 0x49C7xx-0x49CAxx -0x800, 0x49CBxx -0x810, 0x49CDxx -0x820, 0x49CExx -0x81C.
// NTSC: fRam0049c7dc = 1,0 y 60 campos/s para Freezer y Piccolo (freezer::g_ntsc). Bubbles sigue el paso de su Maquina (1,2).
void MapaUsa() {
  kGp = 0x4A35F0; kSemilla = kGp - 0x7FE8; kPMgr = kGp - 0x7380;
  kIA = 0x31A650; kUpdate = 0x31AF70; kTray = 0x31A280; kCrearIA = 0x316FF0; kTick = 0x31AF50;
  kObjsA = 0x29A340; kObjsB = 0x318110; kCtxF = 0x28A7A0; kCrear = 0x140300; kVoz = 0x22FBB0; kBoca = 0x207060;
  kPrintf = 0x127DE0; kIcono = 0x317880; kBones = 0x2384E0; kRaiz = 0x2F5B90; kPlatano = 0x317500; kHitbox = 0x240260;
  kGetWork = 0x296C90; kBorrar = 0x140460; kSweep = 0x2E7430; kRay = 0x2E75C0; kCamLoc = 0x208370;
  kTrans = 0x35E090; kSonidoFz = 0x2405B0; kHueso = 0x2EF490; kPantalla = 0x2EF660;
  kMemoryFn = 0x127CA0; kRespawn = 0x2EADF0; kRotPc = 0x32D950; kInitPc = 0x3276E0;
  kEntrar = 0x498D60; kGlobFz = 0x5D9560; kTablaFz = 0x5D9348; kGlobPc = 0x5D9140;
  kPasos19D = 0x5D8962; kAnchos19D = 0x5D8964; kY19D = 0x5D88D8;
  kPasoT = 0x49BFDC; kPAncho = 0x49C04C; kPAlto = 0x49C050; kPMgw = 0x49C3A8; kPObjs = 0x49C3B0;
  kPCtx19 = 0x49C548; kPCam = 0x49C54C; kPUia = 0x49C56C; kPNUia = 0x49C570; kPGrid = 0x49C598; kPVctx = 0x49C5D0;
  kPView = 0x49C65C;
  kMarcaFn = 0x31D600; kMarcasD = 0x5D8D00;   // N2: tools/dif_mapa_iw_usa.py pc_eu_a_usa / usa_datos
  // P1 (7/11): pc_eu_a_usa comprobado instruccion a instruccion; los tres punteros, en los lw gp-relativos de USA
  kCpu7DD = 0x363E50; kEntrar7DD = 0x363FD0; kPCtxD = 0x49C394; kPCtxM = 0x49C5A0; kPPilaM = 0x49BF00;
  kParamsPic = 0x44E2D0;   // la misma tabla de 128 B, unica en el ELF USA
  kEntradaJug = 0x299F40; kTick7DD = 0x363FB0; kUpd7DD = 0x363F90; kBotones = 0x299290; kPalanca = 0x298FC0; kManoBsk = 0x23F650; kEventos = 0x23F390;
  // T2B: NB012 (pc_eu_a_usa / usa_datos; Main, Reset, jadeo y AnimRes emparejados a mano: pc_eu_a_usa cae 0x20-0x70 antes)
  kMainDA = 0x314D50; kScrollDA = 0x315280; kMissDA = 0x314F70; kResetDA = 0x315850; kInitDA = 0x311E00;
  kAudioPlay = 0x35ECA0; kAudioStop = 0x35EB90; kUiAnim = 0x2ED6E0; kCamTimer = 0x2ED690; kCamF164 = 0x2ED6C0;
  kPoseDA = 0x311B80; kEngine = 0x28A8D0; kJadeo = 0x22FB40; kReady = 0x381F50; kScr158 = 0x31DF00;
  kEfectoFinDA = 0x313680; kResultDA = 0x312B20; kAnimResDA = 0x312E50; kPremio = 0x289DF0; kFlag4 = 0x28A0D0;
  kGolpeSnd = 0x2405B0; kTransSnd = 0x371F60; kVibra = 0x2EF800; kQteUpd = 0x2EEDA0;
  kPWorkDA = 0x49C5A8; kPVibDA = 0x49C5B4; kPActor = 0x49C270; kPQte = 0x49C550; kPCdd0 = 0x49C5B0; kPAnchoUi = 0x49C05C;
  kFlancoDA = 0x4AD6D2;
  // T2B: NB014 (pc_eu_a_usa; Gameplay y Reset emparejados a mano)
  kUpdDC = 0x331AC0; kJuegoDC = 0x332850; kResetDC = 0x332BA0; kVelDC = 0x32E800; k354DC = 0x32E760; kStatsDC = 0x32E5A0;
  kPose1DC = 0x335FB0; kPose2DC = 0x338310; kUiModo = 0x2ED8D0; kUiTrans = 0x2ED940; kCam170 = 0x2ED680; kCamVista = 0x2ED5D0;
  kCamUpd = 0x2EE130; kUiUpd = 0x2F0F80; kFadeH = 0x31E590; kFadeR = 0x31E2B0; kFadeU = 0x31E0E0; kEfecto = 0x2F7740;
  kChrFade = 0x2EF430; kVidaSnd = 0x372400; kFinalDC = 0x32EDC0; kMmi2 = 0x11DEF8; kCfg = 0x28A810; kCda4 = 0x2F5D20;
  kPWorkDC = 0x49C5D8; kPCe00 = 0x49C5E0; kPCe04 = 0x49C5E4; kPJug = 0x49C5E8; kPRiv = 0x49C5EC;   // -0x820: los lw de gp de QteMg_Update USA (usa_datos da -0x81C)
  kPasoCampo = 1.0f;
  freezer::g_ntsc = true;
}

const bool kRegistrado =
    (g_ia = &IA, g_update = &Update, g_trayectoria = &Trayectoria, g_tarea5 = &Tarea5, g_tick = &Tick,
     freezer::g_activa = &ActivaFz, freezer::g_esquiva = &Esquiva, msn019::g_fila = &Fila19,
     piccolo::g_esquiva = &EsquivaPc, cell::g_marcas = &Marcas7DE, persecucion::g_tick = &Tick7DD, persecucion::g_activa = &Activa7DD,
     nb012::g_campo = &CampoDA, nb012::g_activa = &ActivaDA, nb014::g_campo = &CampoDC, nb014::g_activa = &ActivaDC,
     true);   // dia1E4: + Piccolo; N2: + Cell; P1: + 0x7DD
}  // namespace
}  // namespace bubbles
}  // namespace dbz3::iwmips

// tanda3 iw-botones: 7DB/7D4/7D5 con su logica original (cvars, declaraciones y puentes en el .inl)
#include "iw_mips_botones.inl"

// tanda3 iw-vuelo: 0x7D3 (ladrillos, motor MgFly) con su logica original
#include "iw_mips_vuelo.inl"

// tanda3 iw-campo: MSNMW (0x7D0/7D2/7D9/7DF) con su logica original (cvar, declaraciones y puente en el .inl)
#include "iw_mips_campo.inl"

// tanda4 iw-mapa: el mapa de Dragon Mission / Fighter's Road con su logica original (cvar dbz3_iw_mapa_original, apagada)
#include "iw_mips_mapa.inl"
