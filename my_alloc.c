#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 32

typedef void page;

// Doublepointer: To fit a doubly linked list in 8 byte objects we only store the lower 32bit of each pointer.

typedef void *doublePointer;

// High 32 bit of pointer
uintptr_t pointerPrefix;

// Some useful bitmasks
#define LOW32 0x00000000ffffffff
#define HIGH32 0xffffffff00000000

// Instead of storing a nullpointer as 0, we store a nullpointer as 1.
// This works because all pointers we use are multiple of 8.
// This is necessary to differentiate between nullpointer and first byte of first block.
#define DOUBLENULL ((doublePointer) 0x0000000100000001)

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
#endif

#ifdef DEBUG_USED
long int sumUsed = 0;
long int sumAvailiable = 0;
#endif

// Each 8 byte header stores the size of the object before and after it.

// Header of 0: end of page
#define END_OF_PAGE 0
// Footer of 0: start of page
#define START_OF_PAGE 0
// LSB == 1: space not occupied
typedef struct header {
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

// Utility methods for doublepointer
void *firstPointer(doublePointer d) {
    if (((uintptr_t) d >> 32) & 1) {
        return 0;
    }
    return (void *) ((uintptr_t) d >> 32 | pointerPrefix);
}

void *secondPointer(doublePointer d) {
    if ((uintptr_t) d & 1) {
        return 0;
    }
    return (void *) (((uintptr_t) d & LOW32) | pointerPrefix);
}

void setFirst(doublePointer *d, void *p) {
    if (p == 0) {
        p = (void *) 1;
    }
    *d = (doublePointer) (((uintptr_t) *d & LOW32) | ((uintptr_t) p << 32));
#ifdef DEBUG_DOUBLEPOINTER
    printf("[SETFIRST] Writing to %p\n", d);
#endif
}

void setSecond(doublePointer *d, void *p) {
    if (p == 0) {
        p = (void *) 1;
    }
    *d = (doublePointer) (((uintptr_t) *d & HIGH32) | ((uintptr_t) p & LOW32));
#ifdef DEBUG_DOUBLEPOINTER
    printf("[SETSECOND] Writing to %p\n", d);
#endif
}

// Removes free space from the list it belongs to
void removeFreeSpaceFromList(doublePointer *p) {
#ifdef DEBUG_REMOVE_LIST
    printf("[REMOVE_LIST] Called for %p\n", p);
#endif
    doublePointer *prevObject = firstPointer(*p);
    doublePointer *followingObject = secondPointer(*p);

#ifdef DEBUG_REMOVE_LIST
    printf("[REMOVE_LIST] Previous listelement is %p, following is %p\n", prevObject, followingObject);
#endif

    if (prevObject == 0) {
        // Space is at start of list
        if (followingObject != 0) {
            setFirst(followingObject, 0);
        }

        int size = headerOf(p)->tailingObjectSize;
        int index = (size / 8) - 1;
        if (index >= NUMBER_OF_BUCKETS) {
            index = NUMBER_OF_BUCKETS - 1;
        }
        buckets[index] = followingObject;
    } else {
        setSecond(prevObject, followingObject);

        // object could be at the end of list
        if (followingObject != 0) {
            setFirst(followingObject, prevObject);
        }
    }
}

#ifdef DEBUG_USED

void printUsed() {
    printf("Used: %f%%\n", 100 * (double) sumUsed / sumAvailiable);
}

#endif

/**
 * Initializes page with header + footer
 * @return Pointer to start (header) of initialized page
 */
page *initNewPage() {
    void *ret = get_block_from_system();

#ifdef DEBUG_USED
    sumAvailiable += BLOCKSIZE;
#endif

    if (!pointerPrefix) {
        // Lets assume the first 32 bits in every pointer are equal...
        // (https://www.youtube.com/watch?v=gY2k8_sSTsE)
        pointerPrefix = (uintptr_t) ret & HIGH32;
#ifdef DEBUG_DOUBLEPOINTER
        printf("[PTR] Set prefix to %lx\n", (unsigned long) pointerPrefix);
#endif
    }

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
    // header *foot = footerOf(ret + sizeof(header));
    header *foot = ret + BLOCKSIZE - sizeof(header);
    foot->precedingObjectSize = head->tailingObjectSize;
    foot->tailingObjectSize = END_OF_PAGE;

    // "First and last" element
    *((doublePointer *) (ret + sizeof(header))) = DOUBLENULL;

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

    // Use the first free space large enough to fit the required size.
    // Insert remaining space in corresponding list (bucket)

    // Pointer to allocated space
    void *object = 0;

    // Search all buckets (beginning with smallest usable size) for one that has a free space:
    int i = (int) ((size >> 3) - 1);
    while (i < NUMBER_OF_BUCKETS - 1 && buckets[i] == 0) {
        ++i;
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
    objectFooter = object + size;
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
        header *freeObjectFooter = remainingFreeObjectPtr + remainingObjectSpace;
        freeObjectFooter->precedingObjectSize = remainingObjectSpace | 1;

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Initialized free space (Header at %p, Footer at %p).\n", freeObjectHeader, freeObjectFooter);
#endif

        // Put that free space in the correct list (at the start)
        int remainingSpaceIndex = (remainingObjectSpace / 8) - 1;
        if (remainingSpaceIndex >= NUMBER_OF_BUCKETS) {
            remainingSpaceIndex = NUMBER_OF_BUCKETS - 1;
        }

        // Has no previous free space
        setFirst(remainingFreeObjectPtr, 0);
        // Following free space is whatever is currently at the start
        setSecond(remainingFreeObjectPtr, buckets[remainingSpaceIndex]);

        // If the list wasn't empty before, point it to the new start
        if (buckets[remainingSpaceIndex]) {
            setFirst(buckets[remainingSpaceIndex], remainingFreeObjectPtr);
        }

        // Start of list is this free space
        buckets[remainingSpaceIndex] = remainingFreeObjectPtr;

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Free space (%p) has been put into bucket %d.\n", remainingFreeObjectPtr, remainingSpaceIndex);
#endif

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Inserting free space (%d bytes) in list (bucket %d)\n", remainingObjectSpace,
               remainingSpaceIndex);
#endif
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
    header *footer = ptr + objectSize;
    if (footer->tailingObjectSize & 1) {
        // Tailing object is also empty

#ifdef DEBUG_FREE
        printf("[FREE] There is free space (object size %d) behind object.\n",
               realSize(footerOf(ptr)->tailingObjectSize));
#endif

        int tailingObjectSize = realSize(footer->tailingObjectSize);
        totalFreeSize = objectSize + sizeof(header) + tailingObjectSize;

        void *tailingObject = ptr + objectSize + sizeof(header);

#ifdef DEBUG_FREE
        printf("[FREE] Concatenating free space behind (object %p).\n", tailingObject);
#endif

#ifdef DEBUG_FREE
        printf("[FREE] Removing object from list.\n");
#endif
        removeFreeSpaceFromList(tailingObject);
    }

    // Combine preceding free space
    if (headerOf(ptr)->precedingObjectSize & 1) {
        int precedingObjectSize = realSize(headerOf(ptr)->precedingObjectSize);
        totalFreeSize += precedingObjectSize + sizeof(header);

#ifdef DEBUG_FREE
        printf("[FREE] There is free space (object size %d) before object.\n",
               precedingObjectSize);
#endif

        void *precedingObject = ptr - precedingObjectSize - sizeof(header);
        removeFreeSpaceFromList(precedingObject);
        ptr = precedingObject;
    }

    // expand free object
    headerOf(ptr)->tailingObjectSize = (uint32_t) totalFreeSize | 1;
    footer = ptr + totalFreeSize;
    footer->precedingObjectSize = (uint32_t) totalFreeSize | 1;

    // Insert
    int index = (totalFreeSize / 8) - 1;
    if (index >= NUMBER_OF_BUCKETS) {
        index = NUMBER_OF_BUCKETS - 1;
    }

#ifdef DEBUG_FREE
    printf("[FREE] Inserting free space (%d bytes) in list (bucket %d)\n", totalFreeSize, index);
#endif

    setSecond(ptr, buckets[index]);
    setFirst(ptr, 0);

    if (buckets[index] != 0) {
        setFirst(buckets[index], ptr);
    }

    buckets[index] = ptr;

#ifdef DEBUG_FREE
    printf("[FREE] Done.\n");
#endif
}
