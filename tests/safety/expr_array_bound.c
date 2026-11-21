#define BUFSIZE (1 << 20)
static int buf[BUFSIZE];
int main(void) { return buf[BUFSIZE]; }  // NOSONAR intentional out-of-bounds fixture
