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

#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>
#include <rex/filesystem/afs.h>
#include <rex/logging.h>

#include <fstream>

namespace rex::filesystem {

HostPathFile::HostPathFile(uint32_t file_access, HostPathEntry* entry,
                           std::unique_ptr<rex::filesystem::FileHandle> file_handle)
    : File(file_access, entry), file_handle_(std::move(file_handle)) {}

HostPathFile::~HostPathFile() = default;

void HostPathFile::Destroy() {
  delete this;
}

X_STATUS HostPathFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                                size_t* out_bytes_read) {
  if (!(file_access_ & (FileAccess::kGenericRead | FileAccess::kFileReadData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  // AFS mod override: if this file is an AFS container and the requested range
  // falls inside an entry that a mod replaces, serve the mod's bytes instead.
  if (entry()) {
    const auto& host_path = static_cast<HostPathEntry*>(entry())->host_path();

    // Virtual mid-insert AFS table: presents a consistent AFS table where
    // overridden entries larger than their slot grow in place (like a rebuilt
    // AFS). The guest then allocates a buffer large enough for the mod bin, and
    // data reads are translated back to the physical file (or the override).
    // Only used when the AFS has at least one "grown" entry; otherwise the plain
    // per-entry override below is enough.
    if (host_path.filename() == "data_cmn.afs") {
      std::vector<uint8_t> vtable;
      bool any_growth = false;
      const size_t vh_size = AfsGetVirtualTable(host_path, vtable, any_growth);
      if (vh_size > 0 && byte_offset < vh_size) {
        // Read inside the header+table region: serve the virtual table.
        const size_t src_off = static_cast<size_t>(byte_offset);
        const size_t n = std::min(buffer.size(), vh_size - src_off);
        std::memcpy(buffer.data(), vtable.data() + src_off, n);
        size_t got = n;
        if (n < buffer.size()) {
          // Request crossed past the table region (guest reads often round up
          // to 0x8000). Complete from the real file to keep the total exact.
          size_t real_got = 0;
          if (file_handle_->Read(byte_offset + n, buffer.data() + n, buffer.size() - n,
                                 &real_got)) {
            got += real_got;
          }
        }
        if (out_bytes_read) {
          *out_bytes_read = got;
        }
        return got > 0 ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
      }
      if (any_growth) {
        uint64_t phys_offset = 0, mod_offset = 0;
        std::filesystem::path mod_path;
        const int entry_index =
            AfsTranslateOffset(host_path, byte_offset, phys_offset, mod_path, mod_offset);
        if (entry_index >= 0) {
          if (!mod_path.empty()) {
            std::ifstream mod_file(mod_path, std::ios::binary);
            if (mod_file) {
              const size_t to_read = buffer.size();
              mod_file.seekg(std::streamoff(mod_offset), std::ios::beg);
              mod_file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(to_read));
              size_t got = mod_file.gcount() > 0 ? size_t(mod_file.gcount()) : 0;
              if (out_bytes_read) {
                *out_bytes_read = got;
              }
              return got > 0 ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
            }
          }
          // Regular (non-overridden) entry: read from the physical file at the
          // translated offset.
          if (file_handle_->Read(phys_offset, buffer.data(), buffer.size(), out_bytes_read)) {
            return X_STATUS_SUCCESS;
          }
          return X_STATUS_END_OF_FILE;
        }
      }
    }

    uint64_t entry_start = 0, entry_size = 0;
    int entry_index = AfsFindEntry(host_path, byte_offset, entry_start, entry_size);
    if (entry_index >= 0) {
      std::filesystem::path mod_path;
      if (AfsFindModOverride(host_path, entry_index, mod_path)) {
        std::ifstream mod_file(mod_path, std::ios::binary);
        if (mod_file) {
          uint64_t mod_offset = byte_offset - entry_start;
          const size_t to_read = buffer.size();
          mod_file.seekg(std::streamoff(mod_offset), std::ios::beg);
          mod_file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(to_read));
          size_t got = mod_file.gcount() > 0 ? size_t(mod_file.gcount()) : 0;
          if (host_path.filename() == "data_cmn.afs" && entry_index == 327) {
            mod_file.seekg(0, std::ios::end);
            const int64_t fsz = static_cast<int64_t>(mod_file.tellg());
            mod_file.seekg(std::streamoff(mod_offset), std::ios::beg);
            REXLOG_INFO("AFS MOD READ: bin 327 mod_off=0x{:X} to_read={} got={} mod_size={}", mod_offset, to_read, got, fsz);
          }
          if (host_path.filename() == "data_cmn.afs" && entry_index == 91) {
            mod_file.seekg(0, std::ios::end);
            const int64_t fsz = static_cast<int64_t>(mod_file.tellg());
            mod_file.seekg(std::streamoff(mod_offset), std::ios::beg);
            REXLOG_INFO("AFS MOD READ: bin 91 mod_off=0x{:X} to_read={} got={} mod_size={}", mod_offset, to_read, got, fsz);
          }
          if (out_bytes_read) {
            *out_bytes_read = got;
          }
          return got > 0 ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
        }
      }
    }
  }

  if (entry() && static_cast<HostPathEntry*>(entry())->host_path().filename() == "data_cmn.afs") {
    uint64_t es = 0, esz = 0;
    int ei = AfsFindEntry(static_cast<HostPathEntry*>(entry())->host_path(), byte_offset, es, esz);
    if (ei == 327) {
      REXLOG_INFO("AFS327 READ: off=0x{:X} to_read={} entry_start=0x{:X} entry_size={}",
                  byte_offset, buffer.size(), es, esz);
    }
  }

  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(), out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access_ &
        (FileAccess::kGenericWrite | FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Write(byte_offset, buffer.data(), buffer.size(), out_bytes_written)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::SetLength(size_t length) {
  if (!(file_access_ & (FileAccess::kGenericWrite | FileAccess::kFileWriteData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->SetLength(length)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

}  // namespace rex::filesystem
