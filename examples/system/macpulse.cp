/*
 * MacPulse — native macOS system monitor for C+
 * Build: ../build/c+ build macpulse.cp -o macpulse && ./macpulse
 * Requires macOS (Mach, libproc, BSD interfaces). No third-party libraries.
 * C+ language features: class, this->, method calls, auto, resource RAII,
 *                       typed print(), IntVec from the .hp standard library.
 * Terminal: q quit, space pause, +/- refresh, 1 overview, 2 processes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <libproc.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <cplus/cplus.h>
#include "stdlib/vector.hp"

#define MAX_PROCS 2048
#define SHOW_PROCS 12
#define PULSE_HISTORY 480

static volatile sig_atomic_t stop_requested = 0;
static void request_stop(int sig) { stop_requested = sig ? 1 : 0; }

resource struct Terminal {
    struct termios original;
    int active;
};

void Terminal_drop(Terminal *t) {
    if (t->active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &t->original);
        t->active = 0;
    }
    fputs("\033[?25h\033[?1049l\033[0m", stdout);
    fflush(stdout);
}

Terminal Terminal_open(void) {
    Terminal t = {0};
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &t.original) == 0) {
        struct termios raw = t.original;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
            t.active = 1;
    }
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
    fflush(stdout);
    return t;
}

typedef struct {
    int pid;
    char name[64];
    uint64_t resident;
    double cpu_seconds;
} ProcEntry;

static int compare_resident(const void *a, const void *b) {
    const ProcEntry *x = a;
    const ProcEntry *y = b;
    if (x->resident < y->resident) return 1;
    if (x->resident > y->resident) return -1;
    return x->pid - y->pid;
}

static void units(double n, char *out, size_t cap) {
    const char *u[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    int i = 0;
    while (n >= 1024.0 && i < 4) { n /= 1024.0; ++i; }
    snprintf(out, cap, "%.1f %s", n, u[i]);
}

static void rate(double n, char *out, size_t cap) {
    char tmp[32];
    units(n, tmp, sizeof tmp);
    snprintf(out, cap, "%s/s", tmp);
}

static int terminal_columns(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col >= 65)
        return w.ws_col;
    return 90;
}
static int terminal_rows(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row >= 20)
        return w.ws_row;
    return 30;
}

static void bar(double pct, int width) {
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    int blocks = (int)((pct * width / 100.0) + 0.5);
    const char *tone = pct >= 88.0 ? "\033[91m" : pct >= 65.0 ? "\033[93m" : "\033[92m";
    fputs(tone, stdout);
    for (int i = 0; i < width; ++i) putchar(i < blocks ? '#' : '.');
    fputs("\033[0m", stdout);
}

static void spark(IntVec *v, int width) {
    const char *levels[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int n = v->len();
    int begin = n > width ? n - width : 0;
    int pad = width - (n - begin);
    for (int k = 0; k < pad; ++k) putchar(' ');
    for (int i = begin; i < n; ++i) {
        int p = v->at(i);
        int index = (p + 6) / 13;
        if (index < 0) index = 0;
        if (index > 8) index = 8;
        fputs(levels[index], stdout);
    }
}

class Monitor {
    uint64_t previous_cpu[4];
    uint64_t previous_in;
    uint64_t previous_out;
    double previous_time;
    double cpu;
    double mem_percent;
    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t swap_used;
    uint64_t swap_total;
    uint64_t disk_used;
    uint64_t disk_total;
    double net_in;
    double net_out;
    double load1;
    double load5;
    double load15;
    unsigned cpu_cores;
    unsigned long process_count;
    long uptime;
    int proc_count;
    ProcEntry top[SHOW_PROCS];

    void sample_cpu(void) {
        host_cpu_load_info_data_t info;
        mach_msg_type_number_t len = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                            (host_info_t)&info, &len) != KERN_SUCCESS) return;
        uint64_t diff[4] = {0};
        uint64_t total = 0;
        for (int i = 0; i < 4; ++i) {
            uint64_t val = info.cpu_ticks[i];
            diff[i] = val >= this->previous_cpu[i] ? val - this->previous_cpu[i] : 0;
            this->previous_cpu[i] = val;
            total += diff[i];
        }
        if (total > 0 && this->previous_time > 0.0)
            this->cpu = 100.0 * (double)(total - diff[CPU_STATE_IDLE]) / (double)total;
    }

    void sample_memory(void) {
        size_t n = sizeof(this->memory_total);
        sysctlbyname("hw.memsize", &this->memory_total, &n, NULL, 0);
        vm_statistics64_data_t vm = {0};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info64_t)&vm, &count) == KERN_SUCCESS) {
            vm_size_t page_size = 0;
            host_page_size(mach_host_self(), &page_size);
            uint64_t available_pages = (uint64_t)vm.free_count +
                (uint64_t)vm.inactive_count + (uint64_t)vm.speculative_count;
            uint64_t available = available_pages * (uint64_t)page_size;
            this->memory_used = available < this->memory_total ?
                this->memory_total - available : 0;
            if (this->memory_total)
                this->mem_percent = 100.0 * (double)this->memory_used /
                                    (double)this->memory_total;
        }
        struct xsw_usage swap = {0};
        n = sizeof swap;
        if (sysctlbyname("vm.swapusage", &swap, &n, NULL, 0) == 0) {
            this->swap_used = swap.xsu_used;
            this->swap_total = swap.xsu_total;
        }
    }

    void sample_disk(void) {
        struct statfs fs;
        if (statfs("/", &fs) == 0) {
            this->disk_total = (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize;
            uint64_t free_bytes = (uint64_t)fs.f_bfree * (uint64_t)fs.f_bsize;
            this->disk_used = this->disk_total > free_bytes ?
                this->disk_total - free_bytes : 0;
        }
    }

    void sample_network(double now) {
        struct ifaddrs *all = NULL;
        uint64_t in_bytes = 0, out_bytes = 0;
        if (getifaddrs(&all) == 0) {
            for (struct ifaddrs *p = all; p; p = p->ifa_next) {
                if (!p->ifa_addr || p->ifa_addr->sa_family != AF_LINK ||
                    !(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK) ||
                    !p->ifa_data) continue;
                const struct if_data *d = (const struct if_data *)p->ifa_data;
                in_bytes += (uint64_t)d->ifi_ibytes;
                out_bytes += (uint64_t)d->ifi_obytes;
            }
            freeifaddrs(all);
        }
        double dt = now - this->previous_time;
        if (this->previous_time > 0.0 && dt > 0.0) {
            this->net_in = in_bytes >= this->previous_in ?
                (double)(in_bytes - this->previous_in) / dt : 0.0;
            this->net_out = out_bytes >= this->previous_out ?
                (double)(out_bytes - this->previous_out) / dt : 0.0;
        }
        this->previous_in = in_bytes;
        this->previous_out = out_bytes;
    }

    void sample_processes(void) {
        int pids[MAX_PROCS] = {0};
        int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof pids);
        if (bytes <= 0) { this->proc_count = 0; return; }
        int count = bytes / (int)sizeof(int);
        this->process_count = (unsigned long)count;
        ProcEntry *entries = calloc((size_t)count, sizeof(ProcEntry));
        if (!entries) { this->proc_count = 0; return; }
        int used = 0;
        for (int i = 0; i < count; ++i) {
            if (pids[i] <= 0) continue;
            struct proc_taskinfo task = {0};
            if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &task,
                             sizeof task) != sizeof task) continue;
            ProcEntry *entry = &entries[used++];
            entry->pid = pids[i];
            entry->resident = task.pti_resident_size;
            entry->cpu_seconds = (double)(task.pti_total_user + task.pti_total_system) / 1e9;
            if (proc_name(pids[i], entry->name, sizeof entry->name) <= 0)
                snprintf(entry->name, sizeof entry->name, "PID-%d", pids[i]);
        }
        qsort(entries, (size_t)used, sizeof entries[0], compare_resident);
        this->proc_count = used < SHOW_PROCS ? used : SHOW_PROCS;
        for (int i = 0; i < this->proc_count; ++i)
            this->top[i] = entries[i];
        free(entries);
    }

    void sample(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double now = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
        this->sample_cpu();
        this->sample_memory();
        this->sample_disk();
        this->sample_network(now);
        this->sample_processes();
        double loads[3] = {0};
        if (getloadavg(loads, 3) == 3) {
            this->load1 = loads[0]; this->load5 = loads[1]; this->load15 = loads[2];
        }
        size_t n = sizeof(this->cpu_cores);
        sysctlbyname("hw.ncpu", &this->cpu_cores, &n, NULL, 0);
        struct timeval boot = {0};
        n = sizeof boot;
        if (sysctlbyname("kern.boottime", &boot, &n, NULL, 0) == 0)
            this->uptime = (long)(tv.tv_sec - boot.tv_sec);
        this->previous_time = now;
    }

    void render(IntVec *cpu_hist, IntVec *ram_hist, int page, int period_ms,
                int paused) {
        char m_used[32], m_total[32], swap_u[32], swap_t[32];
        char d_used[32], d_total[32], down[32], up[32];
        units((double)this->memory_used, m_used, sizeof m_used);
        units((double)this->memory_total, m_total, sizeof m_total);
        units((double)this->swap_used, swap_u, sizeof swap_u);
        units((double)this->swap_total, swap_t, sizeof swap_t);
        units((double)this->disk_used, d_used, sizeof d_used);
        units((double)this->disk_total, d_total, sizeof d_total);
        rate(this->net_in, down, sizeof down);
        rate(this->net_out, up, sizeof up);
        int cols = terminal_columns();
        int width = cols - 15;
        if (width > 70) width = 70;
        if (width < 20) width = 20;
        fputs("\033[H\033[2J", stdout);
        fputs("\033[1;36m  MACPULSE  \033[0m\033[2m  Native macOS monitor written in C+\033[0m\n", stdout);
        printf("  \033[2mCPU cores: %u    Processes: %lu    Uptime: %ldh %02ldm    Refresh: %dms  %s\033[0m\n",
               this->cpu_cores, this->process_count, this->uptime / 3600,
               (this->uptime / 60) % 60, period_ms, paused ? "[PAUSED]" : "");
        fputs("  \033[90m────────────────────────────────────────────────────────────────────────────\033[0m\n", stdout);
        printf("  \033[1mCPU\033[0m    %5.1f%%  ", this->cpu); bar(this->cpu, 34);
        printf("   Load %.2f / %.2f / %.2f\n", this->load1, this->load5, this->load15);
        printf("  \033[1mMEM\033[0m    %5.1f%%  ", this->mem_percent); bar(this->mem_percent, 34);
        printf("   %s / %s\n", m_used, m_total);
        double disk_pct = this->disk_total ? 100.0 * (double)this->disk_used / (double)this->disk_total : 0.0;
        printf("  \033[1mDISK\033[0m   %5.1f%%  ", disk_pct); bar(disk_pct, 34);
        printf("   %s / %s (/)\n", d_used, d_total);
        printf("  \033[1mSWAP\033[0m   %s / %s    \033[1mNET ↓\033[0m %s    \033[1m↑\033[0m %s\n",
               swap_u, swap_t, down, up);
        fputs("\n  \033[1;35mLIVE HISTORY\033[0m  (right = newest)\n", stdout);
        fputs("  CPU  \033[92m", stdout); spark(cpu_hist, width); fputs("\033[0m\n", stdout);
        fputs("  RAM  \033[96m", stdout); spark(ram_hist, width); fputs("\033[0m\n", stdout);
        fputs("\n  \033[1;35mTOP PROCESSES BY RESIDENT MEMORY\033[0m\n", stdout);
        fputs("  \033[2mPID       RESIDENT      TOTAL CPU    PROCESS\033[0m\n", stdout);
        int available = terminal_rows() - 18;
        int maxrows = page == 2 ? this->proc_count : 7;
        if (maxrows > available) maxrows = available;
        if (maxrows > this->proc_count) maxrows = this->proc_count;
        for (int i = 0; i < maxrows; ++i) {
            const ProcEntry *p = &this->top[i];
            char bytes[32];
            units((double)p->resident, bytes, sizeof bytes);
            printf("  %-8d  %-12s %9.1fs    %.35s\n",
                   p->pid, bytes, p->cpu_seconds, p->name);
        }
        fputs("\n  \033[90m[q] quit    [space] pause/resume    [+/-] refresh    [1/2] view\033[0m", stdout);
        fflush(stdout);
    }
};

int main(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("MacPulse needs an interactive terminal.\n", stderr);
        return 1;
    }
    struct sigaction action = {0};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    {
    Terminal terminal = Terminal_open();
    Monitor monitor = {0};
    IntVec cpu_history = IntVec();
    IntVec ram_history = IntVec();
    auto period_ms = 1000;
    auto paused = 0;
    auto page = 1;
    double next_sample = 0.0;
    while (!stop_requested) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double now = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
        if (!paused && now >= next_sample) {
            monitor.sample();
            cpu_history.push((int)(monitor.cpu + 0.5));
            ram_history.push((int)(monitor.mem_percent + 0.5));
            monitor.render(&cpu_history, &ram_history, page, period_ms, paused);
            next_sample = now + period_ms / 1000.0;
        }
        char key;
        ssize_t n = read(STDIN_FILENO, &key, 1);
        if (n == 1) {
            if (key == 'q' || key == 'Q') break;
            if (key == ' ') paused = !paused;
            if (key == '1') page = 1;
            if (key == '2') page = 2;
            if (key == '+' && period_ms > 250) period_ms -= 250;
            if (key == '-' && period_ms < 4000) period_ms += 250;
            if (paused) monitor.render(&cpu_history, &ram_history, page, period_ms, paused);
            if (!paused) next_sample = 0.0;
        }
        struct timespec delay = {0, 50000000};
        nanosleep(&delay, NULL);
    }
    /* Terminal and both IntVec instances drop automatically here. */
    }
    print("MacPulse stopped. Terminal restored.");
    return 0;
}
