#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 32

typedef void page;

// If size of free object is 8, only a single pointer is stored (single linked list)
typedef struct {
    void *firstPointer;
    void *secondPointer;
} doublePointer;


// Bucket contains first element of linked list of free spaces of size 8 * (n + 1)
// Last bucket may contain larger free spaces.
doublePointer *buckets[NUMBER_OF_BUCKETS];

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PAGE_INIT
#define DEBUG_ALLOC
#define DEBUG_GETBLOCK
#define DEBUG_FREE
#define DEBUG_REMOVE_LIST
#define DEBUG_DOUBLEPOINTER
#define DEBUG_USED
#define DEBUG_INSERT
#endif

#ifdef DEBUG_USED
long int sumUsed = 0;
long int sumAvailable = 0;
#endif

// Each 8 byte header stores the size of the object before and after it.

// Header of 0: end of page
#define END_OF_PAGE 0
// Footer of 0: start of page
#define START_OF_PAGE 0
// LSB == 1: space not occupied
typedef struct {
    uint32_t tailingObjectSize;  // Footer of preceding object
    uint32_t precedingObjectSize;  // Header of following object
} header;

header *headerOf(void *object) {
    return object - sizeof(header);
}

// Removes occupancy information from header if present
uint32_t realSize(uint32_t s) {
    return ~(~s | (uint32_t) 1);
}

header *footerOf(void *object) {
    return object + realSize(headerOf(object)->tailingObjectSize);
}

void insertFreeSpace(void *ptr) {
    int size = realSize(headerOf(ptr)->tailingObjectSize);

#ifdef DEBUG_INSERT
    printf("[INSERT] Inserting free space at %p (%d bytes)\n", ptr, size);
#endif

    if (size == 8) {
#ifdef DEBUG_INSERT
        printf("[INSERT] Special case: length == 8. Inserting in bucket[0]. Using single linked list.\n");
#endif
#ifdef DEBUG_INSERT
        printf("[INSERT] buckets[0] = %p.\n", buckets[0]);
#endif
        *(void **) ptr = buckets[0];
#ifdef DEBUG_INSERT
        printf("[INSERT] Value at ptr is now %p.\n", *(void **) ptr);
#endif
        buckets[0] = ptr;
#ifdef DEBUG_INSERT
        printf("[INSERT] buckets[0] = %p.\n", buckets[0]);
#endif
        return;
    }

    int index = (size >> 3) - 1;
    if (index >= NUMBER_OF_BUCKETS) {
        index = NUMBER_OF_BUCKETS - 1;
    }

    ((doublePointer *) ptr)->firstPointer = 0;
    ((doublePointer *) ptr)->secondPointer = buckets[index];
    if (buckets[index] != 0) {
        buckets[index]->firstPointer = ptr;
    }
    buckets[index] = ptr;

#ifdef DEBUG_INSERT
    printf("[INSERT] Free space (%p) has been put into bucket %d.\n", ptr, index);
#endif
}

// Removes free space from the list it belongs to
void removeFreeSpaceFromList(doublePointer *p) {
#ifdef DEBUG_REMOVE_LIST
    printf("[REMOVE_LIST] Called for %p\n", p);
#endif


    if (realSize(headerOf(p)->tailingObjectSize) == 8) {
#ifdef DEBUG_REMOVE_LIST
        printf("[REMOVE_LIST] Special case: size == 8. Searching for previous in list.\n");
#endif

        if (p == buckets[0]) {
#ifdef DEBUG_REMOVE_LIST
            printf("[REMOVE_LIST] Is first element of single list. Not iterating.\n");
#endif
            buckets[0] = *(void **) buckets[0];
#ifdef DEBUG_REMOVE_LIST
            printf("[REMOVE_LIST] buckets[0] is now %p.\n", buckets[0]);
#endif
            return;
        } else {
#ifdef DEBUG_REMOVE_LIST
            printf("[REMOVE_LIST] Ignoring because we would have to search through list.\n");
#endif
            return;
        }
    }

    doublePointer *prevObject = p->firstPointer;
    doublePointer *followingObject = p->secondPointer;

#ifdef DEBUG_REMOVE_LIST
    printf("[REMOVE_LIST] Previous listelement is %p, following is %p\n", prevObject, followingObject);
#endif

    if (prevObject == 0) {
        // Space is at start of list
        if (followingObject != 0) {
            followingObject->firstPointer = 0;
        }

        int size = headerOf(p)->tailingObjectSize;
        int index = (size / 8) - 1;
        if (index >= NUMBER_OF_BUCKETS) {
            index = NUMBER_OF_BUCKETS - 1;
        }
        buckets[index] = followingObject;
    } else {
        prevObject->secondPointer = followingObject;

        // object could be at the end of list
        if (followingObject != 0) {
            followingObject->firstPointer = prevObject;
        }
    }
}

#ifdef DEBUG_USED

void printUsed() {
    printf("Used: %f%%\n", 100 * (double) sumUsed / sumAvailable);
}

#endif

/**
 * Initializes page with header + footer
 * @return Pointer to start (header) of initialized page
 */
page *initNewPage() {
    void *ret = get_block_from_system();

#ifdef DEBUG_USED
    sumAvailable += BLOCKSIZE;
#endif


#if defined(DEBUG_PAGE_INIT) || defined(DEBUG_GETBLOCK)
    printf("[PAGE INIT] New block: %p (Size: %d)\n", ret, BLOCKSIZE);
#endif


    if (!ret) {
        puts("\033[92m[ERROR] --> initNewPage: OUT OF MEMORY\033[0m");
        exit(1);
    }

    //Header an den Anfang der Page setzen
    header *head = headerOf(ret + sizeof(header));
    head->tailingObjectSize = (BLOCKSIZE - 2 * sizeof(header)) | 1;
    head->precedingObjectSize = START_OF_PAGE;

    //"Footer" (header verwendet als Footer) an den Ende der Page setzen
    header *foot = footerOf(ret + sizeof(header));
    foot->precedingObjectSize = head->tailingObjectSize;
    foot->tailingObjectSize = END_OF_PAGE;

    // "First and last" element
    ((doublePointer *) (ret + sizeof(header)))->firstPointer = 0;
    ((doublePointer *) (ret + sizeof(header)))->secondPointer = 0;

#ifdef DEBUG_PAGE_INIT
    printf("[PAGE INIT] Done init page for objectsize %d.\n", head->tailingObjectSize);
#endif

    return ret;
}

void init_my_alloc() {
}

void *my_alloc(size_t size) {
#ifdef DEBUG_ALLOC
    printf("\033[96m[ALLOC] Allocating %ld bytes\n\033[0m", size);
#endif

#ifdef DEBUG_USED
    sumUsed += size;
    printUsed();
#endif

    // Insert remaining space in corresponding list (bucket)

    // Pointer to allocated space
    void *object = 0;

    int i = (int) ((size >> 3) - 1);

    // If there is no free space with exact size
    if (buckets[i] == 0) {

        // Search possible locations, choose the largest (to prevent fragmentation / very small free spaces)
        int possibleLocations[5];
        int n = 0;
        while (i < NUMBER_OF_BUCKETS - 1 && n < 5) {
            if (buckets[i] != 0) {
                possibleLocations[n] = i;
                ++n;
            }
            ++i;
        }

        if (n == 0) {
            i = NUMBER_OF_BUCKETS - 1;
        } else {
            i = possibleLocations[n - 1];
        }

    }

    object = buckets[i];


    if (object == 0) {
        // Did not find a space large enough.
        // New Page

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Did not find space. Requesting new page.\n");
#endif

        void *newPage = initNewPage();

        buckets[NUMBER_OF_BUCKETS - 1] = newPage + sizeof(header);
        object = buckets[NUMBER_OF_BUCKETS - 1];
    }

    header *objectHeader = headerOf(object);
    header *objectFooter;

    // We may have a space that is larger than what we need
    int availableObjectSize = realSize(objectHeader->tailingObjectSize);

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Found free space with objectsize %d in bucket %d at %p.\n", availableObjectSize, i, object);
#endif

    if (availableObjectSize == size + sizeof(header)) {
        // The remaining free space would not fit an actual object, just its header.
        // These 8 bytes are wasted, but 0 size objects are not possible currently (size 0 <=> end/start of block)

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Allocating 8 byte more.\n");
#endif
        size += sizeof(header);
    }

    // Remove that free space from the corresponding list
    removeFreeSpaceFromList(object);

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Removed space from free list. bucket[%d]=%p\n", i, buckets[i]);
#endif

    // Set header + footer of new object
    objectHeader->tailingObjectSize = (uint32_t) size;
    objectFooter = footerOf(object);
    objectFooter->precedingObjectSize = (uint32_t) size;

    if (availableObjectSize > size) {
        // Space for another object is remaining

        void *remainingFreeObjectPtr = object + size + sizeof(header);
        uint32_t remainingObjectSpace = (uint32_t) (availableObjectSize - size - sizeof(header));

#ifdef DEBUG_ALLOC
        printf("[ALLOC] There are %d bytes of object space left at %p.\n", remainingObjectSpace,
               remainingFreeObjectPtr);
#endif
        header *freeObjectHeader = objectFooter;
        freeObjectHeader->tailingObjectSize = remainingObjectSpace | 1;
        header *freeObjectFooter = footerOf(remainingFreeObjectPtr);
        freeObjectFooter->precedingObjectSize = remainingObjectSpace | 1;

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Initialized free space (Header at %p, Footer at %p).\n", freeObjectHeader, freeObjectFooter);
#endif
        insertFreeSpace(remainingFreeObjectPtr);
    }

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Allocated address %p for size %d\n", object, headerOf(object)->tailingObjectSize);
#endif

    return object;
}

void my_free(void *ptr) {

    // Size of object to be deleted
    int objectSize = headerOf(ptr)->tailingObjectSize;

    // Size of resulting free space
    int totalFreeSize = objectSize;

#ifdef DEBUG_FREE
    printf("[FREE] Free called for %p (object size %d)\n", ptr, objectSize);
#endif

#ifdef DEBUG_USED
    sumUsed -= objectSize;
    printUsed();
#endif

    // Combine tailing free space
    if (footerOf(ptr)->tailingObjectSize & 1) {
        // Tailing object is also empty

#ifdef DEBUG_FREE
        printf("[FREE] There is free space (object size %d) behind object.\n",
               realSize(footerOf(ptr)->tailingObjectSize));
#endif

        int tailingObjectSize = realSize(footerOf(ptr)->tailingObjectSize);

        void *tailingObject = ptr + objectSize + sizeof(header);

        if (tailingObjectSize != 8 || tailingObject == buckets[0]) {
            totalFreeSize = objectSize + sizeof(header) + tailingObjectSize;


#ifdef DEBUG_FREE
            printf("[FREE] Concatenating free space behind (object %p).\n", tailingObject);
#endif

#ifdef DEBUG_FREE
            printf("[FREE] Removing object from list.\n");
#endif
            removeFreeSpaceFromList(tailingObject);
        }
    }

    // Combine preceding free space
    if (headerOf(ptr)->precedingObjectSize & 1) {
        int precedingObjectSize = realSize(headerOf(ptr)->precedingObjectSize);
        void *precedingObject = ptr - precedingObjectSize - sizeof(header);

        if (precedingObjectSize != 8 || precedingObject == buckets[0]) {
            totalFreeSize += precedingObjectSize + sizeof(header);

#ifdef DEBUG_FREE
            printf("[FREE] There is free space (object size %d) before object.\n",
                   precedingObjectSize);
#endif

            removeFreeSpaceFromList(precedingObject);
            ptr = precedingObject;
        }
    }

    // expand free object
    headerOf(ptr)->tailingObjectSize = (uint32_t) totalFreeSize | 1;
    footerOf(ptr)->precedingObjectSize = (uint32_t) totalFreeSize | 1;

    insertFreeSpace(ptr);

#ifdef DEBUG_FREE
    printf("[FREE] Done.\n");
#endif
}
