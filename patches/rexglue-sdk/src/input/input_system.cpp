/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/dbg.h>
#include <rex/dbz3_latencia.h>
#include <rex/input/device_assignment.h>
#include <rex/input/flags.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/state_merge.h>
#include <rex/input/xinput/xinput_input_driver.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(input_backend, "sdl", "Input", "Input backend: sdl, xinput")
    .allowed({"sdl", "xinput"});

REXCVAR_DEFINE_DOUBLE(deadzone, 0.1, "Input",
                      "Analog stick deadzone as a fraction of full deflection (0.0 - 0.9)")
    .range(0.0, 0.9)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(rumble, true, "Input", "Enable controller vibration")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(guide_button, false, "Input", "Enable guide button pass-through");
namespace rex::input {

namespace {

// Synthetic devices are parked past every physical ordinal so they cannot push
// a real pad off guest user 0. SlotAssignment routes them by their synthetic
// flag and never reads this value.
constexpr uint32_t kSyntheticOrdinal = UINT32_MAX;

}  // namespace

InputSystem::InputSystem(rex::ui::Window* window) : window_(window) {}

InputSystem::~InputSystem() = default;

X_STATUS InputSystem::Setup() {
  return X_STATUS_SUCCESS;
}

void InputSystem::Shutdown() {
  // device_owners_ holds raw driver pointers.
  devices_.clear();
  device_owners_.clear();
  drivers_.clear();
}

void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));
}

void InputSystem::AttachWindow(rex::ui::Window* window) {
  window_ = window;
  for (auto& driver : drivers_) {
    driver->OnWindowAvailable(window);
  }
}

void InputSystem::SetActiveCallback(std::function<bool()> callback) {
  for (auto& driver : drivers_) {
    driver->set_is_active_callback(callback);
  }
}

void InputSystem::SetDeviceAssignment(std::unique_ptr<DeviceAssignment> assignment) {
  assignment_ = std::move(assignment);
  if (assignment_) {
    assignment_->OnDevicesChanged(devices_);
  }
}

void InputSystem::RefreshDevices() {
  // The guest polls from its threads and the quick menu from the UI thread:
  // device refreshes must not run at the same time (heap corruption, v1.4.0).
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<DeviceInfo> seen;
  std::vector<InputDriver*> owners;
  std::vector<DeviceInfo> enumerated;
  for (auto& driver : drivers_) {
    enumerated.clear();
    driver->EnumerateDevices(enumerated);
    for (auto& info : enumerated) {
      seen.push_back(info);
      owners.push_back(driver.get());
    }
  }

  // Carry forward ordinals already handed out, so a device keeps its guest user
  // when another pad is unplugged.
  bool changed = seen.size() != devices_.size();
  std::vector<bool> fresh(seen.size(), false);
  for (size_t i = 0; i < seen.size(); i++) {
    auto existing = std::find_if(devices_.begin(), devices_.end(),
                                 [&](const DeviceInfo& d) { return d.id == seen[i].id; });
    if (existing != devices_.end()) {
      seen[i].ordinal = existing->ordinal;
      continue;
    }
    fresh[i] = true;
    changed = true;
  }

  // Runs after the carry-forward pass so a new device cannot take an ordinal a
  // live one is still holding. Lowest free rather than a growing counter,
  // because a reconnected pad arrives as a new device and would otherwise walk
  // off the end of the guest users.
  for (size_t i = 0; i < seen.size(); i++) {
    if (!fresh[i]) {
      continue;
    }
    // Only physical devices consume an ordinal, so the first pad to connect is
    // guest user 0 however many synthetic devices enumerated ahead of it.
    if (seen[i].synthetic) {
      seen[i].ordinal = kSyntheticOrdinal;
      fresh[i] = false;
      continue;
    }
    auto taken = [&](uint32_t candidate) {
      for (size_t j = 0; j < seen.size(); j++) {
        if (!fresh[j] && !seen[j].synthetic && seen[j].ordinal == candidate) {
          return true;
        }
      }
      return false;
    };
    uint32_t ordinal = 0;
    while (taken(ordinal)) {
      ordinal++;
    }
    seen[i].ordinal = ordinal;
    fresh[i] = false;
  }

  for (const auto& old : devices_) {
    if (std::none_of(seen.begin(), seen.end(),
                     [&](const DeviceInfo& d) { return d.id == old.id; })) {
      active_devices_.Forget(old.id);
      changed = true;
    }
  }

  std::vector<size_t> order(seen.size());
  for (size_t i = 0; i < order.size(); i++) {
    order[i] = i;
  }
  // Stable: synthetic devices share one ordinal, and their relative order
  // decides which answers GetCapabilities when no pad is attached.
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t a, size_t b) { return seen[a].ordinal < seen[b].ordinal; });

  devices_.clear();
  device_owners_.clear();
  for (size_t i : order) {
    devices_.push_back(seen[i]);
    device_owners_.push_back(owners[i]);
  }

  if (changed && assignment_) {
    assignment_->OnDevicesChanged(devices_);
  }
}

InputDriver* InputSystem::DriverForDevice(DeviceId id) {
  for (size_t i = 0; i < devices_.size(); i++) {
    if (devices_[i].id == id) {
      return device_owners_[i];
    }
  }
  return nullptr;
}

const DeviceInfo* InputSystem::DeviceInfoFor(DeviceId id) const {
  for (const auto& device : devices_) {
    if (device.id == id) {
      return &device;
    }
  }
  return nullptr;
}

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");
  // The guest polls from its threads and the quick menu from the UI thread:
  // device refreshes must not run at the same time (heap corruption, v1.4.0).
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!out_caps || !assignment_) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  RefreshDevices();
  std::vector<DeviceId> ids;
  assignment_->DevicesForUser(user_index, ids);
  if (ids.empty()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Prefer the pad in hand, so button glyphs follow it rather than whichever
  // device enumerated first.
  DeviceId chosen = active_devices_.Active(user_index);
  if (std::find(ids.begin(), ids.end(), chosen) == ids.end()) {
    chosen = ids.front();
  }

  auto* driver = DriverForDevice(chosen);
  if (!driver) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return driver->GetDeviceCapabilities(chosen, flags, out_caps);
}

X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  // A dialog owns the controller.
  if (ui_input_blockers_.load() > 0) {
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
    }
    return X_ERROR_SUCCESS;
  }

  std::lock_guard<std::recursive_mutex> lock(mutex_);
  X_RESULT result = GetStateForUI(user_index, out_state);

  if (result == X_ERROR_SUCCESS && out_state && user_index < kMaxGuestUsers) {
    // The UI toggle combo is for the emulator, keep it from the guest.
    const uint16_t combo = ui_toggle_combo_.load(std::memory_order_relaxed);
    const bool combo_held =
        combo && (static_cast<uint16_t>(out_state->gamepad.buttons) & combo) == combo;
    if (combo_held && !ui_toggle_combo_held_[user_index] && ui_toggle_callback_) {
      consumed_buttons_[user_index] |= combo;
      ui_toggle_callback_();
    }
    ui_toggle_combo_held_[user_index] = combo_held;

    if (consumed_buttons_[user_index] != 0) {
      const uint16_t buttons = out_state->gamepad.buttons;
      // Each button leaves the mask once the player lets go of it.
      consumed_buttons_[user_index] &= buttons;
      out_state->gamepad.buttons =
          static_cast<uint16_t>(buttons & ~consumed_buttons_[user_index]);
    }

    // dbz3 (#59): marca de la ultima pulsacion NUEVA que lee el guest, para la
    // sonda de latencia entrada->present (dbz3_latencia_sonda). No altera la
    // entrada: solo apunta la hora.
    {
      static uint16_t prev_buttons[kMaxGuestUsers] = {};
      const uint16_t buttons = static_cast<uint16_t>(out_state->gamepad.buttons);
      const uint16_t nueva = static_cast<uint16_t>(buttons & ~prev_buttons[user_index]);
      prev_buttons[user_index] = buttons;
      if (nueva != 0) {
        rex::dbz3::g_input_edge_buttons.store(nueva, std::memory_order_relaxed);
        rex::dbz3::g_input_edge_ns.store(
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()),
            std::memory_order_relaxed);
      }
    }
  }
  return result;
}

void InputSystem::AddUIInputBlocker() {
  ui_input_blockers_.fetch_add(1);
}

void InputSystem::RemoveUIInputBlocker() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  X_INPUT_STATE state = {};
  const uint16_t combo = ui_toggle_combo_.load(std::memory_order_relaxed);
  for (uint32_t user_index = 0; user_index < kMaxGuestUsers; user_index++) {
    if (GetStateForUI(user_index, &state) == X_ERROR_SUCCESS) {
      const uint16_t buttons = state.gamepad.buttons;
      consumed_buttons_[user_index] |= buttons;
      // A toggle combo that closed the dialog must not open it again.
      ui_toggle_combo_held_[user_index] = combo && (buttons & combo) == combo;
    }
  }
  // Keystrokes queued while the dialog had the controllers belong to it.
  for (size_t i = 0; i < devices_.size() && i < device_owners_.size(); ++i) {
    X_INPUT_KEYSTROKE discarded = {};
    for (int j = 0; j < 256; ++j) {
      if (device_owners_[i]->GetDeviceKeystroke(devices_[i].id, 0, &discarded) !=
          X_ERROR_SUCCESS) {
        break;
      }
    }
  }
  ui_input_blockers_.fetch_sub(1);
}

void InputSystem::SetUIToggleCombo(uint16_t buttons, std::function<void()> callback) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ui_toggle_callback_ = std::move(callback);
  ui_toggle_combo_held_ = {};
  ui_toggle_combo_.store(buttons, std::memory_order_relaxed);
}

void InputSystem::SetUIToggleComboButtons(uint16_t buttons) {
  ui_toggle_combo_.store(buttons, std::memory_order_relaxed);
}

X_RESULT InputSystem::GetStateForUI(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!assignment_) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  RefreshDevices();
  std::vector<DeviceId> ids;
  assignment_->DevicesForUser(user_index, ids);

  X_INPUT_STATE merged = {};
  bool any = false;
  for (DeviceId id : ids) {
    auto* driver = DriverForDevice(id);
    if (!driver) {
      continue;
    }
    X_INPUT_STATE state = {};
    if (driver->GetDeviceState(id, &state) != X_ERROR_SUCCESS) {
      continue;
    }
    active_devices_.Observe(user_index, id, state.gamepad);
    if (!any) {
      merged = state;
      any = true;
    } else {
      MergeInto(merged, state);
    }
  }

  if (!any) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Apply the configurable stick deadzone to the merged state (XInput-style
  // snap-to-neutral). Covers every driver (XInput, SDL, MnK) at the single
  // guest-facing choke point.
  const double deadzone_fraction = REXCVAR_GET(deadzone);
  if (deadzone_fraction > 0.0) {
    const int32_t threshold =
        static_cast<int32_t>(deadzone_fraction * static_cast<double>(INT16_MAX));
    auto snap = [threshold](int16_t axis) {
      return std::abs(static_cast<int>(axis)) < threshold ? int16_t{0} : axis;
    };
    merged.gamepad.thumb_lx = snap(merged.gamepad.thumb_lx);
    merged.gamepad.thumb_ly = snap(merged.gamepad.thumb_ly);
    merged.gamepad.thumb_rx = snap(merged.gamepad.thumb_rx);
    merged.gamepad.thumb_ry = snap(merged.gamepad.thumb_ry);
  }
  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");
  // The guest polls from its threads and the quick menu from the UI thread:
  // device refreshes must not run at the same time (heap corruption, v1.4.0).
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!assignment_) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Vibration toggle: when disabled, accept the request without buzzing any
  // pad (the guest expects SetState to succeed).
  if (!REXCVAR_GET(rumble)) {
    return X_ERROR_SUCCESS;
  }

  RefreshDevices();
  std::vector<DeviceId> ids;
  assignment_->DevicesForUser(user_index, ids);

  // Every pad on this user belongs to the same player, so all of them buzz.
  // Only pads decide the result: synthetic devices accept any vibration and
  // would otherwise report success for a pad that never rumbled.
  bool any_pad = false;
  bool any_synthetic = false;
  bool pad_rumbled = false;
  X_RESULT pad_error = X_ERROR_DEVICE_NOT_CONNECTED;
  for (DeviceId id : ids) {
    auto* driver = DriverForDevice(id);
    const DeviceInfo* info = DeviceInfoFor(id);
    if (!driver || !info) {
      continue;
    }
    X_RESULT result = driver->SetDeviceVibration(id, vibration);
    if (info->synthetic) {
      any_synthetic = true;
      continue;
    }
    any_pad = true;
    if (result == X_ERROR_SUCCESS) {
      pad_rumbled = true;
    } else {
      pad_error = result;
    }
  }
  if (any_pad) {
    return pad_rumbled ? X_ERROR_SUCCESS : pad_error;
  }
  return any_synthetic ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");
  // The guest polls from its threads and the quick menu from the UI thread:
  // device refreshes must not run at the same time (heap corruption, v1.4.0).
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // A dialog owns the controller: the guest sees no keystrokes meanwhile.
  if (ui_input_blockers_.load() > 0) {
    return X_ERROR_EMPTY;
  }
  if (!assignment_) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  RefreshDevices();
  std::vector<DeviceId> ids;
  assignment_->DevicesForUser(user_index, ids);

  bool any_connected = false;
  for (DeviceId id : ids) {
    auto* driver = DriverForDevice(id);
    if (!driver) {
      continue;
    }
    X_RESULT result = driver->GetDeviceKeystroke(id, flags, out_keystroke);
    if (result == X_ERROR_SUCCESS) {
      out_keystroke->user_index = static_cast<uint8_t>(user_index);
      return result;
    }
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

std::unique_ptr<InputSystem> CreateDefaultInputSystem(bool tool_mode) {
  auto input = std::make_unique<InputSystem>(nullptr);

  if (!tool_mode) {
    bool use_sdl = REXCVAR_GET(input_backend) == "sdl";
#if REX_PLATFORM_WIN32
    if (REXCVAR_GET(input_backend) == "xinput") {
      // A5/B30: XInput solo ve mandos de Xbox. Sin ninguno conectado (DualSense, Switch, o
      // xinput1_4 que no carga) se usa SDL en su lugar; nunca los dos para el mismo mando.
      auto xinput_driver = std::make_unique<xinput::XinputInputDriver>(nullptr, 0);
      std::vector<DeviceInfo> pads;
      if (xinput_driver->Setup() == X_STATUS_SUCCESS) xinput_driver->EnumerateDevices(pads);
      if (pads.empty()) {
        REXLOG_INFO("input: no hay mandos XInput; se usa SDL (DualSense y otros)");
        use_sdl = true;
      } else {
        REXLOG_INFO("input: {} mando(s) XInput; se usa XInput", pads.size());
        input->AddDriver(std::move(xinput_driver));
      }
    }
#endif

    if (use_sdl) {
      auto sdl_driver = std::make_unique<sdl::SDLInputDriver>(nullptr, 0);
      if (sdl_driver->Setup() == X_STATUS_SUCCESS) {
        input->AddDriver(std::move(sdl_driver));
      }
    }

    // MnK driver (keyboard/mouse -> controller emulation)
    auto mnk_driver = std::make_unique<mnk::MnkInputDriver>(nullptr, 0);
    if (mnk_driver->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(mnk_driver));
    }
  }

  // NOP driver (primary in tool mode, fallback otherwise)
  uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<nop::NopInputDriver>(nullptr, nop_index));
  input->SetDeviceAssignment(std::make_unique<SlotAssignment>());
  return input;
}

}  // namespace rex::input
