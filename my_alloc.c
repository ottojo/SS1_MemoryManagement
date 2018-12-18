#include <stdlib.h>
#include "my_alloc.h"
#include "my_system.h"
#include "stdio.h"

#define HEADER_SIZE (8+126+2)
#define BLOCK_SIZE 8192
#define DATA_SIZE (BLOCK_SIZE-HEADER_SIZE)
#define MIN_OBJECT_SIZE 8
#define MAX_OBJECT_SIZE 256

//#define DEBUG
//#define DEBUG_INIT
//#define DEBUG_CONTENT
typedef struct Block {
    struct Block *next;
} Block;


/*List for free Object spaces in block. Does not care about the amount of Blocks*/
/*TODO: 
 * High potential for optimizing here
 * Ideas:
 * -seperated FreeList for each size
 * -Sorting
 */
typedef struct FreeObj {
    struct FreeObj *next;
} FreeObj, Object;

void init_my_alloc();

void *my_alloc(size_t size);

void my_free(void *ptr);

int updateContents(void *ptr);

Block *getBlock(void *ptr);

int getObjSize(void *ptr);

int initBlock(Block *block);

void printContents(Block *block);

FreeObj *rootFree[32];
Block *rootBlock;

void init_my_alloc() {
#ifdef DEBUG_INIT
    printf("##########################################################################################################\n");
    printf("Initializing...\n");
#endif
    rootBlock = get_block_from_system();
    if (rootBlock == NULL) {
        fprintf(stderr, "System MEM failure!\n");
        exit(0);
    }
    initBlock(rootBlock);
    //assuming, that new Block has more space that MAX_OBJECT_SIZE
    rootFree[(MAX_OBJECT_SIZE / 8 - 1)] = (FreeObj *) ((void *) rootBlock + HEADER_SIZE);
    rootFree[(MAX_OBJECT_SIZE / 8 - 1)]->next = NULL;
    updateContents(rootFree[(MAX_OBJECT_SIZE / 8) - 1]);
#ifdef DEBUG_INIT
    printf("Initialization finished!\n");
    printf("rootFree: %p", rootFree[(MAX_OBJECT_SIZE/8)-1]);
#endif
}

void *my_alloc(size_t size) {
#ifdef DEBUG
    printf("\n\nallocating mem of size: %ld\n",size);
#endif
    FreeObj *object;

    int freeIterator = (size / 8) - 1;

    //moving freeIterator to next free Object with size of at least requested size
    int MAX_freeIterator = (MAX_OBJECT_SIZE / 8) - 1;
    while (rootFree[freeIterator] == NULL && freeIterator < MAX_freeIterator) freeIterator++;

    /*case: no matching Object
     * -->allocating new Block*/
    if (rootFree[freeIterator] == NULL) {
#ifdef DEBUG
        printf("no nextFree matching. Allocating new Block!\n");
#endif
        Block *newBlock = get_block_from_system();
        if (newBlock == NULL) {
            fprintf(stderr, "System MEM failure!\n");
            exit(0);
        }
        initBlock(newBlock);

        //inserting newBlock as rootBlock of List
        newBlock->next = rootBlock;
        rootBlock = newBlock;

        //assume that new block will definetly be large enough for Object
        object = (FreeObj *) ((void *) rootBlock + HEADER_SIZE);
        FreeObj *newFree = (FreeObj *) ((void *) object + size);
        rootFree[(MAX_OBJECT_SIZE / 8) - 1] = newFree;
        updateContents(object);
        updateContents(newFree);

#ifdef DEBUG
        printf("look at first entry:");
        printContents(newBlock);
        printf("allocation finished!\n");
#endif
        return object;

    }

    /*case: matching freeObject has unknown size larger than requested size     
     * ->freeObject is divided into two parts. 
     *  --first part will be returned
     *  --second part goes back to the list. Place depends on the remaining size.*/
    if (getObjSize(rootFree[freeIterator]) > size) {
#ifdef DEBUG
        printf("found free Object with unknown size larger than requested size!\n");
#endif
        int freeObjSize = getObjSize(rootFree[freeIterator]);
        object = rootFree[freeIterator];
        //creating newFree Object from unused mem of matching FreeObject
        FreeObj *newFree = (FreeObj *) ((void *) rootFree[freeIterator] + size);
        rootFree[freeIterator] = rootFree[freeIterator]->next; //can be NULL
        int newFreeSize = (freeObjSize - size >= MAX_OBJECT_SIZE) ? (MAX_OBJECT_SIZE) : (freeObjSize - size);
        newFree->next = rootFree[(newFreeSize / 8) - 1]; //can be NULL
        rootFree[(newFreeSize / 8) - 1] = newFree;
        updateContents(newFree);
        return object;
    }

    /*case: matching object found:*/
    //case matching object has same size as requested size
#ifdef DEBUG
    printf("found free Object with size perfectly matching with requested size!\n");
#endif
    if (getObjSize(rootFree[freeIterator]) == size) {
        object = rootFree[freeIterator];
        rootFree[freeIterator] = rootFree[freeIterator]->next;
        return object;
    }
    fprintf(stdout, "FATAL ERROR: RETURNING NULL POINTER TO USER!!!\n");
    exit(0);
    return NULL;

}

void my_free(void *ptr) {

#ifdef DEBUG
    printf("\nfree pointer: %p\n",ptr);
#endif
    FreeObj *newFree = (FreeObj *) ptr;
    int newFreeSize = getObjSize(ptr);
    newFree->next = rootFree[(newFreeSize / 8) - 1];
    rootFree[(newFreeSize / 8) - 1] = newFree;
    //printf("freeIterator: %d\n",(newFreeSize/8)-1);

}

int updateContents(void *ptr) {
#ifdef DEBUG
    printf("\nupdating contents with pointer: %p\n",ptr);

#endif
    Block *block = getBlock(ptr);
    if (block == NULL) {
        fprintf(stderr, "Updating contents failed!\n");
        fprintf(stderr, "pointer: %p\n", ptr);
        fprintf(stderr, "blockp: %p\n", block);
        exit(0);
    }
    //position is the bit position
    unsigned int position = (int) (ptr - (void *) block - HEADER_SIZE) / 8;
    /*pretty sure that indices are correct*/
    void *contents = (void *) block + 8;

    ((char *) contents)[position / 8] |= (1) << (7 - (position % 8));
#ifdef DEBUG
    printf("Updating finished!\n");
#endif
    return 0;
}


/*finds corresponding block to given Object. Returns NULL in case of no result*/
Block *getBlock(void *ptr) {
#ifdef DEBUG
    printf("trying to find corresponding block to ptr: %p\n",ptr);
#endif
    Block *block = (Block *) rootBlock;
    while ((void *) ptr < (void *) block || (void *) ptr > (void *) ((void *) block + BLOCK_SIZE)) {
        if (block->next == NULL) {
            return NULL;
        }
        block = block->next;
    }
#ifdef DEBUG
    printf("found block: %p\n",block);
#endif
    return block;
}


/*returns size to next pointer according to contents in header
* size is in Bytes*/
int getObjSize(void *ptr) {
#ifdef DEBUG
    printf("Getting object size...\n");
#endif
    Block *block = getBlock(ptr);
#ifdef DEBUG
    printf("Contents of block:\n");
    printContents(block);
#endif
    unsigned int position = (ptr - (void *) block - HEADER_SIZE) / 8;
    int pos0 = position;
    //going bitwise through contents, looking for next object marker
    void *contents = (void *) block + 8;
    do {
        position++;
    } while (!(((char *) contents)[position / 8] & (1) << (7 - (position % 8))) && position < DATA_SIZE / 8);
#ifdef DEBUG
    printf("found object size: %d\n",(position-pos0)*8);
#endif
    return (position - pos0) * 8;

}

int initBlock(Block *block) {
    char *cp = (char *) block;
    //filling header with zeros
    for (; cp < (char *) (DATA_SIZE / 8); cp++) *cp = 0;
#ifdef DEBUG
    printf("NEW BLOCK//////////////////////////////////////////////////////////////////////////////////////////////\n");
    printf("new block allocated %p and filled with zeros.\n",block);
#endif
    return 0;
}

void printContents(Block *block) {
    void *content = (void *) block + 8;
    int position = 0;
    for (; position < 1007; position++) {
#ifdef DEBUG_CONTENT
        if(position % 8==0){
            printf("\n %d contentptr: %p to: %p  ", position, &(((char*) content)[position/8]),(void*) ((void*) block + HEADER_SIZE + 8*position));
        }
#endif
        (((char *) content)[position / 8] & (1) << (7 - (position % 8))) ? printf("%d", 1) : printf("%d", 0);

    }
#ifdef DEBUG_CONTENT
    printf("\n");
#endif
}


