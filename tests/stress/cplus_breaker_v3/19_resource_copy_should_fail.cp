resource struct Handle {int id;};
void Handle_drop(Handle *self){ self->id=0; }
int main(void){Handle a={1};Handle b=a;return b.id;}
