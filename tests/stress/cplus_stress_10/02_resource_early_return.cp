#include "cplus/cplus.h"

resource struct Token {
    int id;
};

void Token_drop(Token *self) {
    if (self->id != 0) {
        print("CLOSE", self->id);
        self->id = 0;
    }
}

Token make_token(int id) {
    Token token = {id};
    return token;
}

int nested(int early) {
    Token outer = make_token(1);
    {
        Token inner = make_token(2);
        if (early)
            return 42;
    }
    return 9;
}

int main(void) {
    int answer = nested(1);
    print("RETURN", answer);
    return 0;
}
