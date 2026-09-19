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
#include <chrono>
#include <cstring>
#include <fstream>

// dbz1_diag_logging is defined in src/system/dbz1_diag_flags.cpp (shared runtime).
REXCVAR_DECLARE(bool, dbz1_diag_logging);
// dbz3 I/O diagnostics/readahead cvars are defined in src/filesystem/afs.cpp.
REXCVAR_DECLARE(bool, dbz3_io_logging);
REXCVAR_DECLARE(bool, dbz3_io_readahead);
REXCVAR_DECLARE(int32_t, dbz3_io_readahead_kb);

// dbz3 - sequential readahead for AFS containers.
//
// Host reads are synchronous (one ReadFile per guest read), so a burst of small
// sequential reads -- loading screens and ADX audio streaming do hundreds per
// transition -- turns into one disk seek per read. On a mechanical drive that is
// exactly the "va lento / tirones" profile users report. When the guest reads
// sequentially inside a container, we read a bigger chunk once and serve the
// following reads from RAM (bounded per stream, adaptive 256 KB .. _kb). Purely
// a cache: the bytes come from the same file at the same offsets, so the result
// is identical; it is skipped entirely while mods are installed (their reads are
// remapped) and can be turned off with `dbz3_io_readahead=false`.
REXCVAR_DEFINE_BOOL(dbz3_io_readahead, true, "DBZ3/Dev",
                    "Read AFS containers ahead of the guest (slow disks)");
REXCVAR_DEFINE_INT32(dbz3_io_readahead_kb, 2048, "DBZ3/Dev",
                     "Maximum readahead window per AFS container, in KB");

namespace rex::filesystem {

namespace {

constexpr size_t kReadaheadStreams = 4;
constexpr size_t kReadaheadMinChunk = 256 * 1024;
constexpr size_t kReadaheadMaxChunk = 64 * 1024 * 1024;

int64_t IoNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct ReadaheadStream {
  std::filesystem::path path;
  std::vector<uint8_t> buffer;
  uint64_t window_start = 0;  // file offset of buffer[0]
  size_t window_size = 0;     // valid bytes in buffer
  uint64_t last_end = 0;      // end offset of the last request served
  size_t chunk = kReadaheadMinChunk;
  uint64_t stamp = 0;
};

std::mutex g_rh_mutex;
ReadaheadStream g_rh[kReadaheadStreams];
uint64_t g_rh_stamp = 0;

// Caller holds g_rh_mutex. Returns the stream for `path`, reusing the least
// recently used slot when there is no entry yet.
ReadaheadStream* ReadaheadStreamFor(const std::filesystem::path& path) {
  ReadaheadStream* oldest = &g_rh[0];
  for (auto& stream : g_rh) {
    if (stream.path == path) {
      return &stream;
    }
    if (stream.stamp < oldest->stamp) {
      oldest = &stream;
    }
  }
  oldest->path = path;
  oldest->buffer.clear();
  oldest->buffer.shrink_to_fit();
  oldest->window_start = 0;
  oldest->window_size = 0;
  oldest->last_end = 0;
  oldest->chunk = kReadaheadMinChunk;
  return oldest;
}

void ReadaheadInvalidate(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(g_rh_mutex);
  for (auto& stream : g_rh) {
    if (stream.path == path) {
      stream.buffer.clear();
      stream.window_size = 0;
      stream.last_end = 0;
    }
  }
}

// Serves [offset, offset+size) from the container's readahead window, refilling
// it with one bigger read when the access is sequential. Returns true when the
// request was answered from the cache (got bytes, possibly short at EOF); false
// means "take the normal path" (disabled, first access, random seek, or a read
// error/EOF that the caller must report exactly like before).
bool ReadaheadRead(const std::filesystem::path& path, FileHandle& file, uint64_t file_size,
                   uint64_t offset, uint8_t* dst, size_t size, size_t& got) {
  if (!REXCVAR_GET(dbz3_io_readahead) || size == 0) {
    return false;
  }
  const uint64_t limit_kb = uint64_t(REXCVAR_GET(dbz3_io_readahead_kb));
  if (limit_kb == 0) {
    return false;
  }
  const size_t max_chunk = size_t(std::min<uint64_t>(limit_kb * 1024, kReadaheadMaxChunk));

  std::lock_guard<std::mutex> lock(g_rh_mutex);
  ReadaheadStream* stream = ReadaheadStreamFor(path);
  stream->stamp = ++g_rh_stamp;

  // 1) The request fits in the window we already have in RAM.
  if (stream->window_size && offset >= stream->window_start &&
      offset + size <= stream->window_start + stream->window_size) {
    std::memcpy(dst, stream->buffer.data() + (offset - stream->window_start), size);
    stream->last_end = offset + size;
    got = size;
    return true;
  }

  // 2) Not sequential (or nothing cached): drop the window and let the normal
  //    path serve this read. The next read decides whether to read ahead.
  if (offset != stream->last_end) {
    stream->window_size = 0;
    stream->last_end = offset + size;
    return false;
  }

  // 3) Sequential: read one bigger chunk and serve the request from it.
  size_t chunk = std::min(max_chunk, stream->chunk);
  if (file_size) {
    if (offset >= file_size) {
      stream->window_size = 0;
      stream->last_end = offset + size;
      return false;
    }
    chunk = size_t(std::min<uint64_t>(chunk, file_size - offset));
  }
  if (chunk < size) {
    chunk = size;
  }
  stream->buffer.resize(chunk);
  size_t read = 0;
  if (!file.Read(size_t(offset), stream->buffer.data(), chunk, &read)) {
    stream->window_size = 0;
    return false;
  }
  stream->buffer.resize(read);
  stream->window_start = offset;
  stream->window_size = read;
  stream->chunk = std::min(max_chunk, chunk * 2);
  stream->last_end = offset + size;
  const size_t served = std::min(size, read);
  if (served) {
    std::memcpy(dst, stream->buffer.data(), served);
  }
  got = served;
  return served > 0;
}

}  // namespace

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

  const bool io_logging = REXCVAR_GET(dbz3_io_logging);
  const int64_t time_begin = io_logging ? IoNowNs() : 0;
  const bool diag = REXCVAR_GET(dbz1_diag_logging);

  HostPathEntry* host_entry = entry() ? static_cast<HostPathEntry*>(entry()) : nullptr;
  const std::filesystem::path* host_path = host_entry ? &host_entry->host_path() : nullptr;
  const bool is_afs = host_path && host_path->extension() == ".afs";
  // With no mod installed there is no override and no virtual table to consult:
  // the whole lookup block below is skipped. It used to build a map key string
  // per lookup, take two locks and -- for data_cmn.afs -- copy the complete
  // virtual table on every read.
  const bool has_mods = is_afs && AfsModsPresent();
  int entry_index = -1;

  if (host_entry && (diag || has_mods)) {
    const std::filesystem::path& path = *host_path;

    // TEMP diagnostic: log every AFS read mapped to its entry index, so a play
    // session can reveal which data_cmn/data_eng entries each character/stage
    // loads (roster -> bin mapping). Gated by dbz1_diag_logging (F10/dev
    // toggle) so no file is written in normal play. Writes dbz1_afs_reads.log.
    if (diag) {
      uint64_t entry_start = 0, entry_size = 0;
      entry_index = AfsFindEntry(path, byte_offset, entry_start, entry_size);
      if (entry_index >= 0) {
        std::ofstream diag_file("dbz1_afs_reads.log", std::ios::app);
        if (diag_file) {
          diag_file << path.filename().string() << " entry=" << entry_index << std::hex
                    << " off=0x" << byte_offset << " n=0x" << buffer.size() << " eoff=0x"
                    << entry_start << " esize=0x" << entry_size << std::dec << std::endl;
        }
      }
    }

    if (has_mods) {
      // Virtual mid-insert AFS table: presents a consistent AFS table where
      // overridden entries larger than their slot grow in place (like a rebuilt
      // AFS). The guest then allocates a buffer large enough for the mod bin, and
      // data reads are served consistently: every byte range is translated to the
      // physical file (or the override), and anything outside an entry (header,
      // gap, pad, EOF) is served as zeros -- NEVER as a physical read at a stale
      // offset, which would pull garbage from the middle of another entry and
      // crash the guest's sub-block parser.
      if (path.filename() == "data_cmn.afs") {
        bool any_growth = false;
        const std::vector<uint8_t>* vtable = AfsGetVirtualTableFast(path, any_growth);
        if (any_growth && vtable) {
          size_t served = 0;
          uint64_t off = byte_offset;
          const uint64_t end = byte_offset + buffer.size();
          const size_t vh_size = vtable->size();
          while (off < end) {
            std::filesystem::path src;
            uint64_t src_off = 0, run = 0;
            uint64_t chunk;
            if (off < vh_size) {
              // Header+table region: serve the virtual table bytes.
              chunk = std::min<uint64_t>(vh_size - off, end - off);
              std::memcpy(buffer.data() + served, vtable->data() + off, size_t(chunk));
            } else if (AfsVirtualRange(path, off, src, src_off, run) && run > 0) {
              chunk = std::min(run, end - off);
              if (src.empty()) {
                // Gap/pad region: zeros (never leak physical bytes).
                std::memset(buffer.data() + served, 0, size_t(chunk));
              } else if (src == path) {
                size_t got = 0;
                if (!file_handle_->Read(src_off, buffer.data() + served, size_t(chunk), &got)) {
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
      const int mod_entry = AfsFindEntry(path, byte_offset, entry_start, entry_size);
      if (entry_index < 0) {
        entry_index = mod_entry;
      }
      if (mod_entry >= 0) {
        std::filesystem::path mod_path;
        if (AfsFindModOverride(path, mod_entry, mod_path)) {
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
  }

  const int64_t time_pre = io_logging ? IoNowNs() : 0;

  // Physical read, with the sequential readahead cache in front of it (only for
  // AFS containers without mods, where a byte offset maps 1:1 to the file). The
  // cache is keyed by the file the handle actually opened, and only used when it
  // is the same file as the entry (region/audio remaps open a different one, and
  // then the entry size would not describe it).
  size_t got = 0;
  bool from_cache = false;
  const std::filesystem::path& phys_path = file_handle_->path();
  if (is_afs && !has_mods && host_path && *host_path == phys_path &&
      ReadaheadRead(phys_path, *file_handle_, host_entry->size(), byte_offset, buffer.data(),
                    buffer.size(), got)) {
    from_cache = true;
  } else if (!file_handle_->Read(byte_offset, buffer.data(), buffer.size(), &got)) {
    if (out_bytes_read) {
      *out_bytes_read = 0;
    }
    if (io_logging) {
      const int64_t now = IoNowNs();
      AfsIoRecordRead({host_path, entry_index, byte_offset, buffer.size(),
                       uint64_t(time_pre - time_begin), uint64_t(now - time_pre), false});
    }
    return X_STATUS_END_OF_FILE;
  }

  // The original code passed `out_bytes_read` straight to FileHandle::Read; now
  // the count goes through a local (readahead may answer without touching the
  // file), so it MUST be written back or the caller reads uninitialized memory
  // and the guest hangs on its first container read.
  if (out_bytes_read) {
    *out_bytes_read = got;
  }
  if (io_logging) {
    const int64_t now = IoNowNs();
    AfsIoRecordRead({host_path, entry_index, byte_offset, buffer.size(),
                     uint64_t(time_pre - time_begin),
                     from_cache ? 0 : uint64_t(now - time_pre), from_cache});
  }
  return X_STATUS_SUCCESS;
}

X_STATUS HostPathFile::WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access_ &
        (FileAccess::kGenericWrite | FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  ReadaheadInvalidate(file_handle_->path());
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

  ReadaheadInvalidate(file_handle_->path());
  if (file_handle_->SetLength(length)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

}  // namespace rex::filesystem
