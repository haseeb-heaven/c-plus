/* Independent modern-C port of the json sample workload: a small JSON reader,
 * pretty/compact writers, path lookups and malformed-input diagnostics.
 * C11; parse nodes and strings live in a transient scoped bump arena. */
#include "common.h"

static unsigned char arena_bytes[1 << 16];
static size_t arena_top;
static void *scoped_take(size_t n) {
    void *p = arena_bytes + arena_top;
    arena_top = (arena_top + n + 15u) & ~(size_t)15u;
    return p;
}

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct Node Node;
struct Node {
    int type;
    char *text;
    double num;
    int boolean;
    int n;
    char **keys;
    Node **kids;
};
static int node_count;

static Node *new_node(int type) {
    Node *n = scoped_take(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->type = type;
    ++node_count;
    return n;
}

typedef struct { const char *s; size_t p; const char *msg; size_t ep; } JP;
static void seterr(JP *j, const char *msg) { j->msg = msg; j->ep = j->p; }
static void ws(JP *j) {
    while (j->s[j->p] == ' ' || j->s[j->p] == '\t' ||
           j->s[j->p] == '\n' || j->s[j->p] == '\r') ++j->p;
}
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *read_string(JP *j) {
    char *buf;
    size_t n = 0;
    if (j->s[j->p] != '"') { seterr(j, "expected string"); return 0; }
    ++j->p;
    buf = scoped_take(strlen(j->s) + 4);
    while (j->s[j->p] && j->s[j->p] != '"') {
        unsigned char ch = (unsigned char)j->s[j->p];
        if (ch == '\\') {
            char e;
            ++j->p;
            e = j->s[j->p];
            if (!e) { seterr(j, "unterminated string"); return 0; }
            switch (e) {
                case '"': buf[n++] = '"'; break;
                case '\\': buf[n++] = '\\'; break;
                case '/': buf[n++] = '/'; break;
                case 'b': buf[n++] = '\b'; break;
                case 'f': buf[n++] = '\f'; break;
                case 'n': buf[n++] = '\n'; break;
                case 'r': buf[n++] = '\r'; break;
                case 't': buf[n++] = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    int k, h;
                    for (k = 1; k <= 4; ++k) {
                        h = hexval(j->s[j->p + k]);
                        if (h < 0) { seterr(j, "bad escape"); return 0; }
                        cp = (cp << 4) | (unsigned)h;
                    }
                    j->p += 4;
                    if (cp < 0x80) buf[n++] = (char)cp;
                    else if (cp < 0x800) {
                        buf[n++] = (char)(0xc0 | (cp >> 6));
                        buf[n++] = (char)(0x80 | (cp & 0x3f));
                    } else {
                        buf[n++] = (char)(0xe0 | (cp >> 12));
                        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                        buf[n++] = (char)(0x80 | (cp & 0x3f));
                    }
                    break;
                }
                default: seterr(j, "bad escape"); return 0;
            }
            ++j->p;
        } else {
            buf[n++] = (char)ch;
            ++j->p;
        }
    }
    if (!j->s[j->p]) { seterr(j, "unterminated string"); return 0; }
    ++j->p;
    buf[n] = 0;
    return buf;
}

static Node *parse_value(JP *j);

static Node *parse_array(JP *j) {
    Node *n = new_node(J_ARR);
    ++j->p;
    n->kids = scoped_take(sizeof(Node *) * 64);
    ws(j);
    if (j->s[j->p] == ']') { ++j->p; return n; }
    for (;;) {
        Node *v = parse_value(j);
        if (!v) return 0;
        n->kids[n->n++] = v;
        ws(j);
        if (j->s[j->p] == ',') {
            ++j->p;
            ws(j);
            if (j->s[j->p] == ']') { seterr(j, "bad number"); return 0; }
            continue;
        }
        if (j->s[j->p] == ']') { ++j->p; break; }
        seterr(j, "unclosed container");
        return 0;
    }
    return n;
}

static Node *parse_object(JP *j) {
    Node *n = new_node(J_OBJ);
    ++j->p;
    n->keys = scoped_take(sizeof(char *) * 64);
    n->kids = scoped_take(sizeof(Node *) * 64);
    ws(j);
    if (j->s[j->p] == '}') { ++j->p; return n; }
    for (;;) {
        char *key;
        Node *v;
        ws(j);
        key = read_string(j);
        if (!key) return 0;
        ws(j);
        if (j->s[j->p] != ':') { seterr(j, "expected ':'"); return 0; }
        ++j->p;
        v = parse_value(j);
        if (!v) return 0;
        n->keys[n->n] = key;
        n->kids[n->n] = v;
        ++n->n;
        ws(j);
        if (j->s[j->p] == ',') { ++j->p; continue; }
        if (j->s[j->p] == '}') { ++j->p; break; }
        seterr(j, "unclosed container");
        return 0;
    }
    return n;
}

static Node *parse_value(JP *j) {
    char ch;
    ws(j);
    ch = j->s[j->p];
    if (ch == '"') {
        char *s = read_string(j);
        Node *n;
        if (!s) return 0;
        n = new_node(J_STR);
        n->text = s;
        return n;
    }
    if (ch == '{') return parse_object(j);
    if (ch == '[') return parse_array(j);
    if (ch == 't' && strncmp(j->s + j->p, "true", 4) == 0) {
        Node *n = new_node(J_BOOL);
        j->p += 4;
        n->boolean = 1;
        return n;
    }
    if (ch == 'f' && strncmp(j->s + j->p, "false", 5) == 0) {
        Node *n = new_node(J_BOOL);
        j->p += 5;
        n->boolean = 0;
        return n;
    }
    if (ch == 'n' && strncmp(j->s + j->p, "null", 4) == 0) {
        j->p += 4;
        return new_node(J_NULL);
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        size_t start = j->p, len;
        char *text;
        Node *n;
        if (ch == '-') ++j->p;
        if (j->s[j->p] < '0' || j->s[j->p] > '9') { seterr(j, "bad number"); return 0; }
        while (j->s[j->p] >= '0' && j->s[j->p] <= '9') ++j->p;
        if (j->s[j->p] == '.') {
            ++j->p;
            if (j->s[j->p] < '0' || j->s[j->p] > '9') { seterr(j, "bad number"); return 0; }
            while (j->s[j->p] >= '0' && j->s[j->p] <= '9') ++j->p;
        }
        if (j->s[j->p] == 'e' || j->s[j->p] == 'E') {
            ++j->p;
            if (j->s[j->p] == '+' || j->s[j->p] == '-') ++j->p;
            if (j->s[j->p] < '0' || j->s[j->p] > '9') { seterr(j, "bad number"); return 0; }
            while (j->s[j->p] >= '0' && j->s[j->p] <= '9') ++j->p;
        }
        len = j->p - start;
        text = scoped_take(len + 1);
        memcpy(text, j->s + start, len);
        text[len] = 0;
        n = new_node(J_NUM);
        n->text = text;
        n->num = strtod(text, 0);
        return n;
    }
    seterr(j, "unexpected token");
    return 0;
}

typedef struct { char *p; size_t n; } Buf;
static void bputc(Buf *b, char c) { b->p[b->n++] = c; b->p[b->n] = 0; }
static void bputs(Buf *b, const char *s) {
    size_t l = strlen(s);
    memcpy(b->p + b->n, s, l);
    b->n += l;
    b->p[b->n] = 0;
}
static void bindent(Buf *b, int level) {
    for (int i = 0; i < 2 * level; ++i) bputc(b, ' ');
}
static void escape_string(Buf *b, const char *s) {
    bputc(b, '"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"': bputs(b, "\\\""); break;
            case '\\': bputs(b, "\\\\"); break;
            case '\n': bputs(b, "\\n"); break;
            case '\r': bputs(b, "\\r"); break;
            case '\t': bputs(b, "\\t"); break;
            case '\b': bputs(b, "\\b"); break;
            case '\f': bputs(b, "\\f"); break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    bputs(b, tmp);
                } else bputc(b, (char)c);
        }
    }
    bputc(b, '"');
}
static void print_qstring(const char *s) {
    Buf b;
    b.p = scoped_take(strlen(s) * 6 + 4);
    b.n = 0;
    b.p[0] = 0;
    escape_string(&b, s);
    fputs(b.p, stdout);
}

static void serialize(Node *n, Buf *b, int level, int pretty) {
    int i;
    switch (n->type) {
        case J_NULL: bputs(b, "null"); break;
        case J_BOOL: bputs(b, n->boolean ? "true" : "false"); break;
        case J_NUM: bputs(b, n->text); break;
        case J_STR: escape_string(b, n->text); break;
        case J_ARR:
            bputc(b, '[');
            if (n->n && pretty) bputc(b, '\n');
            for (i = 0; i < n->n; ++i) {
                if (pretty) bindent(b, level + 1);
                serialize(n->kids[i], b, level + 1, pretty);
                if (i + 1 < n->n) bputc(b, ',');
                if (pretty) bputc(b, '\n');
            }
            if (n->n && pretty) bindent(b, level);
            bputc(b, ']');
            break;
        case J_OBJ:
            bputc(b, '{');
            if (n->n && pretty) bputc(b, '\n');
            for (i = 0; i < n->n; ++i) {
                if (pretty) bindent(b, level + 1);
                escape_string(b, n->keys[i]);
                bputs(b, pretty ? ": " : ":");
                serialize(n->kids[i], b, level + 1, pretty);
                if (i + 1 < n->n) bputc(b, ',');
                if (pretty) bputc(b, '\n');
            }
            if (n->n && pretty) bindent(b, level);
            bputc(b, '}');
            break;
    }
}

static Node *member(Node *o, const char *key) {
    if (!o || o->type != J_OBJ) return 0;
    for (int i = 0; i < o->n; ++i)
        if (strcmp(o->keys[i], key) == 0) return o->kids[i];
    return 0;
}

int main(void) {
    static const char doc[] =
        "{\"name\":\"" "go" "ose samples" "\",\"version\":4,\"stable\":false,\"ratio\":0.75,"
        "\"tags\":[\"fast\",\"safe\",\"flat\"],"
        "\"nested\":{\"depth\":2,\"empty\":{},\"list\":[1,2.5,-300,true,null,\"x\\ty\",[]],"
        "\"quote\":\"she said \\\"hi\\\"\\nand left\"},"
        "\"matrix\":[[1,0,0],[0,1,0],[0,0,1]],"
        "\"unicode\":\"caf\xC3\xA9 \xE2\x9C\x93\","
        "\"authors\":[{\"name\":\"ada\",\"born\":1815},{\"name\":\"grace\",\"born\":1906},"
        "{\"name\":\"linus\",\"born\":1969}]}";
    static const char *bad[] = {
        "[1, 2", "{\"a\" 1}", "[tru]", "\"abc", "{\"k\": [1, 2,]}", "1 2"
    };
    char input[512];
    size_t doclen = strlen(doc);
    JP j;
    Node *root;
    Buf pretty, compact;
    Node *authors;

    (void)fails; (void)report; (void)cmp_int; /* shared common.h helpers */

    memcpy(input, doc, doclen);
    memset(input + doclen, ' ', 470 - doclen);
    input[470] = 0;

    j.s = input;
    j.p = 0;
    j.msg = 0;
    j.ep = 0;
    node_count = 0;
    root = parse_value(&j);
    if (root) {
        ws(&j);
        if (j.s[j.p]) seterr(&j, "trailing input");
    }
    printf("parsed %lu bytes into %d nodes\n", (unsigned long)strlen(input), node_count);

    pretty.p = scoped_take(1 << 14);
    pretty.n = 0;
    pretty.p[0] = 0;
    serialize(root, &pretty, 0, 1);
    puts(pretty.p);

    compact.p = scoped_take(1 << 14);
    compact.n = 0;
    compact.p[0] = 0;
    serialize(root, &compact, 0, 0);
    printf("compact: %lu bytes\n", (unsigned long)compact.n);
    puts(compact.p);

    printf("name = ");
    print_qstring(member(root, "name")->text);
    printf("\n");
    printf("nested.depth = %.1f\n", member(member(root, "nested"), "depth")->num);
    printf("nested.list[2] = %.1f\n", member(member(root, "nested"), "list")->kids[2]->num);
    authors = member(root, "authors");
    for (int i = 0; i < authors->n; ++i) {
        printf("author %d: ", i);
        print_qstring(member(authors->kids[i], "name")->text);
        printf(" born %.1f\n", member(authors->kids[i], "born")->num);
    }

    for (unsigned k = 0; k < sizeof(bad) / sizeof(*bad); ++k) {
        JP t;
        t.s = bad[k];
        t.p = 0;
        t.msg = 0;
        t.ep = 0;
        node_count = 0;
        if (parse_value(&t)) {
            ws(&t);
            if (t.s[t.p]) seterr(&t, "trailing input");
        }
        printf("'%s' -> %s at byte %lu\n", bad[k], t.msg, (unsigned long)t.ep);
    }
    return 0;
}
