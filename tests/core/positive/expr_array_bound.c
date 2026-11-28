#define BUFSIZE (1 << 20)
static int buf[BUFSIZE];
int main(void) {
  buf[100] = 7;
  return buf[100] == 7 ? 0 : 1;
}
