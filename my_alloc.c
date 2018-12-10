#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 33

//Notwendig für die Umsetzung des Konzepts des Binnings
static void* buckets[NUMBER_OF_BUCKETS];

#define DEBUG_INIT
#define DEBUG_PAGE_INIT
// #define DEBUG_ALLOC
// #define DEBUG_GETBLOCK
// #define DEBUG_FREE

typedef struct header {
	uint32_t tailSize;
	uint32_t headSize;
} header;

void* initNewPage();
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
	
	printf("Void: %ld\n", sizeof(void));
	
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS - 1; i++) {
		buckets[i] = NULL;
	}
	
	buckets[NUMBER_OF_BUCKETS - 1] = initNewPage();
	
	#ifdef DEBUG_INIT
		printf("\033[95m[INIT] Initialisierung beendet.\n\033[0m");
	#endif
}

void* my_alloc(size_t size) {
	#ifdef DEBUG_ALLOC 
		printf("\033[96m\n[ALLOC] Allokiere: %ld\n\033[0m", size);
	#endif
	
	return NULL;
}

void my_free(void* ptr) {
	
}

/**
Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer)
*/
void* initNewPage() {
	void* ret = get_block_from_system();
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Block: %p (Size: %d)\n\033[0m", ret, BLOCKSIZE); 
	#endif
	
	if(!ret) {
		#ifdef DEBUG_PAGE_INIT
			puts("\033[92m[ERROR] --> initNewPage: OUT OF MEMORY\033[0m");
			exit(-1);
		#endif
	}
	
	//Header an den Anfang der Page setzen
	header *head = ((header*)ret);
	head->tailSize = 0;
	head->headSize = BLOCKSIZE - 4* sizeof(uint32_t) + 1;	//+1 als Flag für freies Feld
	
	//"Footer" (header verwendet als Footer) an den Ende der Page setzen
	head = ((header*)(ret + BLOCKSIZE - 2*sizeof(uint32_t)));
	head->tailSize = BLOCKSIZE - 4*sizeof(uint32_t) + 1;
	head->headSize = 0;
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Header und Footer erstellt (Footer-pos: %p, Nutzgroesse: %d)\n\033[0m", head, head->tailSize - 1); 
	#endif
	
	ret += 2*sizeof(uint32_t);			//Zeiger hinter Header-Paket verschieben
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Zeiger verschoben: %p\n\033[0m", ret); 
	#endif
	
	return ret;
}
