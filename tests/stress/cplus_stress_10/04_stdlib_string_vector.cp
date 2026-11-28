#include "stdlib/string.hp"
#include "stdlib/vector.hp"

int main(void) {
    string title = string("C+ testing");
    vector values = vector();

    values.push_back(2);
    values.push_back(3);
    values.push_back(5);
    values.push_back(7);

    print("STDLIB", title.size(), values.size(), values.at(3));
    return 0;
}
