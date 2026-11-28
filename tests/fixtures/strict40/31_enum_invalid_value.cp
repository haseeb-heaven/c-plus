enum State { STATE_OFF = 0, STATE_ON = 1 };
int main(void) { enum State state = (enum State)99; return state == STATE_ON; }
