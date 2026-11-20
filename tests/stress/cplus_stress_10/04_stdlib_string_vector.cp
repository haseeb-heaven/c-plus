#include "stdlib/string.hp"
#include "stdlib/vector.hp"

int main(void) {
    String title = String("C+ testing");
    IntVec values = IntVec();

    values.push(2);
    values.push(3);
    values.push(5);
    values.push(7);

    print("STDLIB", title.len(), values.len(), values.at(3));
    return 0;
}
