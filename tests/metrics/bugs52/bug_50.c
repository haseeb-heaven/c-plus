#include <stdlib.h>
struct Message { int length; char payload[]; };
int main(void) { struct Message* msg = (struct Message*)malloc(sizeof(struct Message)); msg->payload[0] = 'X'; free(msg); return 0; }
