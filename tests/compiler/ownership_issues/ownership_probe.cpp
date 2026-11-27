// Standalone probe for cplus::libcEffect() and the authoritative summary
// predicate. Built by run_suite.py against the freshly compiled
// compiler/Analysis/OwnershipAnalysis.cpp so a stale object inside
// libcplus_core.a cannot mask the regression.
#include "Analysis/OwnershipAnalysis.h"

#include <iostream>

static const char *b(bool v) { return v ? "1" : "0"; }

int main() {
  static const char *names[] = {
      "strncpy",   "strncat",      "memcpy",     "memset",
      "strcpy",    "strcat",       "strcpy_s",   "strncpy_s",
      "strcat_s",  "strncat_s",    "memcpy_s",   "memmove_s",
      "memset_s",  "fopen",        "fopen_s",    "cplus_fopen_s",
      "fclose",    "cplus_fclose_s", "fread",    "fwrite",
      "cplus_fread_s", "cplus_fwrite_s",
  };
  for (const char *name : names) {
    cplus::FunctionEffect e = cplus::libcEffect(name);
    std::cout << "effect " << name << " alloc=" << b(e.allocates)
              << " frees=" << b(e.frees) << " borrows=" << b(e.borrows)
              << " writes=" << b(e.writes)
              << " returnsOwned=" << b(e.returnsOwned)
              << " checkedSize=" << b(e.checkedSize)
              << " mayFail=" << b(e.mayFail) << " freedArg=" << e.freedArg
              << " destArg=" << e.destArg << " sizeArg=" << e.sizeArg
              << " category=" << e.category << "\n";
  }
  static const char *verifiedNames[] = {
      "cplus_unverified_sink", "__cplus_unsafe",   "cplus_core_helper",
      "cplus_check_bounds",    "__cplus_bounds_check", "cplus_check_nonnull",
      "cplus_register_allocation", "cplus_release", "cplus_scan_value",
      "malloc",                "cplus_fopen_s",
  };
  for (const char *name : verifiedNames)
    std::cout << "verified " << name << " "
              << b(cplus::hasVerifiedEffectDeclaration(name)) << "\n";
  return 0;
}
