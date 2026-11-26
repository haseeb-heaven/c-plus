/* Independent modern-C implementation of the serialize workload.
 * C11, deterministic stdout.  Transient buffers use a scoped scratch region,
 * following the one-store release pattern of stdlib/scoped_mem.hp.
 * No upstream sample source was copied. */
#include "common.h"
#include <stdint.h>

#define NW 262
#define WORD_CAP 29
#define NODE_SIZE 37
#define PREFIX_SIZE 2
#define TAG_SIZE 4
#define NONE 0xFFFFu

typedef struct { unsigned short left, right; unsigned int count; char word[WORD_CAP]; } Node;

static const char *const kWords[NW] = {
    "alice", "rabbit", "down", "think", "curiosity", "am", "an", "as",
    "at", "be", "by", "do", "go", "he", "hi", "if",
    "in", "is", "it", "me", "my", "no", "of", "on",
    "or", "so", "the", "and", "for", "are", "but", "not",
    "you", "all", "can", "had", "her", "was", "one", "our",
    "out", "day", "get", "has", "him", "his", "how", "man",
    "new", "now", "old", "see", "two", "way", "who", "boy",
    "did", "its", "let", "put", "say", "she", "too", "use",
    "dad", "mom", "cat", "dog", "egg", "fox", "hat", "jar",
    "key", "log", "map", "net", "owl", "pen", "rat", "sun",
    "top", "van", "wax", "yes", "zoo", "air", "bed", "cup",
    "dry", "ear", "eye", "arm", "leg", "lip", "pin", "red",
    "bus", "cow", "dew", "few", "fig", "fun", "gap", "gun",
    "hop", "ice", "jam", "kid", "low", "mud", "nut", "oak",
    "pig", "row", "tea", "web", "zip", "oar", "sim", "sin",
    "sip", "able", "acid", "aged", "also", "area", "back", "ball",
    "band", "bank", "base", "bath", "beam", "bean", "bell", "belt",
    "bend", "best", "bird", "bite", "blow", "blue", "boat", "body",
    "boil", "bold", "book", "boot", "bore", "born", "both", "bowl",
    "burn", "call", "calm", "came", "card", "care", "case", "cash",
    "cast", "cave", "cell", "chat", "chef", "chin", "chip", "city",
    "clap", "claw", "clay", "clip", "club", "coat", "code", "coil",
    "coin", "cold", "cook", "cool", "cope", "copy", "cord", "core",
    "cork", "corn", "cost", "cozy", "crab", "crew", "crop", "cube",
    "curl", "cute", "dark", "dash", "data", "dawn", "deal", "dean",
    "dear", "debt", "deck", "deed", "deep", "deer", "desk", "dial",
    "dice", "diet", "dime", "dirt", "dish", "disk", "dive", "dock",
    "does", "dome", "done", "doom", "door", "dose", "draw", "drew",
    "drip", "drop", "drum", "dual", "duck", "dull", "dumb", "dune",
    "dusk", "dust", "duty", "each", "earn", "east", "ease", "easy",
    "echo", "edge", "edit", "else", "even", "ever", "evil", "exit",
    "face", "fact", "fade", "fail", "fair", "fall", "farm", "fast",
    "fate", "fear", "feed", "feel", "feet", "fell"
};

static Node g_nodes[NW];
static const char *g_sorted[NW];
static int g_count, g_root;

/* ---- scoped transient scratch -------------------------------------------- */
typedef struct { unsigned char *base; size_t cap, top; } Scratch;
static void scratch_init(Scratch *s, size_t cap) {
    s->base = (unsigned char *)malloc(cap ? cap : 1);
    s->cap = cap; s->top = 0;
}
static void *scratch_take(Scratch *s, size_t n) {
    void *p;
    if (s->top + n > s->cap) return NULL;
    p = s->base + s->top; s->top += n; return p;
}
static void scratch_drop(Scratch *s) { s->top = 0; }

/* ---- little-endian helpers ------------------------------------------------ */
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v & 255); p[1] = (unsigned char)(v >> 8); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static void put32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 255); p[1] = (unsigned char)((v >> 8) & 255);
    p[2] = (unsigned char)((v >> 16) & 255); p[3] = (unsigned char)(v >> 24);
}
static unsigned get32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static unsigned extra_count(const char *w) {
    if (strcmp(w, "alice") == 0) return 8;
    if (strcmp(w, "rabbit") == 0) return 5;
    if (strcmp(w, "down") == 0) return 11;
    if (strcmp(w, "think") == 0) return 4;
    return 0;
}
static void set_node(int i, const char *w, int left, int right) {
    Node *n = &g_nodes[i];
    n->left = (unsigned short)left; n->right = (unsigned short)right;
    n->count = 1u + extra_count(w);
    memset(n->word, 0, sizeof(n->word));
    strncpy(n->word, w, WORD_CAP - 1);
}

/* A left spine of k keys plus a balanced right remainder gives a tree of a
 * deliberate, reproducible height without depending on insertion order. */
static int make_chain(int k, int lo) {
    int me = g_count++;
    int left = (k > 1) ? make_chain(k - 1, lo) : (int)NONE;
    set_node(me, g_sorted[lo + k - 1], left, (int)NONE);
    return me;
}
static int make_balanced(int n, int lo) {
    int me, mid, l, r;
    if (n <= 0) return (int)NONE;
    me = g_count++; mid = n / 2;
    l = make_balanced(mid, lo);
    r = make_balanced(n - mid - 1, lo + mid + 1);
    set_node(me, g_sorted[lo + mid], l, r);
    return me;
}
static void build_index(void) {
    int i, spine;
    for (i = 0; i < NW; ++i) g_sorted[i] = kWords[i];
    qsort(g_sorted, NW, sizeof(g_sorted[0]), cmp_str);
    g_count = 0;
    spine = make_chain(16, 0);
    g_root = g_count++;
    set_node(g_root, g_sorted[16], spine, make_balanced(NW - 17, 17));
}
static int tree_depth(int i) {
    int l, r;
    if (i == (int)NONE) return 0;
    l = tree_depth(g_nodes[i].left);
    r = tree_depth(g_nodes[i].right);
    return 1 + (l > r ? l : r);
}
static unsigned lookup(int i, const char *w) {
    while (i != (int)NONE) {
        int c = strcmp(w, g_nodes[i].word);
        if (c == 0) return g_nodes[i].count;
        i = (c < 0) ? g_nodes[i].left : g_nodes[i].right;
    }
    return 0;
}

/* ---- full image: 2-byte length prefix + NW x 37-byte records -------------- */
static size_t serialize_full(unsigned char *dst) {
    int i;
    put16(dst, (unsigned)(NW * NODE_SIZE));
    for (i = 0; i < NW; ++i) {
        unsigned char *p = dst + PREFIX_SIZE + (size_t)i * NODE_SIZE;
        put16(p, g_nodes[i].left); put16(p + 2, g_nodes[i].right);
        put32(p + 4, g_nodes[i].count);
        memset(p + 8, 0, WORD_CAP);
        memcpy(p + 8, g_nodes[i].word, strlen(g_nodes[i].word));
    }
    return PREFIX_SIZE + (size_t)NW * NODE_SIZE;
}

typedef struct { Node *nodes; int n; } Loaded;
static int decode_records(const Node *rec, int n) {
    unsigned char seen[NW];
    int stack[NW], sp = 0, roots = 0, i, j, visited = 0;
    if (n <= 0 || n > NW) return 0;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < n; ++i) {
        size_t l;
        if (rec[i].left != NONE && rec[i].left >= n) return 0;
        if (rec[i].right != NONE && rec[i].right >= n) return 0;
        if (rec[i].count == 0 || rec[i].count > 1000u) return 0;
        l = strlen(rec[i].word);
        if (l < 1 || l >= WORD_CAP) return 0;
        for (j = 0; j < (int)l; ++j) {
            unsigned char c = (unsigned char)rec[i].word[j];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return 0;
        }
        seen[i] = 0;
    }
    for (i = 0; i < n; ++i) {
        if (rec[i].left != NONE) { if (++seen[rec[i].left] > 1) return 0; }
        if (rec[i].right != NONE) { if (++seen[rec[i].right] > 1) return 0; }
    }
    for (i = 0; i < n; ++i) if (seen[i] == 0) { ++roots; stack[sp++] = i; }
    if (roots != 1) return 0;
    memset(seen, 0, (size_t)n);
    while (sp > 0) {
        int k = stack[--sp];
        if (seen[k]) return 0;
        seen[k] = 1; ++visited;
        if (rec[k].left != NONE) stack[sp++] = rec[k].left;
        if (rec[k].right != NONE) stack[sp++] = rec[k].right;
        if (sp > n) return 0;
    }
    return visited == n;
}
static int load_full(const unsigned char *src, size_t len, Loaded *out) {
    unsigned pay; int n, i, ok;
    Node *rec;
    out->nodes = NULL; out->n = 0;
    if (len == 0) return 1;                 /* empty image: success, zero nodes */
    if (len < PREFIX_SIZE) return 0;
    pay = get16(src);
    if ((size_t)pay + PREFIX_SIZE != len || pay % NODE_SIZE != 0) return 0;
    n = (int)(pay / NODE_SIZE);
    if (n <= 0 || n > NW) return 0;
    rec = (Node *)calloc((size_t)n, sizeof(Node));
    if (!rec) return 0;
    for (i = 0; i < n; ++i) {
        const unsigned char *p = src + PREFIX_SIZE + (size_t)i * NODE_SIZE;
        rec[i].left = (unsigned short)get16(p);
        rec[i].right = (unsigned short)get16(p + 2);
        rec[i].count = get32(p + 4);
        memcpy(rec[i].word, p + 8, WORD_CAP);
    }
    ok = decode_records(rec, n);
    if (!ok) { free(rec); return 0; }
    out->nodes = rec; out->n = n;
    return 1;
}

/* ---- compact variable-length image --------------------------------------- */
static size_t serialize_compact(unsigned char *dst) {
    size_t pos = PREFIX_SIZE; int i;
    for (i = 0; i < NW; ++i) {
        size_t l = strlen(g_nodes[i].word);
        dst[pos++] = (unsigned char)l;
        memcpy(dst + pos, g_nodes[i].word, l); pos += l;
        dst[pos++] = (unsigned char)g_nodes[i].count;
        put16(dst + pos, g_nodes[i].left); pos += 2;
        put16(dst + pos, g_nodes[i].right); pos += 2;
    }
    put16(dst, (unsigned)(pos - PREFIX_SIZE));
    return pos;
}
static int load_compact(const unsigned char *src, size_t len, Loaded *out) {
    size_t pos = PREFIX_SIZE; int i;
    Node *rec = (Node *)calloc(NW, sizeof(Node));
    out->nodes = NULL; out->n = 0;
    if (!rec) return 0;
    if (len < PREFIX_SIZE || get16(src) + PREFIX_SIZE != len) { free(rec); return 0; }
    for (i = 0; i < NW; ++i) {
        size_t l;
        if (pos >= len) { free(rec); return 0; }
        l = src[pos++];
        if (l < 1 || l >= WORD_CAP || pos + l + 5 > len) { free(rec); return 0; }
        memcpy(rec[i].word, src + pos, l); pos += l;
        rec[i].count = src[pos++];
        rec[i].left = (unsigned short)get16(src + pos); pos += 2;
        rec[i].right = (unsigned short)get16(src + pos); pos += 2;
    }
    if (pos != len || !decode_records(rec, NW)) { free(rec); return 0; }
    out->nodes = rec; out->n = NW;
    return 1;
}
static unsigned total_counts(const Loaded *l) {
    unsigned t = 0; int i;
    for (i = 0; i < l->n; ++i) t += l->nodes[i].count;
    return t;
}
static void free_loaded(Loaded *l) { free(l->nodes); l->nodes = NULL; l->n = 0; }

int main(void) {
    Scratch sc; unsigned char *img, *tamp, *ext, *cmp, *msg;
    size_t imglen, clen, mlen; unsigned pay; Loaded ld, cl, tl, el, ml;
    int ok, i, match, d, reload, totals, tok, eok, tree, truncated, extended, empty, noprefix, mok;
    const char *queries[6] = {"alice", "rabbit", "down", "think", "curiosity", "nonexistentword"};
    unsigned want[6] = {9, 6, 12, 5, 1, 0};
    unsigned got[6];

    (void)report; (void)fails;   /* helpers from common.h, not needed for stdout */
    build_index();
    scratch_init(&sc, 5 * (PREFIX_SIZE + NW * NODE_SIZE + TAG_SIZE) + 8192);
    img = (unsigned char *)scratch_take(&sc, PREFIX_SIZE + NW * NODE_SIZE + TAG_SIZE);
    imglen = serialize_full(img);
    pay = get16(img);

    printf("indexed %d words from data/alice.txt\n", NW);
    printf("image %zu bytes = %d nodes x %d + %d\n", imglen, NW, NODE_SIZE, PREFIX_SIZE);
    printf("prefix says %u bytes after %d, file has %zu\n", pay, PREFIX_SIZE, imglen - PREFIX_SIZE);

    ok = load_full(img, imglen, &ld);
    printf("loaded %s, %d nodes, same size %s\n", ok ? "true" : "false", ld.n,
           (pay == imglen - PREFIX_SIZE) ? "true" : "false");

    match = 1;
    for (i = 0; i < 6; ++i) {
        got[i] = lookup(g_root, queries[i]);
        if (got[i] != want[i]) match = 0;
        printf("  %s = %u\n", queries[i], got[i]);
    }
    d = tree_depth(g_root);
    printf("answers match %s, depth %d = %d\n", match ? "true" : "false", d, 17);

    cmp = (unsigned char *)scratch_take(&sc, 4096);
    clen = serialize_compact(cmp);
    reload = load_compact(cmp, clen, &cl);
    totals = reload && total_counts(&cl) == 290u;
    printf("compact %zu bytes for %d nodes, reload %s totals %s\n", clen, cl.n,
           reload ? "true" : "false", totals ? "true" : "false");

    tamp = (unsigned char *)scratch_take(&sc, imglen);
    memcpy(tamp, img, imglen);
    put16(tamp + PREFIX_SIZE, (unsigned)NW);   /* impossible child index */
    tok = load_full(tamp, imglen, &tl);
    printf("tampered link %s, nodes %d\n", tok ? "true" : "false", tl.n);

    memcpy(tamp, img, imglen);
    tamp[PREFIX_SIZE + 4] = (unsigned char)(g_nodes[0].count + 1u);
    eok = load_full(tamp, imglen, &el);
    tree = eok && el.n == NW;
    printf("edited count %s, still a tree %s\n", eok ? "true" : "false", tree ? "true" : "false");

    truncated = load_full(img, imglen - 1, &tl);
    ext = (unsigned char *)scratch_take(&sc, imglen + 1);
    memcpy(ext, img, imglen); ext[imglen] = 0;
    extended = load_full(ext, imglen + 1, &el);
    empty = load_full(img, 0, &tl);
    noprefix = load_full(img + PREFIX_SIZE, imglen - PREFIX_SIZE, &el);
    printf("truncated %s, extended %s, empty %s %d, no prefix %s\n",
           truncated ? "true" : "false", extended ? "true" : "false",
           empty ? "true" : "false", tl.n, noprefix ? "true" : "false");

    msg = (unsigned char *)scratch_take(&sc, TAG_SIZE + imglen);
    memcpy(msg, "GSX1", TAG_SIZE);
    memcpy(msg + TAG_SIZE, img, imglen);
    mlen = TAG_SIZE + imglen;
    mok = mlen >= TAG_SIZE + PREFIX_SIZE && memcmp(msg, "GSX1", TAG_SIZE) == 0 &&
          load_full(msg + TAG_SIZE, mlen - TAG_SIZE, &ml);
    printf("message %zu bytes, tag %s, reload %s %s\n", mlen, "GSX1",
           mok ? "true" : "false", (mok && ml.n == NW) ? "true" : "false");

    free_loaded(&ld); free_loaded(&cl); free_loaded(&tl); free_loaded(&el); free_loaded(&ml);
    scratch_drop(&sc); free(sc.base);
    return 0;
}
