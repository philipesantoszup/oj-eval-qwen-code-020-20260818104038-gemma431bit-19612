#include "buddy.h"
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096
#define MAX_RANK 16

typedef struct free_block {
    struct free_block *next;
    struct free_block *prev;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *pool_start = NULL;
static int pool_pgcount = 0;
static unsigned char *page_status = NULL; // 0: free, 1: allocated
static unsigned char *page_rank = NULL;   // rank of the block starting at this page

static int get_page_index(void *p) {
    if (!pool_start || !p || (unsigned long)p < (unsigned long)pool_start) return -1;
    unsigned long offset = (unsigned long)p - (unsigned long)pool_start;
    if (offset % PAGE_SIZE != 0) return -1;
    int idx = offset / PAGE_SIZE;
    if (idx >= pool_pgcount) return -1;
    return idx;
}

static void add_free_block(int rank, void *p) {
    free_block_t *block = (free_block_t *)p;
    block->next = free_lists[rank];
    block->prev = NULL;
    if (free_lists[rank]) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
    
    int idx = get_page_index(p);
    if (idx != -1) {
        page_status[idx] = 0;
        page_rank[idx] = (unsigned char)rank;
    }
}

static void remove_free_block(int rank, void *p) {
    free_block_t *block = (free_block_t *)p;
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_lists[rank] = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
}

int init_page(void *p, int pgcount) {
    if (!p || pgcount <= 0) return -EINVAL;
    
    pool_start = p;
    pool_pgcount = pgcount;
    
    if (page_status) free(page_status);
    if (page_rank) free(page_rank);
    
    page_status = (unsigned char *)calloc(pgcount, 1);
    page_rank = (unsigned char *)calloc(pgcount, 1);
    if (!page_status || !page_rank) return -EINVAL;

    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    int current_pg = 0;
    while (current_pg < pgcount) {
        int remaining = pgcount - current_pg;
        int best_rank = 1;
        for (int r = MAX_RANK; r >= 1; r--) {
            if ((1 << (r - 1)) <= remaining) {
                best_rank = r;
                break;
            }
        }
        
        void *block_p = (char *)p + (unsigned long)current_pg * PAGE_SIZE;
        add_free_block(best_rank, block_p);
        current_pg += (1 << (best_rank - 1));
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) return ERR_PTR(-EINVAL);
    
    int r = rank;
    while (r <= MAX_RANK && free_lists[r] == NULL) {
        r++;
    }
    
    if (r > MAX_RANK) return ERR_PTR(-ENOSPC);
    
    void *p = free_lists[r];
    remove_free_block(r, p);
    
    while (r > rank) {
        r--;
        void *buddy = (char *)p + (unsigned long)(1 << (r - 1)) * PAGE_SIZE;
        add_free_block(r, buddy);
    }
    
    int idx = get_page_index(p);
    if (idx != -1) {
        page_status[idx] = 1;
        page_rank[idx] = (unsigned char)rank;
    }
    
    return p;
}

int return_pages(void *p) {
    int idx = get_page_index(p);
    if (idx == -1 || page_status[idx] == 0) return -EINVAL;
    
    int rank = page_rank[idx];
    
    while (rank < MAX_RANK) {
        int block_size = (1 << (rank - 1));
        int buddy_idx = idx ^ block_size;
        
        if (buddy_idx < 0 || buddy_idx >= pool_pgcount) break;
        if (page_status[buddy_idx] != 0 || page_rank[buddy_idx] != rank) break;
        
        void *buddy_p = (char *)pool_start + (unsigned long)buddy_idx * PAGE_SIZE;
        remove_free_block(rank, buddy_p);
        
        if (buddy_idx < idx) {
            idx = buddy_idx;
            p = buddy_p;
        }
        rank++;
    }
    
    add_free_block(rank, p);
    return OK;
}

int query_ranks(void *p) {
    int idx = get_page_index(p);
    if (idx == -1) return -EINVAL;
    
    for (int r = MAX_RANK; r >= 1; r--) {
        int block_size = (1 << (r - 1));
        int start_idx = (idx / block_size) * block_size;
        if (start_idx < 0 || start_idx >= pool_pgcount) continue;
        
        if (page_rank[start_idx] == r) {
            // We must also ensure that the block actually contains the page.
            // Since start_idx = (idx / block_size) * block_size, it always does.
            // But we should check if it's actually a block of rank r.
            // If it's allocated, it must be rank r.
            // If it's free, it must be rank r.
            // Wait, if page_rank[start_idx] == r, then it's a block of rank r.
            return r;
        }
    }
    
    return -EINVAL;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    
    int count = 0;
    free_block_t *curr = free_lists[rank];
    while (curr) {
        count++;
        curr = curr->next;
    }
    return count;
}
