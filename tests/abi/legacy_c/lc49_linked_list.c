/*
 * lc49 - pointer patterns: singly linked list built with malloc and torn down
 * with free. Independent legacy-C ABI probe for pointer-heavy C structures.
 */
#include <stdio.h>
#include <stdlib.h>

struct node {
    int value;
    struct node *next;
};

static int push(struct node **head, int value)
{
    struct node *n = (struct node *)malloc(sizeof(*n));
    if (n == NULL) {
        return 0;
    }
    n->value = value;
    n->next = *head;
    *head = n;
    return 1;
}

static int sum_list(const struct node *head)
{
    int total = 0;
    while (head != NULL) {
        total += head->value;
        head = head->next;
    }
    return total;
}

static int count_list(const struct node *head)
{
    int count = 0;
    while (head != NULL) {
        ++count;
        head = head->next;
    }
    return count;
}

static void free_list(struct node *head)
{
    while (head != NULL) {
        struct node *next = head->next;
        free(head);
        head = next;
    }
}

int main(void)
{
    struct node *head = NULL;
    int i, ok = 1;

    for (i = 1; i <= 8; ++i) {
        if (!push(&head, i)) {
            ok = 0;
            break;
        }
    }
    if (count_list(head) != 8 || sum_list(head) != 36 ||
        head == NULL || head->value != 8) {
        ok = 0;
    }
    free_list(head);
    if (!ok) {
        printf("lc49 result: FAIL list check\n");
        return 1;
    }
    printf("lc49 result: OK nodes=8 sum=36 head=8\n");
    return 0;
}
