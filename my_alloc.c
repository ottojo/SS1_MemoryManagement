#include <stdlib.h>
#include <stdio.h>
#include "my_alloc.h"
#include "my_system.h"

#define NR_STACKS 32

//#define DEBUG_INIT
//#define DEBUG_ALLOC
//#define DEBUG_GETBLOCK
//#define DEBUG_FREE

typedef struct freelist {
    struct freelist *nextFree;
} freelist;

typedef struct stack {
    void *firstblock;
    void *currentblock;
    freelist *freesection;
} stack;


// stack n contains only elements of size (n+1)*8
stack stackarray[NR_STACKS];


// last 8 bytes in block are pointer to next block.

void init_my_alloc() {
#ifdef DEBUG_INIT
    printf("Initializing.\n");
#endif
    for (int i = 0; i < NR_STACKS; i++) {
#ifdef DEBUG_INIT
        printf("Initializing stack %d.\n", i);
#endif
        stackarray[i].firstblock = get_block_from_system();
#ifdef DEBUG_GETBLOCK
        printf("\tAllocated new block at %p\n", stackarray[i].firstblock);
#endif
        // Initial block has no blocks following it
        *(void **) (stackarray[i].firstblock + BLOCKSIZE - 8) = 0;
        stackarray[i].currentblock = stackarray[i].firstblock;
        stackarray[i].freesection = (freelist *) stackarray[i].firstblock;
        stackarray[i].freesection->nextFree = 0;
    }
#ifdef DEBUG_INIT
    printf("Done.\n");
#endif
}

void *my_alloc(size_t size) {
#ifdef DEBUG_ALLOC
    printf("Allocating space for object of size %ld.\n", size);
#endif

    stack *currentStack = &stackarray[(size >> 3) - 1];
    freelist *f = currentStack->freesection;

#ifdef DEBUG_ALLOC
    printf("\tUsing stack nr %ld at %p, next free space is at %p.\n", (size >> 3) - 1, currentStack, f);
#endif

    if (f->nextFree == 0) {
        // Nach freiem Block nichts belegt.

#ifdef DEBUG_ALLOC
        printf("\tNext free section is at the end.\n");
        printf("\tAdresse des nextBlockP ist %p\n", currentStack->currentblock + BLOCKSIZE - 8);
        printf("\tAdresse direkt nach neuem Objekt ist %p\n", ((void *) f) + size);
#endif

        // (adresse nach neuem objekt) >= blockende
        if (((void *) f) + 2 * size > currentStack->currentblock + BLOCKSIZE - 8) {
            // Next free space is on new block
            void *newBlock = get_block_from_system();
#ifdef DEBUG_GETBLOCK
            printf("\tAllocated new block at %p\n", newBlock);
#endif

            // Set last 8 bytes of current block to pointer to next block
            *(void **) (currentStack->currentblock + BLOCKSIZE - 8) = newBlock;

            // Next free space is start of new block
            currentStack->currentblock = newBlock;
            currentStack->freesection = newBlock;
        } else {
            // Next free space is directly behind the just requested object and fits on current block
            currentStack->freesection = ((void *) currentStack->freesection) + size;
        }
    } else {
        // We are not at the end of used space, so we have a pointer to the next free space
        currentStack->freesection = f->nextFree;
    }

    return f;
}

void my_free(void *ptr) {
    void *block;
    int index = -1;
#ifdef DEBUG_FREE
    printf("Searching for pointer %p\n", ptr);
#endif
    bool found = false;
    // Search for stack containing pointer
    for (int i = 0; i < NR_STACKS; i++) {
#ifdef DEBUG_FREE
        printf("\tSearching stack %d\n", i);
#endif
        block = stackarray[i].firstblock;
        int bc = 0;
        while (true) {
#ifdef DEBUG_FREE
            printf("\t\tSearching block at %p\n", block);
#endif
            ++bc;
            // Test of pointer is in range of block
            if (ptr >= block && ptr < (block + BLOCKSIZE - 8)) {
                found = true;
                break;
            }

            // Not found in this block
            void *nextBlockAddress = *(void **) (block + BLOCKSIZE - 8);
            if (nextBlockAddress == 0) {
                // Last block in stack, search next stack.
                // printf("searched %d blocks.\n", bc);
                break;
            } else {
                // More blocks in stack, go to next block.
                block = nextBlockAddress;
            }
        }
        if (found) {
            // Found pointer in stack i.
            index = i;
#ifdef DEBUG_FREE
            printf("\t\tYay! Found pointer on stack %d\n", index);
#endif
            break;
        }
    }

    if (!found) {
        printf("Error, did not find pointer %p that should be freed.\n", ptr);
        return;
    }

    if (index == -1) {
        printf("something went horribly wrong here.\n");
        return;
    }


    // Store next free at pointer
    *(freelist **) ptr = stackarray[index].freesection;

    // Next free element in stack is now the object to be deleted
    stackarray[index].freesection = ptr;

#ifdef DEBUG_FREE
    printf("\tFree successful for %p\n", ptr);
#endif

}
