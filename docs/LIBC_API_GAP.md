# libc API surface coverage (evidence, not a safety claim)

Compiler contract functions: 29
Distinct libc functions appearing in corpus sources: 42
Frequently used but WITHOUT a compiler contract: 27

| function | corpus occurrences |
| --- | ---: |
| printf | 132 |
| scanf | 19 |
| pthread_create | 16 |
| pthread_join | 13 |
| fgets | 13 |
| snprintf | 7 |
| puts | 6 |
| fprintf | 5 |
| longjmp | 4 |
| setjmp | 4 |
| getenv | 3 |
| strcmp | 3 |
| pthread_mutex_unlock | 3 |
| pthread_mutex_lock | 3 |
| read | 2 |
| qsort | 2 |
| setenv | 2 |
| close | 2 |
| open | 2 |
| fputs | 2 |
| strerror | 1 |
| fscanf | 1 |
| sscanf | 1 |
| strchr | 1 |
| putenv | 1 |
| memcmp | 1 |
| strstr | 1 |

This table measures contract-JSON surface only: several listed functions (printf, scanf, fgets, ...) have dedicated name-specific AST checks instead. Absence from the JSON is not a safety claim. Extending this list requires agreed semantics with Agent 2 (issue #23) and positive/negative tests before any new diagnostic is enabled.
