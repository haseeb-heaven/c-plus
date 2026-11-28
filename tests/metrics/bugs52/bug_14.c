#include <stdlib.h>
struct Connection { int* rx_buf; int* tx_buf; };
int conn_init(struct Connection* c) { c->rx_buf = (int*)malloc(1024); if (!c->rx_buf) return -1; c->tx_buf = (int*)malloc((size_t)-1); if (!c->tx_buf) { return -1; } return 0; }
int main(void) { struct Connection c = {0}; conn_init(&c); return 0; }
