#include <ck_stack.h>

typedef struct fd_node {
    int fd;
    ck_stack_entry_t next;
} fd_node_t;

static ck_stack_t free_fds;

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
    
    fd_node_t *node = ck_stack_container(entry, fd_node_t, next);
    int fd = node->fd;
    free(node);
    
    // Set up fd_table[fd]...
    ck_bitmap_set(&fd_bitmap, fd);
    
    return fd;
}

void fd_close(int fd) {
    // Clean up fd_table[fd]...
    ck_bitmap_reset(&fd_bitmap, fd);
    
    // Return to free list
    fd_node_t *node = malloc(sizeof(fd_node_t));
    node->fd = fd;
    ck_stack_push_upmc(&free_fds, &node->next);
}
