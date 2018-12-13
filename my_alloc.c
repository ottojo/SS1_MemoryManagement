#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 32

typedef void page;

typedef struct freeSpaceListElement {
    struct freeSpaceListElement *next;
} freeSpaceListElement;


// Bucket contains linked list of free spaces of size 8 * (n + 1)
// Last bucket may contain larger free spaces.
freeSpaceListElement *buckets[NUMBER_OF_BUCKETS];

void *firstBlock;

//#define DEBUG_INIT
//#define DEBUG_PAGE_INIT
//#define DEBUG_ALLOC
//#define DEBUG_GETBLOCK
//#define DEBUG_FREE

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

uint32_t realSize(uint32_t s) {
    // Did you know that ! is not bitwise NOT? Now you know~
    //printf("%d is really %d.\n", s, ~(~s | (uint32_t) 1));
    return ~(~s | (uint32_t) 1);
}

header *footerOf(void *object) {
    /*
     printf("Calculating footer of %p\n", object);
     printf("Header is at %p\n", headerOf(object));
     printf("Size is %d\n", headerOf(object)->tailingObjectSize);
     printf("Real size is %d\n", realSize(headerOf(object)->tailingObjectSize));
     printf("Footer is at obj + rsize = %p\n", object + realSize(headerOf(object)->tailingObjectSize));
*/
    return object + realSize(headerOf(object)->tailingObjectSize);
}

void printBlock(void *block) {
    printf("MEMORY DUMP START\n");
    while (true) {
        if (headerOf(block)->tailingObjectSize & 1) {
            printf("Free: %d bytes from %p\n", realSize(headerOf(block)->tailingObjectSize), block);
        } else {
            printf("Used: %d bytes from %p\n", realSize(headerOf(block)->tailingObjectSize), block);
        }
        //printf("Footer is at %p\n", footerOf(block));
        if (footerOf(block)->tailingObjectSize == END_OF_PAGE) {
            break;
        }
        block += realSize(headerOf(block)->tailingObjectSize) + sizeof(header);
    }
    printf("MEMORY DUMP END\n");
}


page *initNewPage();

void init_my_alloc() {
#ifdef DEBUG_INIT
    printf("[INIT]\n");
#endif

    buckets[NUMBER_OF_BUCKETS - 1] = initNewPage() + sizeof(header);

    firstBlock = buckets[NUMBER_OF_BUCKETS - 1];

#ifdef DEBUG_INIT
    printf("\t[INIT] Bucket %d points to object space at %p (Size %d).\n", NUMBER_OF_BUCKETS - 1,
           buckets[NUMBER_OF_BUCKETS - 1],
           headerOf(buckets[NUMBER_OF_BUCKETS - 1])->tailingObjectSize);
#endif
    buckets[NUMBER_OF_BUCKETS - 1]->next = 0;


#ifdef DEBUG_INIT
    printf("[INIT] Done\n");
#endif
}

void *my_alloc(size_t size) {
#ifdef DEBUG_ALLOC
    printf("\033[96m[ALLOC] Allokiere: %ld\n\033[0m", size);
#endif


    // Find space
    // First fit...

    void *object = 0;

    for (int i = (int) ((size >> 3) - 1); i < NUMBER_OF_BUCKETS; i++) {
        if (buckets[i] != 0) {
            object = buckets[i];

#ifdef DEBUG_ALLOC
            printf("Object size is %d\n", headerOf(object)->tailingObjectSize);
            printf("Real object size is %d\n", realSize(headerOf(object)->tailingObjectSize));
#endif

            int availableObjectSize = realSize(headerOf(object)->tailingObjectSize);

#ifdef DEBUG_ALLOC
            printf("[ALLOC] Found free space with objectsize %d in bucket %d at %p.\n", availableObjectSize, i, object);
#endif

            // "Increment" free space list
            buckets[i] = ((freeSpaceListElement *) object)->next;

            // Set header + footer of new object
            headerOf(object)->tailingObjectSize = (uint32_t) size;
            footerOf(object)->precedingObjectSize = (uint32_t) size;

            // Maybe there was more space than we need
            if (availableObjectSize == size + 8) {
#ifdef DEBUG_ALLOC
                printf("Allocating 8 byte more.\n");
#endif
                // Allocate 1 byte more
                // Maybe allocate even 8 bytes more, because everything else would be fucking stupid and would result in
                // wasting HOURS on troubleshooting a seemingly COMPLETELY UNRELATED problem just because this magic
                // number right here was 1 instead of 8.
                // Seriously, who would do that?
                // TODO allocate those bytes maybe (could be used after concatenating free spaces)
                headerOf(object)->tailingObjectSize = (uint32_t) (size + 8);
                footerOf(object)->precedingObjectSize = (uint32_t) (size + 8);
            } else if (availableObjectSize > size + sizeof(header)) {
                // Space for another object is remaining



                void *nextObject = object + size + sizeof(header);
                uint32_t remainingObjectSpace = (uint32_t) (availableObjectSize - size - sizeof(header));

#ifdef DEBUG_ALLOC
                printf("[ALLOC] There are %d bytes of object space left at %p.\n", remainingObjectSpace, nextObject);
#endif
                headerOf(nextObject)->tailingObjectSize = remainingObjectSpace | 1;
                footerOf(nextObject)->precedingObjectSize = remainingObjectSpace | 1;

#ifdef DEBUG_ALLOC
                printf("[ALLOC] Initialized free space (Header at %p, Footer at %p).\n", headerOf(nextObject),
                       footerOf(nextObject));
#endif

                // Put that free space in some list
                int index = (remainingObjectSpace / 8) - 1;
                if (index >= NUMBER_OF_BUCKETS) {
                    index = NUMBER_OF_BUCKETS - 1;
                }
                ((freeSpaceListElement *) nextObject)->next = buckets[index];
                buckets[index] = nextObject;
#ifdef DEBUG_ALLOC
                printf("[ALLOC] Free space has been put into bucket %d.\n", index);
#endif
            }
            break;
        } else {
            //   printf("Bucket %d does not contain space.\n", i);
        }
    }

    if (object == 0) {
        // Did not find anything.
        // New Page

        void *newPage = initNewPage();
#ifdef DEBUG_GETBLOCK
        printf("[ALLOC] Did not find free space. New block: %p.\n", newPage);
#endif

        buckets[NUMBER_OF_BUCKETS - 1] = newPage + sizeof(header);
        buckets[NUMBER_OF_BUCKETS - 1]->next = 0;
        return my_alloc(size);
    }

#ifdef DEBUG_ALLOC
    printf("[ALLOC] Allocated address %p for size %d\n", object, headerOf(object)->tailingObjectSize);
    printBlock(firstBlock);

#endif

    return object;
}

void my_free(void *ptr) {

#ifdef DEBUG_FREE
    printf("First block before free:\n");
    printBlock(firstBlock);
#endif

    // Size of object to be deleted
    // TODO LSB should ALWAYS be 0 here, but it isn't. Seems to be a bug in alloc (forgot to unset LSB?).
    //  It shouldn't be necessary to call realSize here.
    //  Or maybe it works? Pls report bug if you see this debug message.
    //  https://github.com/ottojo/SS1_MemoryManagement/issues/new
    if (headerOf(ptr)->tailingObjectSize & 1) {
        printf("[FREE] Called for %p, but it is marked as empty...\n", ptr);
        exit(13);
    }
    int objectSize = realSize(headerOf(ptr)->tailingObjectSize);
    // Size of resulting free space
    int totalFreeSize = objectSize;

#ifdef DEBUG_FREE
    printf("[FREE] Free called for %p (object size %d)\n", ptr, objectSize);
#endif

    //printf("Footer of object (%p) shows trailing object size of %d, LSB of %d\n", footerOf(ptr),
    //      realSize(footerOf(ptr)->tailingObjectSize), footerOf(ptr)->tailingObjectSize & 1);

    // Concat tailing
    // TODO: make sure that is REALLY free?
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
        // TODO this free space is not deleted
        //  Prev seemst to be the large remaining space at the end
        //  fixed?

        // Delete references to tailing free space

        // Should be in this bucket
        int bucketToSearch = (tailingObjectSize / 8) - 1;
        if (bucketToSearch >= NUMBER_OF_BUCKETS) {
            bucketToSearch = NUMBER_OF_BUCKETS - 1;
        }

        freeSpaceListElement *test = buckets[bucketToSearch];
        freeSpaceListElement *prev = 0;

        // TODO: instead of iterating through list, build doubly linked list

        while (test != objectToConcat && test) {
            prev = test;
            test = test->next;
        }

#ifdef DEBUG_FREE
        printf("Found this pointer in list, the previous element in list is %p.\n", prev);
#endif

        if (prev == 0) {
            // objectToConcat is buckets[i]
#ifdef DEBUG_FREE
            printf("[FREE] Object to concat is bucket[%d] (first element in list).\n", bucketToSearch);
#endif
            buckets[bucketToSearch] = buckets[bucketToSearch]->next;
        } else {
#ifdef DEBUG_FREE
            printf("[FREE] Removing object from list.\n");
#endif
            prev->next = ((freeSpaceListElement *) objectToConcat)->next;
        }
    }

    // Concat preceding
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

        freeSpaceListElement *test = buckets[bucketToSearch];
        freeSpaceListElement *prev = 0;

        // TODO: instead of iterating through list, build doubly linked list

        // FInd previous element in list
        while (test != ptr && test) {
            prev = test;
            test = test->next;
        }

        if (prev == 0) {
            // objectToConcat is buckets[i] (first element in list)
            buckets[bucketToSearch] = buckets[bucketToSearch]->next;
        } else {
            prev->next = ((freeSpaceListElement *) ptr)->next;
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
    printf("[FREE] Inserting free space in list (bucket %d)\n", index);
    printf("[FREE] buckets[%d] comes after current (%p)\n", index, ptr);
#endif
    ((freeSpaceListElement *) ptr)->next = buckets[index];
    buckets[index] = ptr;   //
    headerOf(ptr)->tailingObjectSize = (uint32_t) (totalFreeSize | 1);
    footerOf(ptr)->precedingObjectSize = (uint32_t) (totalFreeSize | 1);

#ifdef DEBUG_FREE
    printf("[FREE] Done. First block after free:\n");
    printBlock(firstBlock);
#endif
}

/**
 * Initializes page with header + footer
 * @return Pointer to start (header) of initialized page
 */
page *initNewPage() {
    void *ret = get_block_from_system();
#ifdef DEBUG_GETBLOCK
    printf("[PAGE INIT] New block: %p (Size: %d)\n", ret, BLOCKSIZE);
#endif

    if (!ret) {
        puts("\033[92m[ERROR] --> initNewPage: OUT OF MEMORY\033[0m");
        exit(1);
    }

    //Header an den Anfang der Page setzen
    header *head = headerOf(ret + sizeof(header));
    head->tailingObjectSize = (BLOCKSIZE - 2 * sizeof(header)) | 1;
    // No preceding object
    head->precedingObjectSize = START_OF_PAGE;

    //"Footer" (header verwendet als Footer) an den Ende der Page setzen
    header *foot = footerOf(ret + sizeof(header));
    foot->precedingObjectSize = head->tailingObjectSize;
    foot->tailingObjectSize = END_OF_PAGE;

#ifdef DEBUG_PAGE_INIT

    printf("[PAGE INIT] Done init page for objectsize %d.\n", head->tailingObjectSize);
#endif

    return ret;
}
