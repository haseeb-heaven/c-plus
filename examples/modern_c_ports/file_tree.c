/* Independent modern-C implementation of the file_tree workload.
 * C11, deterministic stdout.  Pools keep stable slots; transient relocation
 * cells follow the scoped transient-allocation pattern of stdlib/scoped_mem.hp.
 * No upstream sample source was copied. */
#include "common.h"

#define MAX_NODES 64
#define MAX_LINKS 64
#define NAME_CAP 32

typedef struct { char name[NAME_CAP]; int first, last, parent_link, is_dir, live; } FsNode;
typedef struct { int child, next, live; } Link;

static FsNode g_nodes[MAX_NODES];
static Link g_links[MAX_LINKS];
static int g_ncap, g_nused, g_nfree[MAX_NODES], g_nfn;
static int g_lcap, g_lused, g_lfree[MAX_LINKS], g_lfn;

static int node_alloc(void) {
    int i = g_nfn ? g_nfree[--g_nfn] : g_ncap++;
    if (i >= MAX_NODES) return -1;
    memset(&g_nodes[i], 0, sizeof(g_nodes[i]));
    g_nodes[i].first = g_nodes[i].last = g_nodes[i].parent_link = -1;
    g_nodes[i].live = 1;
    ++g_nused;
    return i;
}
static void node_release(int i) {
    g_nodes[i].live = 0;
    g_nfree[g_nfn++] = i;
    --g_nused;
}
static int link_cell(void) {
    int i = g_lfn ? g_lfree[--g_lfn] : g_lcap++;
    if (i >= MAX_LINKS) return -1;
    memset(&g_links[i], 0, sizeof(g_links[i]));
    g_links[i].next = -1;
    return i;
}
static void cell_release(int i) { g_lfree[g_lfn++] = i; }
static int link_add(int parent, int child) {
    int c = link_cell();
    if (c < 0) return -1;
    g_links[c].child = child;
    g_links[c].next = -1;
    if (g_nodes[parent].first < 0) g_nodes[parent].first = c;
    else g_links[g_nodes[parent].last].next = c;
    g_nodes[parent].last = c;
    g_nodes[child].parent_link = c;
    ++g_lused;
    return c;
}
static void link_del(int parent, int child) {
    int c = g_nodes[parent].first, prev = -1;
    while (c >= 0 && g_links[c].child != child) { prev = c; c = g_links[c].next; }
    if (c < 0) return;
    if (prev < 0) g_nodes[parent].first = g_links[c].next;
    else g_links[prev].next = g_links[c].next;
    if (g_nodes[parent].last == c) g_nodes[parent].last = prev;
    g_nodes[child].parent_link = -1;
    cell_release(c);
    --g_lused;
}
static int parent_of(int child) {
    int c;
    for (c = 0; c < g_ncap; ++c) {
        int k;
        if (!g_nodes[c].live) continue;
        for (k = g_nodes[c].first; k >= 0; k = g_links[k].next)
            if (g_links[k].child == child) return c;
    }
    return -1;
}
static int make_node(const char *name, int parent, int dir) {
    int i = node_alloc();
    if (i < 0) return -1;
    strncpy(g_nodes[i].name, name, NAME_CAP - 1);
    g_nodes[i].is_dir = dir;
    if (parent >= 0) link_add(parent, i);
    return i;
}
static int find_child(int dir, const char *name) {
    int c;
    for (c = g_nodes[dir].first; c >= 0; c = g_links[c].next) {
        int k = g_links[c].child;
        if (strcmp(g_nodes[k].name, name) == 0) return k;
    }
    return -1;
}
static int resolve(const char *path) {
    int cur = 0;
    const char *p = path;
    if (strcmp(path, "/") == 0) return 0;
    while (*p) {
        char part[NAME_CAP];
        size_t n;
        p += strspn(p, "/");
        if (!*p) break;
        n = strcspn(p, "/");
        if (n >= NAME_CAP) return -1;
        memcpy(part, p, n); part[n] = 0;
        cur = find_child(cur, part);
        if (cur < 0) return -1;
        p += n;
    }
    return cur;
}
static void rm_r(int idx) {
    int c = g_nodes[idx].first;
    while (c >= 0) {
        int next = g_links[c].next;
        rm_r(g_links[c].child);
        c = next;
    }
    {
        int p = parent_of(idx);
        if (p >= 0) link_del(p, idx);
    }
    node_release(idx);
}
static void move_node(int child, int newparent) {
    int old = parent_of(child);
    /* Relinking reserves a temporary cell before the old edge is released. */
    int tmp = link_cell();
    if (old < 0 || tmp < 0) return;
    link_add(newparent, child);
    link_del(old, child);
    cell_release(tmp);
}
static void print_tree(int idx, int depth) {
    int i, c;
    for (i = 0; i < depth; ++i) printf("  ");
    if (depth == 0) printf("/");
    else printf("%s%s", g_nodes[idx].name, g_nodes[idx].is_dir ? "/" : "");
    printf("\n");
    for (c = g_nodes[idx].first; c >= 0; c = g_links[c].next)
        print_tree(g_links[c].child, depth + 1);
}
static void show(const char *label) {
    printf("-- %s: %d nodes in %d slots, %d links in %d cells\n",
           label, g_nused, g_ncap, g_lused, g_lcap);
    print_tree(0, 0);
}

int main(void) {
    int ann, carl, etc, i;
    static int probe[3] = {3, 1, 2};
    (void)report; (void)fails;
    qsort(probe, 3, sizeof(probe[0]), cmp_int);
    if (probe[2] != 3) return 1;

    /* root, then a small directory tree in insertion order */
    node_alloc();
    {
        int home = make_node("home", 0, 1);
        ann = make_node("ann", home, 1);
        make_node("notes.txt", ann, 0);
        make_node("todo.txt", ann, 0);
        {
            int bob = make_node("bob", home, 1);
            make_node("game.sav", bob, 0);
        }
        etc = make_node("etc", 0, 1);
        make_node("hosts", etc, 0);
    }
    show("built");

    i = resolve("/home/bob");
    if (i >= 0) rm_r(i);
    show("rm -r /home/bob");

    carl = make_node("carl", resolve("/home"), 1);
    make_node("a.txt", carl, 0);
    show("mkdir /home/carl, touch a.txt");

    i = resolve("/home/ann/todo.txt");
    if (i >= 0) move_node(i, resolve("/etc"));
    show("mv /home/ann/todo.txt /etc");

    i = resolve("/home/ann");
    make_node("b.txt", i, 0);
    i = resolve("/home/carl");
    make_node("c.txt", i, 0);
    i = resolve("/etc");
    for (int k = 0; k < 3; ++k) {
        char buf[16];
        snprintf(buf, sizeof(buf), "log%d", k);
        make_node(buf, i, 0);
    }
    show("more files");
    return 0;
}
