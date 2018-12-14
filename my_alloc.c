#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 32

typedef void page;

typedef void *doublePointer;

// Bucket contains linked list of free spaces of size 8 * (n + 1)
// Last bucket may contain larger free spaces.
doublePointer *buckets[NUMBER_OF_BUCKETS];

uintptr_t pointerPrefix;

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PAGE_INIT
#define DEBUG_ALLOC
#define DEBUG_GETBLOCK
#define DEBUG_FREE
#endif
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

header *footerOf(void *object) {
    return object + realSize(headerOf(object)->tailingObjectSize);
}

#define LOW32 0x00000000ffffffff
#define HIGH32 0xffffffff00000000
#define DOUBLENULL ((doublePointer) 0x0000000100000001)

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
}

void setSecond(doublePointer *d, void *p) {
    if (p == 0) {
        p = (void *) 1;
    }
    //printf("Set second\n");
    *d = (doublePointer) (((uintptr_t) *d & HIGH32) | ((uintptr_t) p & LOW32));
}

/**
 * Initializes page with header + footer
 * @return Pointer to start (header) of initialized page
 */
page *initNewPage() {
    void *ret = get_block_from_system();

    if (!pointerPrefix) {
        // Lets assume the first 32 bits in every pointer are equal...
        // (https://www.youtube.com/watch?v=gY2k8_sSTsE)
        pointerPrefix = (uintptr_t) ret & HIGH32;
        printf("[PTR] Set prefix to %lx\n", (unsigned long) pointerPrefix);
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
    header *foot = footerOf(ret + sizeof(header));
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
        //buckets[NUMBER_OF_BUCKETS - 1]->next = 0;
        object = buckets[NUMBER_OF_BUCKETS - 1];
    }

    header *objectHeader = headerOf(object);
    header *objectFooter;

    // We may have a space that is larger than what we need
    int availableObjectSize = realSize(objectHeader->tailingObjectSize);

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Found free space with objectsize %d in bucket %d at %p.\n", availableObjectSize, i, object);
#endif

    // Remove that free space from the corresponding list
    buckets[i] = secondPointer(*(doublePointer *) object);

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Removed space from free list. bucket[%d]=%p\n", i, buckets[i]);
#endif

    // Set header + footer of new object
    objectHeader->tailingObjectSize = (uint32_t) size;
    objectFooter = footerOf(object);
    objectFooter->precedingObjectSize = (uint32_t) size;

    // Maybe there was more space than we need
    if (availableObjectSize == size + sizeof(header)) {

        // The remaining free space would not fit an actual object, just its header.
        // These 8 bytes are wasted, but 0 size objects are not possible currently (size 0 <=> end/start of block)

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Allocating 8 byte more.\n");
#endif

        objectHeader->tailingObjectSize = (uint32_t) (size + sizeof(header));
        // Footer is now somewhere else
        objectFooter = footerOf(object);
        objectFooter->precedingObjectSize = (uint32_t) (size + sizeof(header));
    } else if (availableObjectSize > size + sizeof(header)) {
        // Space for another object is remaining

        void *nextObject = object + size + sizeof(header);
        uint32_t remainingObjectSpace = (uint32_t) (availableObjectSize - size - sizeof(header));

#ifdef DEBUG_ALLOC
        printf("[ALLOC] There are %d bytes of object space left at %p.\n", remainingObjectSpace, nextObject);
#endif
        header *nextObjectHeader = objectFooter;
        nextObjectHeader->tailingObjectSize = remainingObjectSpace | 1;
        header *nextObjectFooter = footerOf(nextObject);
        nextObjectFooter->precedingObjectSize = remainingObjectSpace | 1;

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Initialized free space (Header at %p, Footer at %p).\n", nextObjectHeader, nextObjectFooter);
#endif

        // Put that free space in the correct list (at the start)
        int remainingSpaceIndex = (remainingObjectSpace / 8) - 1;
        if (remainingSpaceIndex >= NUMBER_OF_BUCKETS) {
            remainingSpaceIndex = NUMBER_OF_BUCKETS - 1;
        }

        setFirst(nextObject, 0);
        setSecond(nextObject, buckets[remainingSpaceIndex]);
        buckets[remainingSpaceIndex] = nextObject;

#ifdef DEBUG_ALLOC
        printf("[ALLOC] Free space (%p) has been put into bucket %d.\n", nextObject, remainingSpaceIndex);
#endif
    }


#ifdef DEBUG_ALLOC
    printf("[ALLOC] Allocated address %p for size %d\n", object, headerOf(object)->tailingObjectSize);
#endif

    return object;
}

void my_free(void *ptr) {

    // Size of object to be deleted
    int objectSize = realSize(headerOf(ptr)->tailingObjectSize);
    // Size of resulting free space
    int totalFreeSize = objectSize;

#ifdef DEBUG_FREE
    printf("[FREE] Free called for %p (object size %d)\n", ptr, objectSize);
#endif


















    // Combine tailing free space
    if (footerOf(ptr)->tailingObjectSize & 1) {
        // Tailing object is also empty

#ifdef DEBUG_FREE
        printf("[FREE] There is free space (object size %d) behind object.\n",
               realSize(footerOf(ptr)->tailingObjectSize));
#endif

        int tailingObjectSize = realSize(footerOf(ptr)->tailingObjectSize);
        totalFreeSize = objectSize + sizeof(header) + tailingObjectSize;

        void *objectToConcat = ptr + objectSize + sizeof(header);

#ifdef DEBUG_FREE
        printf("[FREE] Concatenating free space behind (object %p).\n", objectToConcat);
#endif

        // Delete references to tailing free space

        // Should be in this bucket
        int bucketToSearch = (tailingObjectSize / 8) - 1;
        if (bucketToSearch >= NUMBER_OF_BUCKETS) {
            bucketToSearch = NUMBER_OF_BUCKETS - 1;
        }

        void *prevFreeObjectLocation = firstPointer(*(doublePointer *) objectToConcat);

#ifdef DEBUG_FREE
        printf("[FREE] Found this pointer in list, the previous element in list is %p.\n", prevFreeObjectLocation);
#endif

        if (prevFreeObjectLocation == 0) {
            // objectToConcat is buckets[i]
#ifdef DEBUG_FREE
            printf("[FREE] Object to concat is buckets[%d] (first element in list).\n", bucketToSearch);
#endif
            buckets[bucketToSearch] = secondPointer(objectToConcat);
#ifdef DEBUG_FREE
            printf("[FREE] buckets[%d] = %p (should not equal %p)\n", bucketToSearch, buckets[bucketToSearch],
                   objectToConcat);
#endif
            if (buckets[bucketToSearch] == objectToConcat) {
                buckets[bucketToSearch] = 0;
            }
            setFirst(objectToConcat, 0);
        } else {
#ifdef DEBUG_FREE
            printf("[FREE] Removing object from list.\n");
#endif
            // Remove objectToConcat from list
            setSecond(prevFreeObjectLocation, secondPointer(*(doublePointer *) objectToConcat));
            setFirst(secondPointer(*(doublePointer *) objectToConcat), prevFreeObjectLocation);
        }
    }


















    // Concat preceding (more or less same as above)
    if (headerOf(ptr)->precedingObjectSize & 1) {
        int precedingObjectSize = realSize(headerOf(ptr)->precedingObjectSize);
        totalFreeSize += precedingObjectSize + sizeof(header);

#ifdef DEBUG_FREE
        printf("[FREE] There is free space (object size %d) before object.\n",
               precedingObjectSize);
#endif

        // 1. move ptr back
        ptr -= precedingObjectSize + sizeof(header);

        // 2. Delete references to preceding free space

        // Should be in this bucket
        int bucketToSearch = (precedingObjectSize / 8) - 1;
        if (bucketToSearch >= NUMBER_OF_BUCKETS) {
            bucketToSearch = NUMBER_OF_BUCKETS - 1;
        }

        void *prevFreeObjectLocation = firstPointer(*(doublePointer *) ptr);

        if (prevFreeObjectLocation == 0) {
            // objectToConcat is buckets[i] (first element in list)
            // Delete from bucket.
            buckets[bucketToSearch] = secondPointer(ptr);


            if (buckets[bucketToSearch] == ptr) {
                buckets[bucketToSearch] = 0;
            }
            setFirst(ptr, 0);

        } else {
            setSecond(prevFreeObjectLocation, secondPointer(*(doublePointer *) ptr));
            setFirst(secondPointer(*(doublePointer *) ptr), prevFreeObjectLocation);
        }
    }







    // expand free object
    headerOf(ptr)->tailingObjectSize = (uint32_t) totalFreeSize | 1;
    footerOf(ptr)->precedingObjectSize = (uint32_t) totalFreeSize | 1;
















    // Now that we have concatenated, insert new free space into appropriate list (at the start because why not)
    int index = (totalFreeSize / 8) - 1;
    if (index >= NUMBER_OF_BUCKETS) {
        index = NUMBER_OF_BUCKETS - 1;
    }
#ifdef DEBUG_FREE
    printf("[FREE] Inserting free space (%d bytes) in list (bucket %d)\n", totalFreeSize, index);
#endif

    if (secondPointer(*(doublePointer *) ptr) != buckets[index]) {
        setSecond(ptr, buckets[index]);
    } else {
        setSecond(ptr, 0);
    }
    setFirst(ptr, 0);
    buckets[index] = ptr;

#ifdef DEBUG_FREE
    printf("[FREE] Done.\n");
#endif
}
