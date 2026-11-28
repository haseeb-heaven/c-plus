/* Logging module: level filtering, structured fields, RAII sinks, cleanup. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/log.hp"

int main(void) {
    error err = error();
    const char *keys[2];
    long long values[2];
    char line[256];
    int seen = 0;
    int filtered = 0;
    int fields = 0;
    int located = 0;

    char path[64];
    snprintf(path, sizeof(path), "/tmp/cplus_log_%d.log", 4242);
    unlink(path);

    {
        log_sink sink = log_open_file(path, LOG_INFO, &err);
        print("open", sink.valid(), sink.level() == LOG_INFO);
        if (!sink.valid())
            return 1;

        sink.write(LOG_TRACE, "hidden", NULL, NULL, 0, "t.c", 1, &err);
        sink.write(LOG_INFO, "visible", NULL, NULL, 0, "t.c", 2, &err);
        keys[0] = "user";
        values[0] = 42;
        keys[1] = "attempt";
        values[1] = 3;
        sink.write(LOG_ERROR, "failed", keys, values, 2, "t.c", 7, &err);
        sink.set_level(LOG_DEBUG);
        sink.info("via method", &err);
        print("sink-api", sink.native() != NULL, sink.level() == LOG_DEBUG);
        sink.warn("warned", &err);
        sink.failure("errored", &err);
        print("write-status", err.ok());
    }                                               /* RAII closes the file */

    FILE *reader = fopen(path, "r");
    if (reader != NULL) {
        while (fgets(line, sizeof(line), reader) != NULL) {
            seen++;
            if (strstr(line, "hidden") != NULL)
                filtered++;
            if (strstr(line, "user=42") != NULL && strstr(line, "attempt=3") != NULL)
                fields++;
            if (strstr(line, "ERROR") != NULL)
                located++;
        }
        fclose(reader);
    }
    print("lines", seen, "filtered", filtered, "fields", fields, "error-lines",
          located);

    print("level-name", strcmp(log_level_name(LOG_WARN), "WARN") == 0,
          strcmp(log_level_name(99), "UNKNOWN") == 0);
    print("level-parse", log_level_parse("debug", &err) == LOG_DEBUG,
          log_level_parse("nonsense", &err) == -1,
          err.category() == STDLIB_CATEGORY_INVALID);

    int stderr_fd = fileno(stderr);
    {
        log_sink borrowed = log_stderr(LOG_WARN);
        print("borrowed", borrowed.valid(), borrowed.write(LOG_INFO, "quiet",
                                                           NULL, NULL, 0, NULL,
                                                           0, &err) == STDLIB_OK);
    }                                               /* drop must not close it */
    print("stderr-open", fcntl(stderr_fd, F_GETFD) != -1);

    log_sink console = log_stdout(LOG_FATAL);
    print("stdout-sink", console.valid(), console.enabled(LOG_FATAL));
    log_sink missing = log_open_file("/nonexistent/dir/x.log", LOG_INFO, &err);
    print("open-fail", missing.valid() == 0, err.category() == STDLIB_CATEGORY_IO);
    log_sink zero;
    print("zero-sink", zero.valid() == 0);
    zero.drop();
    zero.drop();                                    /* double drop is inert */
    unlink(path);
    return 0;
}
