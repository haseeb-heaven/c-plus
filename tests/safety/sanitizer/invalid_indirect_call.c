typedef void (*Callback)(void);
static int wrong_signature(int value) { return value; }
int main(void) {
  volatile Callback cb = (Callback)&wrong_signature;
  cb();
  return 0;
}
