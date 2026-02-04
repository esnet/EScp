#include <stdio.h>
#include <stdlib.h>
#include <ck_stack.h>

#define MAX_FDS 1024

typedef struct fd_node {
    int fd;
    ck_stack_entry_t next;
} fd_node_t;

static ck_stack_t free_fds;
CK_STACK_CONTAINER(fd_node_t, next, fd_node_container)

void fd_init() {
    ck_stack_init(&free_fds);
    
    // Pre-populate with all FDs in reverse order
    // (so popping gives lowest first)
    for (int i = MAX_FDS - 1; i >= 0; i--) {
        fd_node_t *node = malloc(sizeof(fd_node_t));
        node->fd = i;
        ck_stack_push_upmc(&free_fds, &node->next);
    }
}

int fd_open(void *data, size_t size) {
    ck_stack_entry_t *entry = ck_stack_pop_upmc(&free_fds);
    if (!entry) {
        return -1;  // No free FDs
    }
    
    fd_node_t *node = fd_node_container(entry);
    int fd = node->fd;
    free(node);
    
    return fd;
}

void fd_close(int fd) {
    // Return to free list
    fd_node_t *node = malloc(sizeof(fd_node_t));
    node->fd = fd;
    ck_stack_push_upmc(&free_fds, &node->next);
}

int main() {
    fd_init();

    int fd1 = fd_open(NULL, 0);
    int fd2 = fd_open(NULL, 0);
    int fd3 = fd_open(NULL, 0);

    printf("Opened FDs: %d, %d, %d\n", fd1, fd2, fd3);

    fd_close(fd2);
    printf("Closed FD: %d\n", fd2);

    int fd4 = fd_open(NULL, 0);
    printf("Opened FD (should be %d): %d\n", fd2, fd4);

    if (fd4 == fd2) {
        printf("Success: FD reused correctly.\n");
    } else {
        printf("Failure: FD not reused correctly.\n");
        return 1;
    }

    return 0;
}
