#include "cplus/cplus.h"

class Ledger {
    int balance;

    Ledger(int opening) {
        this->balance = opening;
    }

    void credit(int amount) {
        this->balance += amount;
    }

    void debit(int amount) {
        this->balance -= amount;
    }

    int total(void) {
        return this->balance;
    }

    int double_total(void) {
        return this->total() * 2;
    }

    void drop(void) {
        print("DROP", this->balance);
    }
};

int main(void) {
    Ledger account = Ledger(100);
    account.credit(25);
    account.debit(7);
    print("LEDGER", account.total(), account.double_total());
    return 0;
}
