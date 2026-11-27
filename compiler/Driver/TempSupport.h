//===- TempSupport.h - driver temp-file helpers -----------------*- C++ -*-===//
//
// Internal helpers shared by the C+ driver and its focused regression tests.
// Kept header-only so the small unit tests do not need to link the whole
// cplus_core library.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_DRIVER_TEMPSUPPORT_H
#define CPLUS_DRIVER_TEMPSUPPORT_H

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <system_error>
#include <utility>

namespace cplus {
namespace driver_detail {

/// RAII owner of a temporary file path. Move-assignment must remove the file
/// it currently owns, otherwise an overwritten temp file is stranded on disk
/// even on a clean driver exit (issue #205).
class TempFile {
public:
  std::string path;
  bool keep = false;

  TempFile() = default;
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;

  TempFile(TempFile &&other) noexcept
      : path(std::move(other.path)), keep(other.keep) {
    other.path.clear();
  }

  TempFile &operator=(TempFile &&other) noexcept {
    if (this != &other) {
      cleanup(); // remove the temp file we owned before the overwrite
      path = std::move(other.path);
      keep = other.keep;
      other.path.clear();
    }
    return *this;
  }

  ~TempFile() { cleanup(); }

private:
  void cleanup() noexcept {
    if (!keep && !path.empty())
      llvm::sys::fs::remove(path);
  }
};

/// Write `text` to `path`. Open, write, flush and close errors all fail
/// loudly: silently returning success on a short/disk-full write would feed a
/// truncated C source file to the backend (issue #205).
inline bool writeFile(const std::string &path, const std::string &text,
                      std::string *err) {
  std::error_code ec;
  llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    *err = ec.message();
    return false;
  }
  os << text;
  os.flush();
  if (!os.has_error())
    os.close();
  if (os.has_error()) {
    *err = os.error().message();
    if (err->empty())
      *err = "failed to write file";
    os.clear_error();
    return false;
  }
  return true;
}

} // namespace driver_detail
} // namespace cplus

#endif // CPLUS_DRIVER_TEMPSUPPORT_H
