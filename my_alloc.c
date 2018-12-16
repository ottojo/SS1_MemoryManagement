/*
 * The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL
 * NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED",  "MAY", and
 * "OPTIONAL" in this document are to be interpreted as described in
 * RFC 2119.
 */

#include <stdlib.h>
#include "my_alloc.h"
#include "my_system.h"

#define NR_STACKS 32

//#define DEBUG_INIT
//#define DEBUG_ALLOC
//#define DEBUG_GETBLOCK
//#define DEBUG_FREE

// === The Idea ===
// Each block contains only objects of equal size.
// Each block is part of one stack, which only contains blocks with the same object size.
// An object MUST be stored within a space, which contains a spaceHeader and the object.
// The stack contains a linked list of free spaces.

// sizeof(freelist) MUST be < 8, so it fits in any object
typedef struct freelist {
    struct freelist *nextFreeObject;
} freelist;

typedef void block;

typedef struct stack {
    block *currentblock;
    freelist *freeObjectList;
} stack;

stack stackarray[NR_STACKS];

// Any object MUST be preceded by a spaceHeader
typedef struct spaceHeader {
    stack *parentStack;
} spaceHeader;

spaceHeader *headerOf(void *objectLocation) {
    return objectLocation - sizeof(spaceHeader);
}

void init_my_alloc() {
}


void *my_alloc(size_t size) {
#ifdef DEBUG_ALLOC
    printf("Allocating space for object of size %ld.\n", size);
#endif

    int i = (int) ((size >> 3) - 1);
    if (stackarray[i].currentblock == 0) {
#ifdef DEBUG_INIT
        printf("Initializing stack %d for objects sized %d.\n", i, (i + 1) * 8);
#endif
        stackarray[i].currentblock = get_block_from_system();
#ifdef DEBUG_GETBLOCK
        printf("\tAllocated new block at %p\n", stackarray[i].currentblock);
#endif
        // Next free space is start of block, next free object will be preceded by spaceHeader
        stackarray[i].freeObjectList = stackarray[i].currentblock + sizeof(spaceHeader);
#ifdef DEBUG_INIT
        printf("\t First object will be stored at %p.\n", stackarray[i].freeObjectList);
#endif
        // Next free space is last known space
        stackarray[i].freeObjectList->nextFreeObject = 0;
    }

    // index = size/8 - 1
    stack *currentStack = &stackarray[(size >> 3) - 1];

#ifdef DEBUG_ALLOC
    printf("\tUsing stack nr %ld at %p.\n", (size >> 3) - 1, currentStack);
#endif

    void *newObjectLocation = currentStack->freeObjectList;
#ifdef DEBUG_ALLOC
    printf("\tLocation for new object is %p.\n", newObjectLocation);
#endif

    void *newSpaceLocation = newObjectLocation - sizeof(spaceHeader);

#ifdef DEBUG_ALLOC
    printf("\tLocation for new space is %p on block %p.\n", newSpaceLocation, currentStack->currentblock);
#endif

    // Set header
    spaceHeader *h = headerOf(newObjectLocation);
    h->parentStack = currentStack;


    if (currentStack->freeObjectList->nextFreeObject == 0) {

#ifdef DEBUG_ALLOC
        printf("\tNext free space is undefined, so it will be behind the current space.\n");
#endif

        // Space to use is last known space.
        // Things to do now:
        // If the current block can fit another space:
        //  Set the first free space in stack to the next space
        // Else:
        //  Allocate a new block
        //  Set the first free space in stack to the start of the new block

        // &lastByteOfPotentialNewSpace + 1 <= &lastByteInBlock + 1
        if (newSpaceLocation + 2 * (sizeof(spaceHeader) + size) <= currentStack->currentblock + BLOCKSIZE) {
            currentStack->freeObjectList = newObjectLocation + size + sizeof(spaceHeader);
#ifdef DEBUG_ALLOC
            printf("\tNext space will fit on block.\n");
#endif
        } else {
#ifdef DEBUG_ALLOC
            printf("\tNext space will need new block.\n");
#endif
            // Next free space is on new block
            void *newBlock = get_block_from_system();
#ifdef DEBUG_GETBLOCK
            printf("\tAllocated new block at %p\n", newBlock);
#endif
            currentStack->freeObjectList = newBlock + sizeof(spaceHeader);
            currentStack->currentblock = newBlock;
        }
        currentStack->freeObjectList->nextFreeObject = 0;
    } else {

        // The current free space contains a pointer to the next free space
#ifdef DEBUG_ALLOC
        printf("\tThe space we are assigning right now contains a pointer to the next free object: %p\n",
               currentStack->freeObjectList->nextFreeObject);
#endif
        currentStack->freeObjectList = currentStack->freeObjectList->nextFreeObject;
    }
#ifdef DEBUG_ALLOC
    printf("\tDone allocating %p.\n", newObjectLocation);
#endif
    return newObjectLocation;
}

void my_free(void *ptr) {
    spaceHeader *header = headerOf(ptr);
#ifdef DEBUG_FREE
    printf("Free on stack %p\n", header->parentStack);
#endif
    // Append new free space to start of free space list
    ((freelist *) ptr)->nextFreeObject = header->parentStack->freeObjectList;
    header->parentStack->freeObjectList = ptr;

#ifdef DEBUG_FREE
    printf("\tFree successful for %p\n", ptr);
#endif
}
