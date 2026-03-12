#include <stdio.h>
#include <stdlib.h>
#include <ck_stack.h>

#define MAX_FDS 1024

typedef struct item {
    int fd;
    ck_stack_entry_t next;
} item_t;

static ck_stack_t stack = CK_STACK_INITIALIZER;
static item_t items[MAX_FDS];

CK_STACK_CONTAINER(item_t, next, item_container)

void stack_open() {
    ck_stack_init(&stack);
    
    // Pre-populate with all FDs in reverse order
    // (so popping gives lowest first)
    for (int i = MAX_FDS - 1; i >= 0; i--) {
        items[i].fd = i;
        ck_stack_push_mpmc(&stack, &items[i].next);
    }
}

int stack_pop(void *data, size_t size) {
    ck_stack_entry_t *entry = ck_stack_pop_mpmc(&stack);
    if (!entry) {
        return -1;  // No free FDs
    }
    
    item_t *item = item_container(entry);
    return item->fd;
}

void stack_push(int fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return;
    }
    // Return to free list
    ck_stack_push_mpmc(&stack, &items[fd].next);
}

int main() {
    stack_open();

    int fd1 = stack_pop(NULL, 0);
    int fd2 = stack_pop(NULL, 0);
    int fd3 = stack_pop(NULL, 0);

    printf("Opened FDs: %d, %d, %d\n", fd1, fd2, fd3);

    stack_push(fd2);
    printf("Closed FD: %d\n", fd2);

    int fd4 = stack_pop(NULL, 0);
    printf("Opened FD (should be %d): %d\n", fd2, fd4);

    if (fd4 == fd2) {
        printf("Success: FD reused correctly.\n");
    } else {
        printf("Failure: FD not reused correctly.\n");
        return 1;
    }

    return 0;
}
