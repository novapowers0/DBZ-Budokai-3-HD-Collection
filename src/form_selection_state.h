#pragma once
#include "form_selection.h"
#include "form_preferences.h"
#include <mutex>

namespace dbz3::forms {
struct SideState { Snapshot snapshot; int requested = 0; Result result; };
struct MemberChoice { int requested = 0, suit = 0; std::string identity; };
inline std::mutex state_mutex;
inline std::array<SideState, 2> states{};
inline std::array<std::map<int, MemberChoice>, 2> members{};
inline Preferences preferences;
inline std::filesystem::path preferences_path;
inline bool preferences_dirty = false, preferences_blocked = false;
inline std::string preferences_error;
inline void Initialize(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(state_mutex);
  states = {}; members = {}; preferences.clear(); preferences_dirty = false;
  preferences_path = path; preferences_error.clear(); preferences_blocked = false;
  try {
    if (std::filesystem::exists(path.string() + ".lock")) throw std::runtime_error("recuperacion de preferencias pendiente");
    if (std::filesystem::exists(path)) {
      if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path) ||
          std::filesystem::file_size(path) > 1048576) throw std::runtime_error("preferencias no validas o demasiado grandes");
      std::ifstream in(path, std::ios::binary); preferences = ParsePreferences(in);
    }
  } catch (const std::exception& e) {
    preferences_error = e.what(); preferences_blocked = true; // conservar fichero corrupto intacto
  }
}
inline void Publish(int side, const Snapshot& snapshot, bool online = false) {
  if (side < 0 || side > 1) return;
  std::lock_guard<std::mutex> lock(state_mutex);
  auto& s = states[size_t(side)];
  if (s.snapshot.identity != snapshot.identity || s.snapshot.id != snapshot.id) {
    s.requested = 0;
    if (!online) {
       const auto it = preferences.find(snapshot.identity);
       if (it != preferences.end() && it->second < snapshot.count) s.requested = it->second;
    }
  }
  s.snapshot = snapshot; s.result = Resolve(snapshot, s.requested, true, false, online);
}
inline SideState Copy(int side) {
  std::lock_guard<std::mutex> lock(state_mutex);
  return side >= 0 && side < 2 ? states[size_t(side)] : SideState{};
}
inline bool Move(int side, int delta, bool online = false) {
  if (side < 0 || side > 1 || online || !delta) return false;
  std::lock_guard<std::mutex> lock(state_mutex);
  auto& s = states[size_t(side)];
  const int next = Step(s.snapshot, s.requested, delta);
  if (next == s.requested) return false;
  s.requested = next; s.result = Resolve(s.snapshot, next);
  if (!s.snapshot.identity.empty() && !preferences_blocked) {
    preferences[s.snapshot.identity] = next; preferences_dirty = true;
  }
  return true;
}
inline void Confirm(int side, int id, bool online = false) {
  if (side < 0 || side > 1 || id < 0 || id >= kIds || online) return;
  std::lock_guard<std::mutex> lock(state_mutex);
  const auto& s = states[size_t(side)];
  if (s.snapshot.id == id) members[size_t(side)][id] = {s.requested, s.snapshot.suit, s.snapshot.identity};
}
inline int MemberRequest(int side, int id) {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (side < 0 || side > 1) return 0;
  const auto& map = members[size_t(side)];
  const auto it = map.find(id);
  return it != map.end() ? it->second.requested : (states[size_t(side)].snapshot.id == id ? states[size_t(side)].requested : 0);
}
inline int MemberSuit(int side, int id, int fallback) {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (side < 0 || side > 1) return fallback;
  const auto& map = members[size_t(side)];
  const auto it = map.find(id);
  return it == map.end() ? fallback : it->second.suit;
}
inline void ForgetMember(int side, int id) {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (side >= 0 && side < 2) members[size_t(side)].erase(id);
}
inline void ClearMembers() { std::lock_guard<std::mutex> lock(state_mutex); members = {}; }
inline constexpr const char* kRosterPagePreference = "@dbz3_dynamic_roster_page";
inline int RosterPagePreference() {
  std::lock_guard<std::mutex> lock(state_mutex);
  const auto it = preferences.find(kRosterPagePreference);
  return it != preferences.end() ? it->second : 0;
}
inline void RememberRosterPage(int page) {
  if (page < 0 || page >= 256) return;
  std::lock_guard<std::mutex> lock(state_mutex);
  if (preferences_blocked) return;
  auto it = preferences.find(kRosterPagePreference);
  if (it != preferences.end() && it->second == page) return;
  preferences[kRosterPagePreference] = page;
  preferences_dirty = true;
}
inline void Notice(int side, const Snapshot& snapshot, const Result& result) {
  if (side < 0 || side > 1) return;
  std::lock_guard<std::mutex> lock(state_mutex);
  states[size_t(side)] = {snapshot, result.requested, result};
}
// Llamar en UI al salir de la rueda/cerrar; nunca E/S por gatillo ni por tick guest.
inline void FlushPreferences() {
  std::lock_guard<std::mutex> lock(state_mutex);
  if (!preferences_dirty || preferences_blocked || preferences_path.empty()) return;
  try { SavePreferences(preferences_path, preferences); preferences_dirty = false; }
  catch (const std::exception& e) { preferences_error = e.what(); preferences_blocked = true; }
}
inline std::string PreferenceError() { std::lock_guard<std::mutex> lock(state_mutex); return preferences_error; }
} // namespace dbz3::forms
