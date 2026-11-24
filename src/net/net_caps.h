// dbz3 - Versioned capability handshake for cross-executable online compatibility (BETA).
// Pure, header-only contract (no SDK/OS socket deps): usable by netplay.cpp, matchmaking.inl, and harnesses.
#pragma once

#include "team_wire.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dbz3::net::caps {

// Online mode is always BETA; capability compatibility never clears BETA status.
inline constexpr bool kOnlineAlwaysBeta = true;

// Base v7 HELLO is 100 bytes. When dbz3_net_compat_caps is enabled, a 28-byte
// versioned capability trailer is appended at offset 100 (total 128 bytes).
inline constexpr size_t kHelloBaseSize = 100;
inline constexpr size_t kTrailerSize = 28;
inline constexpr size_t kHelloCapSize = kHelloBaseSize + kTrailerSize;
inline constexpr size_t kRejectBaseSize = 10;
inline constexpr size_t kRejectOsSize = 18;
inline constexpr size_t kRejectExtSize = 26;

inline constexpr std::array<uint8_t, 4> kMagic{'C', 'A', 'P', 'S'};
inline constexpr uint8_t kSchemaVersion = 1;
inline constexpr uint8_t kProtoMin = 7;
inline constexpr uint8_t kProtoMax = 8;

// Bump kCurrentSimAbi whenever deterministic online simulation behavior changes
// (guest recompilation, tick/input order, RNG formula, CombatRules, Tag Team relay timing, roster pin order).
inline constexpr uint16_t kCurrentSimAbi = 1;
// Bump kCurrentContentSchema whenever ModFingerprints() inclusion/exclusion rules change.
inline constexpr uint32_t kCurrentContentSchema = 1;

// Platform / CPU / CRT ABI domain: lockstep floating-point and memory layout
// require matching platform ABI unless a future cross-platform proof exists.
enum class PlatformAbi : uint8_t {
  kUnknown = 0,
  kWinX64 = 1,
  kLinuxX64 = 2,
  kAndroidArm64 = 3,
  kWinArm64 = 4,
  kLinuxArm64 = 5,
};

constexpr PlatformAbi LocalPlatformAbi() {
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
  return PlatformAbi::kWinArm64;
#elif defined(_M_X64) || defined(__x86_64__)
  return PlatformAbi::kWinX64;
#else
  return PlatformAbi::kUnknown;
#endif
#elif defined(__ANDROID__)
#if defined(__aarch64__)
  return PlatformAbi::kAndroidArm64;
#else
  return PlatformAbi::kUnknown;
#endif
#elif defined(__linux__)
#if defined(__aarch64__)
  return PlatformAbi::kLinuxArm64;
#elif defined(__x86_64__)
  return PlatformAbi::kLinuxX64;
#else
  return PlatformAbi::kUnknown;
#endif
#else
  return PlatformAbi::kUnknown;
#endif
}

constexpr bool ValidPlatformAbi(uint8_t v) {
  return v >= uint8_t(PlatformAbi::kWinX64) && v <= uint8_t(PlatformAbi::kLinuxArm64);
}

inline bool PlatformMatchesOsTag(PlatformAbi abi, const std::string& os_tag) {
  switch (abi) {
    case PlatformAbi::kWinX64:
    case PlatformAbi::kWinArm64:
      return os_tag == "windows";
    case PlatformAbi::kLinuxX64:
    case PlatformAbi::kLinuxArm64:
      return os_tag == "linux";
    case PlatformAbi::kAndroidArm64:
      return os_tag == "linux" || os_tag.empty();
    default:
      return false;
  }
}

// Descriptor flags (byte 111 of 128-byte HELLO).
inline constexpr uint8_t kFlagBeta = 1u << 0;             // Mandatory: online is always BETA
inline constexpr uint8_t kFlagAllowCrossBuild = 1u << 1;  // Explicit opt-in to connect different executables
inline constexpr uint8_t kKnownFlagsMask = kFlagBeta | kFlagAllowCrossBuild;

// Versioned capability bitmask (u32 LE).
// Unknown bits in supported_caps are cleared in intersection (forward compatibility).
// Unknown bits in required_caps fail admission cleanly with RejectCode::kMissingCaps.
inline constexpr uint32_t kCapLockstepV1 = 1u << 0;        // Core 1v1 lockstep + RNG sync + BattleBarrier
inline constexpr uint32_t kCapTagTeam2v2 = 1u << 1;        // Tag Team 2v2 relay + kTeam custom capsules
inline constexpr uint32_t kCapTeamIdV8 = 1u << 2;          // LIM1 32-bit logical roster IDs (proto v8 on kTeam/kStart)
inline constexpr uint32_t kCapCombatRulesV1 = 1u << 3;     // kRoom CombatRules (flags + reversion)
inline constexpr uint32_t kCapVanillaNegotiate = 1u << 4;  // kVanillaOffer mutual vanilla relaunch
inline constexpr uint32_t kCapRollbackUsV1 = 1u << 5;      // Experimental rollback (same-exe US only; forced off cross-build)
inline constexpr uint32_t kCapF10StrictVal = 1u << 6;      // F10 strict validation guards
inline constexpr uint32_t kKnownCapsMask =
    kCapLockstepV1 | kCapTagTeam2v2 | kCapTeamIdV8 | kCapCombatRulesV1 |
    kCapVanillaNegotiate | kCapRollbackUsV1 | kCapF10StrictVal;

// Compile-time FNV-1a fingerprint of the core lockstep simulation constants.
constexpr uint32_t Mix32(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xFFu;
    h *= 16777619u;
  }
  return h;
}

constexpr uint32_t ComputeSimContractHash(
    uint16_t sim_abi = kCurrentSimAbi,
    uint32_t ui_snap_size = 0x100u,
    uint32_t ui_snap_us = 0x824BA110u,
    uint32_t ui_snap_eu = 0x824BA100u,
    uint32_t ring_size = 256u,
    uint32_t rng_addr_us = 0x82310670u,
    uint32_t max_delay = 10u,
    uint32_t base_roster_slots = 64u,
    uint32_t custom_slots = 38u) {
  uint32_t h = 2166136261u;
  h = Mix32(h, sim_abi);
  h = Mix32(h, ui_snap_size);
  h = Mix32(h, ui_snap_us);
  h = Mix32(h, ui_snap_eu);
  h = Mix32(h, ring_size);
  h = Mix32(h, rng_addr_us);
  h = Mix32(h, max_delay);
  h = Mix32(h, base_roster_slots);
  h = Mix32(h, custom_slots);
  return h;
}

inline constexpr uint32_t kCurrentSimContractHash = ComputeSimContractHash();

struct Descriptor {
  uint8_t schema = kSchemaVersion;
  uint8_t proto_min = kProtoMin;
  uint8_t proto_max = kProtoMax;
  uint16_t sim_abi = kCurrentSimAbi;
  PlatformAbi platform_abi = LocalPlatformAbi();
  uint8_t flags = kFlagBeta;
  uint32_t supported_caps = kCapLockstepV1 | kCapCombatRulesV1 | kCapVanillaNegotiate;
  uint32_t required_caps = kCapLockstepV1;
  uint32_t sim_contract = kCurrentSimContractHash;
  uint32_t content_schema = kCurrentContentSchema;

  bool operator==(const Descriptor& o) const = default;
};

enum class RejectCode : uint8_t {
  kNone = 0,
  kBuild = 1,          // Different binary in strict mode (no capability trailer)
  kPlatformAbi = 2,    // Different OS / CPU ABI
  kSimAbi = 3,         // Incompatible simulation determinism epoch
  kSimContract = 4,    // Lockstep simulation constant/layout contract mismatch
  kContentSchema = 5,  // Mod fingerprint schema mismatch
  kProtocol = 6,       // Disjoint protocol range or unsupported capability schema
  kMissingCaps = 7,    // Required capability bit(s) unsupported by peer
  kStrictPolicy = 8,   // Peer requires exact binary match (compat_caps disabled)
  kBetaRequired = 9,   // Peer descriptor missing mandatory BETA flag
  kMalformed = 10,     // Structural tampering / invalid packet (drop silently)
};

struct RejectExt {
  RejectCode code = RejectCode::kMalformed;
  uint8_t host_platform_abi = 0;
  uint16_t host_sim_abi = 0;
  uint32_t missing_caps = 0;
};

struct Admission {
  bool admitted = false;
  bool cross_build = false;
  bool rollback_allowed = false;
  uint8_t negotiated_proto = kProtoMin;
  uint32_t agreed_caps = 0;
  team_wire::Version team_wire = team_wire::Version::kLegacy;
  RejectCode reject_code = RejectCode::kNone;
  uint32_t missing_caps = 0;
};

inline bool IsHex32(const std::string& s) {
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

inline Descriptor MakeLocalDescriptor(bool allow_cross_build, bool safe_tagteam, bool wide_team_offer,
                                      const std::vector<int>& local_team, bool f10_strict,
                                      bool rollback_requested) {
  Descriptor d{};
  d.schema = kSchemaVersion;
  d.proto_min = kProtoMin;
  d.proto_max = wide_team_offer ? uint8_t(8) : uint8_t(7);
  d.sim_abi = kCurrentSimAbi;
  d.platform_abi = LocalPlatformAbi();
  d.flags = uint8_t(kFlagBeta | (allow_cross_build ? kFlagAllowCrossBuild : 0u));
  d.supported_caps = kCapLockstepV1 | kCapCombatRulesV1 | kCapVanillaNegotiate;
  if (safe_tagteam) d.supported_caps |= kCapTagTeam2v2;
  if (wide_team_offer) d.supported_caps |= kCapTeamIdV8;
  if (rollback_requested) d.supported_caps |= kCapRollbackUsV1;
  if (f10_strict) d.supported_caps |= kCapF10StrictVal;
  d.required_caps = kCapLockstepV1;
  if (local_team.size() >= 2 && safe_tagteam) d.required_caps |= kCapTagTeam2v2;
  const bool needs_wide = std::any_of(local_team.begin(), local_team.end(),
                                      [](int id) { return id < 0 || uint32_t(id) > uint32_t(UINT16_MAX); });
  if (needs_wide && wide_team_offer) d.required_caps |= kCapTeamIdV8;
  d.sim_contract = kCurrentSimContractHash;
  d.content_schema = kCurrentContentSchema;
  return d;
}

inline void PutU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v & 0xFFu));
  out.push_back(uint8_t((v >> 8) & 0xFFu));
}

inline void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t((v >> (8 * i)) & 0xFFu));
}

inline uint16_t GetU16(const uint8_t* p) {
  return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

inline uint32_t GetU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Appends the 28-byte capability trailer onto a 100-byte v7 HELLO/HELLO_ACK.
inline bool AppendHelloTrailer(std::vector<uint8_t>& hello, const Descriptor& d) {
  if (hello.size() != kHelloBaseSize) return false;
  if (d.schema != kSchemaVersion || d.proto_min < kProtoMin || d.proto_min > d.proto_max) return false;
  if (!ValidPlatformAbi(uint8_t(d.platform_abi)) || d.sim_abi == 0 || d.content_schema == 0) return false;
  if ((d.flags & kFlagBeta) == 0 || (d.flags & ~kKnownFlagsMask) != 0) return false;
  if ((d.required_caps & ~d.supported_caps) != 0 || (d.required_caps & kCapLockstepV1) == 0) return false;
  if ((d.supported_caps & kCapTeamIdV8) != 0 && d.proto_max < 8) return false;

  hello.insert(hello.end(), kMagic.begin(), kMagic.end());
  hello.push_back(d.schema);
  hello.push_back(uint8_t(kTrailerSize));
  hello.push_back(d.proto_min);
  hello.push_back(d.proto_max);
  PutU16(hello, d.sim_abi);
  hello.push_back(uint8_t(d.platform_abi));
  hello.push_back(d.flags);
  PutU32(hello, d.supported_caps);
  PutU32(hello, d.required_caps);
  PutU32(hello, d.sim_contract);
  PutU32(hello, d.content_schema);
  return hello.size() == kHelloCapSize;
}

// Parses optional capability trailer on HELLO / HELLO_ACK.
// Returns true for exact 100-byte base packets (*has_caps = false) or valid 128-byte packets (*has_caps = true).
// Returns false with *err = kProtocol on unknown schema version, or *err = kMalformed on structural tampering.
inline bool ParseHelloTrailer(const std::vector<uint8_t>& pkt, const std::string& os_tag,
                              bool* has_caps, Descriptor* out, RejectCode* err) {
  if (has_caps) *has_caps = false;
  if (err) *err = RejectCode::kNone;
  if (pkt.size() == kHelloBaseSize) return true;
  if (pkt.size() != kHelloCapSize) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  const uint8_t* t = pkt.data() + kHelloBaseSize;
  if (!std::equal(kMagic.begin(), kMagic.end(), t)) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  const uint8_t schema = t[4];
  const uint8_t len = t[5];
  if (schema == 0) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if (schema != kSchemaVersion) {
    if (has_caps) *has_caps = true;
    if (err) *err = RejectCode::kProtocol;
    return false;
  }
  if (len != kTrailerSize) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  Descriptor d{};
  d.schema = schema;
  d.proto_min = t[6];
  d.proto_max = t[7];
  d.sim_abi = GetU16(t + 8);
  const uint8_t plat = t[10];
  d.flags = t[11];
  d.supported_caps = GetU32(t + 12);
  d.required_caps = GetU32(t + 16);
  d.sim_contract = GetU32(t + 20);
  d.content_schema = GetU32(t + 24);

  if (d.proto_min < kProtoMin || d.proto_min > d.proto_max || d.sim_abi == 0 || d.content_schema == 0) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if (!ValidPlatformAbi(plat)) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  d.platform_abi = PlatformAbi(plat);
  if (!PlatformMatchesOsTag(d.platform_abi, os_tag)) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if ((d.flags & ~kKnownFlagsMask) != 0) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if ((d.required_caps & ~d.supported_caps) != 0 || (d.required_caps & kCapLockstepV1) == 0) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if ((d.supported_caps & kCapTeamIdV8) != 0 && d.proto_max < 8) {
    if (err) *err = RejectCode::kMalformed;
    return false;
  }
  if (has_caps) *has_caps = true;
  if (out) *out = d;
  return true;
}

inline void WriteRejectExt(std::vector<uint8_t>& reject_18, RejectCode code,
                           PlatformAbi host_platform, uint16_t host_sim_abi, uint32_t missing_caps) {
  if (reject_18.size() != kRejectOsSize) return;
  reject_18.push_back(uint8_t(code));
  reject_18.push_back(uint8_t(host_platform));
  PutU16(reject_18, host_sim_abi);
  PutU32(reject_18, missing_caps);
}

inline bool ParseRejectExt(const std::vector<uint8_t>& pkt, RejectExt* out) {
  if (pkt.size() != kRejectExtSize || !out) return false;
  const uint8_t* p = pkt.data() + kRejectOsSize;
  const uint8_t code = p[0];
  if (code == 0 || code >= uint8_t(RejectCode::kMalformed)) return false;
  out->code = RejectCode(code);
  out->host_platform_abi = p[1];
  out->host_sim_abi = GetU16(p + 2);
  out->missing_caps = GetU32(p + 4);
  return true;
}

// Symmetric, fail-closed admission evaluator shared by Recv(), ValidPacket(), and Handle().
inline Admission EvaluateAdmission(const Descriptor& local_desc, bool local_allow_cross_build,
                                   const std::string& local_build, const std::string& local_os,
                                   const std::string& peer_build, const std::string& peer_os,
                                   bool peer_has_caps, const Descriptor& peer_desc,
                                   bool peer_wide_legacy_offer) {
  Admission out{};
  if (!IsHex32(local_build) || !IsHex32(peer_build)) {
    out.reject_code = RejectCode::kMalformed;
    return out;
  }
  if (peer_has_caps) {
    if ((peer_desc.flags & kFlagBeta) == 0) {
      out.reject_code = RejectCode::kBetaRequired;
      return out;
    }
    if ((peer_desc.required_caps & ~peer_desc.supported_caps) != 0 ||
        (peer_desc.required_caps & kCapLockstepV1) == 0) {
      out.reject_code = RejectCode::kMalformed;
      return out;
    }
  }

  const bool same_build = (peer_build == local_build);
  out.cross_build = !same_build;

  if (same_build) {
    if (!peer_has_caps) {
      const uint32_t legacy_peer_caps =
          kCapLockstepV1 | kCapTagTeam2v2 | kCapCombatRulesV1 | kCapVanillaNegotiate | kCapRollbackUsV1 |
          (peer_wide_legacy_offer ? kCapTeamIdV8 : 0u);
      out.agreed_caps = (local_desc.supported_caps & legacy_peer_caps) & kKnownCapsMask;
      out.team_wire = (out.agreed_caps & kCapTeamIdV8) ? team_wire::Version::kWide : team_wire::Version::kLegacy;
      out.negotiated_proto = uint8_t(out.team_wire);
      out.rollback_allowed = (out.agreed_caps & kCapRollbackUsV1) != 0;
      out.admitted = true;
      return out;
    }
    // Same binary + both carry capability trailers: still verify invariants cleanly.
    if (peer_desc.platform_abi != local_desc.platform_abi) {
      out.reject_code = RejectCode::kPlatformAbi;
      return out;
    }
    if (peer_desc.sim_abi != local_desc.sim_abi) {
      out.reject_code = RejectCode::kSimAbi;
      return out;
    }
    if (peer_desc.sim_contract != local_desc.sim_contract) {
      out.reject_code = RejectCode::kSimContract;
      return out;
    }
    if (peer_desc.content_schema != local_desc.content_schema) {
      out.reject_code = RejectCode::kContentSchema;
      return out;
    }
    const uint8_t lo = std::max(local_desc.proto_min, peer_desc.proto_min);
    const uint8_t hi = std::min(local_desc.proto_max, peer_desc.proto_max);
    if (hi < lo) {
      out.reject_code = RejectCode::kProtocol;
      return out;
    }
    const uint32_t agreed = (local_desc.supported_caps & peer_desc.supported_caps) & kKnownCapsMask;
    const uint32_t missing = (local_desc.required_caps & ~agreed) | (peer_desc.required_caps & ~agreed);
    if (missing != 0) {
      out.reject_code = RejectCode::kMissingCaps;
      out.missing_caps = missing;
      return out;
    }
    out.agreed_caps = agreed;
    out.team_wire = (agreed & kCapTeamIdV8) ? team_wire::Version::kWide : team_wire::Version::kLegacy;
    out.negotiated_proto = hi;
    out.rollback_allowed = (agreed & kCapRollbackUsV1) != 0;
    out.admitted = true;
    return out;
  }

  // Different executable: check OS mismatch first for clear diagnostics.
  if (!peer_os.empty() && peer_os != local_os) {
    out.reject_code = RejectCode::kPlatformAbi;
    return out;
  }
  // Mutual opt-in is mandatory: strict mode cannot be bypassed by a remote peer.
  if (!local_allow_cross_build) {
    out.reject_code = peer_has_caps ? RejectCode::kStrictPolicy : RejectCode::kBuild;
    return out;
  }
  if (!peer_has_caps) {
    out.reject_code = RejectCode::kBuild;
    return out;
  }
  if ((peer_desc.flags & kFlagAllowCrossBuild) == 0) {
    out.reject_code = RejectCode::kStrictPolicy;
    return out;
  }
  // Determinism and security gates for cross-executable lockstep.
  if (peer_desc.platform_abi != local_desc.platform_abi) {
    out.reject_code = RejectCode::kPlatformAbi;
    return out;
  }
  if (peer_desc.sim_abi != local_desc.sim_abi) {
    out.reject_code = RejectCode::kSimAbi;
    return out;
  }
  if (peer_desc.sim_contract != local_desc.sim_contract) {
    out.reject_code = RejectCode::kSimContract;
    return out;
  }
  if (peer_desc.content_schema != local_desc.content_schema) {
    out.reject_code = RejectCode::kContentSchema;
    return out;
  }
  const uint8_t lo = std::max(local_desc.proto_min, peer_desc.proto_min);
  const uint8_t hi = std::min(local_desc.proto_max, peer_desc.proto_max);
  if (hi < lo) {
    out.reject_code = RejectCode::kProtocol;
    return out;
  }
  // Unknown optional bits are cleared; any required bit absent from intersection fails cleanly.
  // Cross-executable sessions force deterministic lockstep (experimental rollback disabled).
  const uint32_t raw_agreed = (local_desc.supported_caps & peer_desc.supported_caps) & kKnownCapsMask;
  const uint32_t agreed = raw_agreed & ~kCapRollbackUsV1;
  const uint32_t missing = (local_desc.required_caps & ~agreed) | (peer_desc.required_caps & ~agreed);
  if (missing != 0) {
    out.reject_code = RejectCode::kMissingCaps;
    out.missing_caps = missing;
    return out;
  }
  out.agreed_caps = agreed;
  out.team_wire = (agreed & kCapTeamIdV8) ? team_wire::Version::kWide : team_wire::Version::kLegacy;
  out.negotiated_proto = hi;
  out.rollback_allowed = false;
  out.admitted = true;
  return out;
}

}  // namespace dbz3::net::caps
