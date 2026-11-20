#include "ext_cp_helper.hp"

int main(void) {
    auto box = Box(box_constant());
    print("box", box.get());
    return 0;
}
