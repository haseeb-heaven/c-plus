# Intel safestringlib provenance

- Upstream: https://github.com/intel/safestringlib
- Commit: f4491915cc682556ed64d4eb6a2461d8eed2eb0c
- License: MIT (see LICENSE; includes Cisco/MIT notice from upstream)
- Copied by: Agent 2 (PR #17, issue #22)
- Purpose: optional checked C memory/string APIs for C+ safe CRT layer

## Copied files

### Headers
- include/safe_lib.h
- include/safe_lib_errno.h
- include/safe_mem_lib.h
- include/safe_str_lib.h
- include/safe_types.h

### Implementations / internal deps
- safeclib/memcpy_s.c
- safeclib/memmove_s.c
- safeclib/memset_s.c
- safeclib/memcmp_s.c
- safeclib/memzero_s.c
- safeclib/safe_mem_constraint.c
- safeclib/safe_str_constraint.c
- safeclib/ignore_handler_s.c
- safeclib/abort_handler_s.c
- safeclib/mem_primitives_lib.c
- safeclib/mem_primitives_lib.h
- safeclib/safe_mem_constraint.h
- safeclib/safe_str_constraint.h
- safeclib/safeclib_private.h
- safeclib/strcpy_s.c
- safeclib/strncpy_s.c
- safeclib/strcat_s.c
- safeclib/strncat_s.c
- safeclib/strnlen_s.c

### Upstream tests copied for reference
- unittests/test_memcpy_s.c
- unittests/test_memmove_s.c
- unittests/test_memset_s.c
- unittests/test_strcpy_s.c
- unittests/test_strcat_s.c

## Local adaptations

- None yet at copy time; any later patch must be listed here with rationale.
