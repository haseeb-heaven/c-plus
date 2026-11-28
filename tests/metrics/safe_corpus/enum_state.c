enum state { IDLE, RUNNING, DONE };
int main(void){ enum state s=IDLE; for(int i=0;i<3;i++){ if(s==IDLE) s=RUNNING; else if(s==RUNNING) s=DONE; }
  return s==DONE ? 0 : 1; }
