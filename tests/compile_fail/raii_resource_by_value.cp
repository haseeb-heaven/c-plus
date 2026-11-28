/* Issue #48: a resource type passed by value would leak in the callee (or
 * double-free if a destructor were emitted there), so the signature must be
 * rejected rather than silently accepted. */
class Resource {
    int id;
    void drop(void) { this->id = 0; }
};

static int consume(Resource r) {
    return r.id;
}

int main(void) {
    Resource r;
    return consume(r);
}
