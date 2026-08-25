/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>

#include <rex/filesystem.h>
#include <rex/filesystem/afs.h>
#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/mapped_memory.h>
#include <rex/string.h>

// dbz1_audio_jp is defined in src/system/dbz1_audio_jp_flag.cpp (shared storage).
REXCVAR_DECLARE(bool, dbz1_audio_jp);
// dbz1_region is defined in src/system/dbz1_region_flag.cpp (shared storage).
REXCVAR_DECLARE(std::string, dbz1_region);
// dbz1_diag_logging is defined in src/system/dbz1_diag_flags.cpp (shared runtime).
REXCVAR_DECLARE(bool, dbz1_diag_logging);
#include <fstream>

namespace rex::filesystem {

HostPathEntry::HostPathEntry(Device* device, Entry* parent, const std::string_view path,
                             const std::filesystem::path& host_path)
    : Entry(device, parent, path), host_path_(host_path) {}

HostPathEntry::~HostPathEntry() = default;

HostPathEntry* HostPathEntry::Create(Device* device, Entry* parent,
                                     const std::filesystem::path& full_path,
                                     rex::filesystem::FileInfo file_info) {
  auto path = rex::string::utf8_join_guest_paths(parent->path(), rex::path_to_utf8(file_info.name));
  auto entry = new HostPathEntry(device, parent, path, full_path);

  entry->create_timestamp_ = file_info.create_timestamp;
  entry->access_timestamp_ = file_info.access_timestamp;
  entry->write_timestamp_ = file_info.write_timestamp;
  if (file_info.type == rex::filesystem::FileInfo::Type::kDirectory) {
    entry->attributes_ = kFileAttributeDirectory;
  } else {
    entry->attributes_ = kFileAttributeNormal;
    if (device->is_read_only()) {
      entry->attributes_ |= kFileAttributeReadOnly;
    }
    entry->size_ = file_info.total_size;
    entry->allocation_size_ = rex::round_up(file_info.total_size, device->bytes_per_sector());
  }
  return entry;
}

X_STATUS HostPathEntry::Open(uint32_t desired_access, File** out_file) {
  if (is_read_only() &&
      (desired_access & (FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    REXFS_ERROR("Attempting to open file for write access on read-only device");
    return X_STATUS_ACCESS_DENIED;
  }

  // Audio language override: when Japanese audio is requested, redirect any
  // English/regional audio file to its Japanese counterpart. US layout uses
  // adx_us/adx_jp; EUR layout has only adx_jp (the game opens it directly).
  std::filesystem::path open_path = host_path_;
  if (REXCVAR_GET(dbz1_audio_jp)) {
    const std::string fn = open_path.filename().string();
    if (fn == "adx_us.afs" || fn == "adx_usa.afs") {
      // Redirect to the Japanese pack. US uses adx_jp, EUR uses adx_jp too
      // (the only audio file); try adx_jp first.
      auto jp_candidate = open_path.parent_path() / "adx_jp.afs";
      if (std::filesystem::is_regular_file(jp_candidate)) {
        open_path = jp_candidate;
      }
    }
  }

  // Region naming override: the game hardcodes US-style asset filenames for
  // the English pack (data_us.afs) regardless of region. The EUR layout names
  // it data_en.afs; present the EUR file under the name the game expects.
  if (REXCVAR_GET(dbz1_region) == "eur") {
    const std::string fn = open_path.filename().string();
    std::filesystem::path parent = open_path.parent_path();
    if (fn == "data_us.afs") {
      open_path = parent / "data_en.afs";
    }
  }

  // TEMP diagnostic: log audio and .afs opens to see what EUR requests.
  // Gated by dbz1_diag_logging so no log file is written in normal play.
  if (REXCVAR_GET(dbz1_diag_logging)) {
    static uint32_t diag_open_count = 0;
    if (diag_open_count < 200) {
      const std::string fn = open_path.filename().string();
      if (fn.find("adx") != std::string::npos || fn.find(".afs") != std::string::npos) {
        ++diag_open_count;
        std::ofstream diag_file("dbz1_host_opens.log", std::ios::app);
        if (diag_file) {
          diag_file << open_path.string() << std::endl;
        }
      }
    }
  }

  // Mod override (whole file): if a mod provides a full replacement for this
  // file (mods/<mod>/<filename> or mods/<mod>/us|eu/<filename>), open the mod's
  // file instead. This is how fan-made packs (e.g. custom music as
  // opening.sfd/adx_usa.afs/Ending00.sfd) are applied without any overlay or
  // duplication of the game assets.
  {
    std::filesystem::path mod_path;
    if (AfsFindModFileOverride(open_path, mod_path)) {
      open_path = mod_path;
    }
  }

  auto file_handle = rex::filesystem::FileHandle::OpenExisting(
      open_path, desired_access, static_cast<HostPathDevice*>(device_)->allow_share_delete());
  if (!file_handle) {
    REXFS_ERROR("Open FAILED: guest={} host={}", path(), rex::path_to_utf8(open_path));
    // TODO(benvanik): pick correct response.
    return X_STATUS_NO_SUCH_FILE;
  }
  *out_file = new HostPathFile(desired_access, this, std::move(file_handle));
  return X_STATUS_SUCCESS;
}

std::unique_ptr<memory::MappedMemory> HostPathEntry::OpenMapped(memory::MappedMemory::Mode mode,
                                                                size_t offset, size_t length) {
  return memory::MappedMemory::Open(host_path_, mode, offset, length);
}

bool HostPathEntry::Truncate() {
  if (is_read_only() || (attributes_ & kFileAttributeDirectory)) {
    return false;
  }
  auto file = rex::filesystem::OpenFile(host_path_, "wb");
  if (!file) {
    return false;
  }
  fclose(file);
  size_ = 0;
  allocation_size_ = 0;
  return true;
}

std::unique_ptr<Entry> HostPathEntry::CreateEntryInternal(const std::string_view name,
                                                          uint32_t attributes) {
  auto full_path = host_path_ / rex::to_path(name);
  if (attributes & kFileAttributeDirectory) {
    if (!std::filesystem::create_directories(full_path)) {
      return nullptr;
    }
  } else {
    auto file = rex::filesystem::OpenFile(full_path, "wb");
    if (!file) {
      return nullptr;
    }
    fclose(file);
  }
  rex::filesystem::FileInfo file_info;
  if (!rex::filesystem::GetInfo(full_path, &file_info)) {
    return nullptr;
  }
  return std::unique_ptr<Entry>(HostPathEntry::Create(device_, this, full_path, file_info));
}

bool HostPathEntry::DeleteEntryInternal(Entry* entry) {
  auto full_path = host_path_ / rex::to_path(entry->name());
  std::error_code ec;  // avoid exception on remove/remove_all failure
  if (entry->attributes() & kFileAttributeDirectory) {
    // Delete entire directory and contents.
    auto removed = std::filesystem::remove_all(full_path, ec);
    return removed >= 1 && removed != static_cast<std::uintmax_t>(-1);
  } else {
    // Delete file.
    return !std::filesystem::is_directory(full_path) && std::filesystem::remove(full_path, ec);
  }
}

X_STATUS HostPathEntry::RenameEntryInternal(const std::vector<std::string_view>& path_parts) {
  auto new_host_path = static_cast<HostPathDevice*>(device_)->host_path();
  for (const auto& path_part : path_parts) {
    new_host_path /= rex::to_path(path_part);
  }

  std::error_code ec;
  std::filesystem::rename(host_path_, new_host_path, ec);
  if (ec) {
    REXFS_ERROR("RenameEntryInternal: failed to rename '{}' to '{}': {}",
                rex::path_to_utf8(host_path_), rex::path_to_utf8(new_host_path), ec.message());
    return X_STATUS_ACCESS_DENIED;
  }

  host_path_ = new_host_path;
  return X_STATUS_SUCCESS;
}

void HostPathEntry::update() {
  rex::filesystem::FileInfo file_info;
  if (!rex::filesystem::GetInfo(host_path_, &file_info)) {
    return;
  }
  if (file_info.type == rex::filesystem::FileInfo::Type::kFile) {
    size_ = file_info.total_size;
    allocation_size_ = rex::round_up(file_info.total_size, device()->bytes_per_sector());
  }
}

bool HostPathEntry::SetAttributes(uint64_t attributes) {
  if (device_->is_read_only()) {
    return false;
  }
  attributes_ = static_cast<uint32_t>(attributes);
  return true;
}

bool HostPathEntry::SetCreateTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  create_timestamp_ = timestamp;
  return true;
}

bool HostPathEntry::SetAccessTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  access_timestamp_ = timestamp;
  return true;
}

bool HostPathEntry::SetWriteTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    return false;
  }
  write_timestamp_ = timestamp;
  return true;
}

}  // namespace rex::filesystem
