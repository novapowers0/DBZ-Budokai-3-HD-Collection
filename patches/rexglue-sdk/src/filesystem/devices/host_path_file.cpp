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
#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstring>
#include <fstream>

// dbz1_diag_logging is defined in src/system/dbz1_diag_flags.cpp (shared runtime).
REXCVAR_DECLARE(bool, dbz1_diag_logging);

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

    // TEMP diagnostic: log every AFS read mapped to its entry index, so a play
    // session can reveal which data_cmn/data_eng entries each character/stage
    // loads (roster -> bin mapping). Gated by dbz1_diag_logging (F10/dev
    // toggle) so no file is written in normal play. Writes dbz1_afs_reads.log.
    if (REXCVAR_GET(dbz1_diag_logging)) {
      uint64_t entry_start = 0, entry_size = 0;
      const int entry_index = AfsFindEntry(host_path, byte_offset, entry_start, entry_size);
      if (entry_index >= 0) {
        std::ofstream diag_file("dbz1_afs_reads.log", std::ios::app);
        if (diag_file) {
          diag_file << host_path.filename().string() << " entry=" << entry_index << std::hex
                    << " off=0x" << byte_offset << " n=0x" << buffer.size() << " eoff=0x"
                    << entry_start << " esize=0x" << entry_size << std::dec << std::endl;
        }
      }
    }

    // Virtual mid-insert AFS table: presents a consistent AFS table where
    // overridden entries larger than their slot grow in place (like a rebuilt
    // AFS). The guest then allocates a buffer large enough for the mod bin, and
    // data reads are served consistently: every byte range is translated to the
    // physical file (or the override), and anything outside an entry (header,
    // gap, pad, EOF) is served as zeros -- NEVER as a physical read at a stale
    // offset, which would pull garbage from the middle of another entry and
    // crash the guest's sub-block parser.
    if (host_path.filename() == "data_cmn.afs") {
      std::vector<uint8_t> vtable;
      bool any_growth = false;
      const size_t vh_size = AfsGetVirtualTable(host_path, vtable, any_growth);
      if (any_growth) {
        size_t served = 0;
        uint64_t off = byte_offset;
        const uint64_t end = byte_offset + buffer.size();
        while (off < end) {
          std::filesystem::path src;
          uint64_t src_off = 0, run = 0;
          uint64_t chunk;
          if (off < vh_size) {
            // Header+table region: serve the virtual table bytes.
            chunk = std::min<uint64_t>(vh_size - off, end - off);
            std::memcpy(buffer.data() + served, vtable.data() + off, size_t(chunk));
          } else if (AfsVirtualRange(host_path, off, src, src_off, run) && run > 0) {
            chunk = std::min(run, end - off);
            if (src.empty()) {
              // Gap/pad region: zeros (never leak physical bytes).
              std::memset(buffer.data() + served, 0, size_t(chunk));
            } else if (src == host_path) {
              size_t got = 0;
              if (!file_handle_->Read(src_off, buffer.data() + served, size_t(chunk),
                                      &got)) {
                got = 0;
              }
              if (got < chunk) {
                std::memset(buffer.data() + served + got, 0, size_t(chunk - got));
              }
            } else {
              std::ifstream mod_file(src, std::ios::binary);
              if (mod_file) {
                mod_file.seekg(std::streamoff(src_off), std::ios::beg);
                mod_file.read(reinterpret_cast<char*>(buffer.data() + served),
                              std::streamsize(chunk));
                const size_t got = size_t(mod_file.gcount());
                if (got < chunk) {
                  std::memset(buffer.data() + served + got, 0, size_t(chunk - got));
                }
              } else {
                std::memset(buffer.data() + served, 0, size_t(chunk));
              }
            }
          } else {
            // Past the virtual end of the AFS: zeros.
            chunk = end - off;
            std::memset(buffer.data() + served, 0, size_t(chunk));
          }
          off += chunk;
          served += size_t(chunk);
        }
        if (out_bytes_read) {
          *out_bytes_read = served;
        }
        return served > 0 ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
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
          if (out_bytes_read) {
            *out_bytes_read = got;
          }
          return got > 0 ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
        }
      }
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
