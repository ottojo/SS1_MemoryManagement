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

#define DEBUG_INIT
#define DEBUG_PAGE_INIT
#define DEBUG_ALLOC
#define DEBUG_GETBLOCK
// #define DEBUG_FREE

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

header *footerOf(void *object) {
    return object + headerOf(object)->tailingObjectSize;
}


page *initNewPage();

/*
	Erster Versuch einer Speicherverwaltung, verwendete Konzepte: Header und Footer sowie Binning 
	(vgl. http://www.cs.princeton.edu/courses/archive/spr09/cos217/lectures/19DynamicMemory2.pdf)
	
	Konvention: Ist die Größenangabe in Header oder Footer (bzw. dem zusammengelegten Part) ungerade (LSB == 1),
				so ist das Feld frei. Die "wirkliche" Größe muss dann durch Subtraktion dieser 1 errechnet werden.
				Insbesondere sind also gerade bzw. durch 8 teilbare Größenangaben belegte Felder.
*/

void init_my_alloc() {
#ifdef DEBUG_INIT
    printf("\033[95m[INIT] Initialisiere.\n\033[0m");
#endif

    for (uint8_t i = 0; i < NUMBER_OF_BUCKETS; i++) {
        buckets[i] = initNewPage();
    }

#ifdef DEBUG_INIT
    printf("\033[95m[INIT] Initialisierung beendet.\n\033[0m");
#endif
}

void *my_alloc(size_t size) {
#ifdef DEBUG_ALLOC
    printf("\033[96m\n[ALLOC] Allokiere: %ld\n\033[0m", size);
#endif


    // Find space
    // First fit...

    void *object = 0;

    for (int i = (int) ((size >> 3) - 1); i < NUMBER_OF_BUCKETS; i++) {
        if (buckets[i] != 0) {
            object = buckets[i];

            int availableObjectSize = headerOf(object)->tailingObjectSize;

            printf("Found free space with objectsize %d in bucket %d at %p.\n", availableObjectSize, i, object);

            // "Increment" free space list
            buckets[i] = ((freeSpaceListElement *) object)->next;

            // Set header + footer of new object
            headerOf(object)->tailingObjectSize = (uint32_t) size;
            footerOf(object)->precedingObjectSize = (uint32_t) size;

            // Maybe there was more space than we need
            if (availableObjectSize == size + 8) {
                // Allocate 1 byte more
                // TODO allocate those bytes maybe (could be used after concatenating free spaces)
                headerOf(object)->tailingObjectSize = (uint32_t) (size + 1);
                footerOf(object)->precedingObjectSize = (uint32_t) (size + 1);
            } else if (availableObjectSize > size + 8) {
                // Space for another object is remaining

                void *nextObject = object + sizeof(header);

                uint32_t remainingObjectSpace = (uint32_t) (availableObjectSize - size - sizeof(header));
                headerOf(nextObject)->tailingObjectSize = remainingObjectSpace;
                footerOf(nextObject)->precedingObjectSize = remainingObjectSpace;

                // Put that free space in some list
                int index = (availableObjectSize >> 3) - 1;
                if (index >= NUMBER_OF_BUCKETS) {
                    index = NUMBER_OF_BUCKETS - 1;
                }
                ((freeSpaceListElement *) nextObject)->next = buckets[i];
                buckets[i] = nextObject;

            }
            break;
        } else {
            printf("Bucket %d does not contain space.\n", i);
        }
    }

    if (object == 0) {
        // Did not find anything.
        printf("Fuck.\n");
    }


    return object;
}

void my_free(void *ptr) {

}

/**
 * Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer)
*/
page *initNewPage() {
    void *ret = get_block_from_system();
#ifdef DEBUG_PAGE_INIT
    printf("[PAGE INIT] Block: %p (Size: %d)\n", ret, BLOCKSIZE);
#endif

    if (!ret) {
        puts("\033[92m[ERROR] --> initNewPage: OUT OF MEMORY\033[0m");
        exit(1);
    }

    //Header an den Anfang der Page setzen
    header *head = headerOf(ret + sizeof(header));
    head->tailingObjectSize = (BLOCKSIZE - 2 * sizeof(header));
    // No preceding object
    head->precedingObjectSize = START_OF_PAGE;

    //"Footer" (header verwendet als Footer) an den Ende der Page setzen
    header *foot = footerOf(ret + sizeof(header));
    foot->precedingObjectSize = head->tailingObjectSize;
    foot->tailingObjectSize = END_OF_PAGE;

#ifdef DEBUG_PAGE_INIT
    //TODO fix

    // printf("\033[90m[PAGE INIT] Header und Footer erstellt (Footer-pos: %p, Nutzgroesse: %d)\n\033[0m", head,
    //      head->tailSize - 1);
#endif

    return ret + sizeof(header);
}
