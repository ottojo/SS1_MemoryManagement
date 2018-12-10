#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 33

//Notwendig für die Umsetzung des Konzepts des Binnings
static char* buckets[NUMBER_OF_BUCKETS];


// static size_t offset = 0;
// static char* data = 0;

// #define DEBUG_INIT
// #define DEBUG_ALLOC
// #define DEBUG_GETBLOCK
// #define DEBUG_FREE

typedef struct header {
	size_t size;					
	char *next; 
} header;

typedef struct footer {
	char *prev;
	size_t size;
} footer;

void initNewBlock();
/* 
	Erster Versuch einer Speicherverwaltung, verwendete Konzepte: Header und Footer sowie Binning 
	(vgl. http://www.cs.princeton.edu/courses/archive/spr09/cos217/lectures/19DynamicMemory2.pdf)
*/

void init_my_alloc() {
	#ifdef DEBUG_INIT
		printf("\033[95m[INIT] Initialisiere.\n\033[0m");
	#endif
	
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS - 1; i++) {
		buckets[i] = NULL;
	}
	/*
	buckets[NUMBER_OF_BUCKETS - 1] = get_block_from_system();
	// printf("Block: %p %d\n", buckets[NUMBER_OF_BUCKETS - 1], BLOCKSIZE); 
	if(!buckets[NUMBER_OF_BUCKETS - 1]) {
		#ifdef DEBUG_INIT 
			puts("\033[92mOUT OF MEMORY\033[0m");
			exit(0);
		#endif
	}
	
	//Header erstellen
	// *((size_t*)buckets[NUMBER_OF_BUCKETS - 1]) = BLOCKSIZE - sizeof(header) + 1;
	// buckets[NUMBER_OF_BUCKETS - 1] += sizeof(size_t);			//Zeiger hinter Größe verschieben
	// *((size_t*)buckets[NUMBER_OF_BUCKETS - 1]) = 0;				//Next-Zeiger auf Null setzen
	
	//Header erstellen
	header *head = ((header*)buckets[NUMBER_OF_BUCKETS - 1]);
	head->size = BLOCKSIZE - 2 * sizeof(size_t) + 1;
	head->next = NULL;
	
	// puts("Header erstellt");
	// printf("Head: %ld %p %p\n", head->size, head, (char*)head + (head->size - 1));
	
	//Footer erstellen
	// footer *tail = ((footer*)(buckets[NUMBER_OF_BUCKETS - 1] + head->size - sizeof(size_t));
	footer *tail = ((footer*)(((char*)head) + (head->size - 1)));
	tail->size = head->size;
	tail->prev = NULL;
	
	buckets[NUMBER_OF_BUCKETS - 1] += sizeof(size_t);			//Zeiger hinter Größe verschieben*/
	// printf("Ptr: %p %p\n", &tail->size, (((char*)(buckets[NUMBER_OF_BUCKETS - 1])) + (head->size - 1)));
	// printf("Size: %ld %ld\n", *(((size_t*)(buckets[NUMBER_OF_BUCKETS - 1])) - 1), *((size_t*)(((char*)(buckets[NUMBER_OF_BUCKETS - 1])) + (head->size - 1))));
	// printf("Header size: %ld\n", sizeof(header));
	
	initNewBlock();
	
	#ifdef DEBUG_INIT
		printf("\033[95m[INIT] Initialisierung beendet.\n\033[0m");
	#endif
}

void* my_alloc(size_t size) {
	#ifdef DEBUG_ALLOC 
		printf("\033[96m\n[ALLOC] Allokiere: %ld\n\033[0m", size);
	#endif
	
	//Überprüfe, ob im entsprechenden Fach der geforderten Größe noch Speicher vorhanden ist
	char *ret;
	uint8_t pos = (size / 8) - 1;
	header *head;
	footer *tail;
	
	if(buckets[pos]) {
		#ifdef DEBUG_ALLOC 
			printf("\033[96m[ALLOC] Feld vorhanden in Bucket %d\n\033[0m", pos);
		#endif
		ret = buckets[pos];
		head = (header*)(((char*)buckets[pos]) - sizeof(size_t));
		
		head->size -= 1;			//Flag für freies Feld entfernen;
		
		// tail = ((footer*)(buckets[pos] + head->size - sizeof(size_t)));
		tail = ((footer*)(((char*)head) + head->size));
		tail->size = head->size;
		
		buckets[pos] = head->next;
		
		//Falls nachfolgende Elemente vorhanden: Prev des entsprechenden Feldes auf Null setzen
		if(head->next) {
			head = ((header*)((char*)(head->next) - sizeof(size_t)));
			tail = ((footer*)(((char*)head) + (head->size - 1)));
			tail->prev = NULL;
		}
	} else {
		//Kein freies Feld in gesuchter Größe vorhanden
		#ifdef DEBUG_ALLOC 
			puts("\033[96m[ALLOC] Feld nicht vorhanden\033[0m");
		#endif
		
		//Überprüfe, ob größere Felder vorhanden sind
		//--> präferiere Feld mit 2*pos + 2 (+2 wegen neuem Header und Footer), dann liegt für nächste Anfrage 
		//    so ein Feld bereit
		uint8_t divPos = 2*pos + 2;
		if(divPos < NUMBER_OF_BUCKETS - 1 && buckets[divPos]) {
			#ifdef DEBUG_ALLOC 
				printf("\033[96m[ALLOC] Spalte Feld aus bucket %d\n\033[0m", divPos);
			#endif
		} else {
			//Präferenz nicht möglich
			divPos = (divPos <= 29) ? pos + 3 : NUMBER_OF_BUCKETS - 1;			//29 = 232/8 = (256 - 3 * 8)/8
			while(!buckets[divPos] && divPos < NUMBER_OF_BUCKETS - 1) {
				divPos ++;
			}

			if(divPos == NUMBER_OF_BUCKETS - 1) {
				//Abspalten von Page
				
				size_t restPageSize;
				//Restgröße von Page ermitteln
				if(!buckets[divPos]) {
					//Keine Page mehr vorhanden --> neue holen
					#ifdef DEBUG_ALLOC 
						printf("\033[96m[ALLOC] Hole neue page\n\033[0m");
					#endif
					initNewBlock();
					restPageSize = BLOCKSIZE - 2 * sizeof(size_t) + 1;
				} else {
					restPageSize = *(((size_t*)(buckets[NUMBER_OF_BUCKETS - 1])) - 1);
				}
				
				
				
				#ifdef DEBUG_ALLOC 
					printf("RestPageSize: %ld\n", restPageSize);
					printf("\033[96m[ALLOC] Abspalten von Page");
				#endif
				if(restPageSize >= size + 24) {
					//Abspalten lohnt sich
					#ifdef DEBUG_ALLOC 
						printf(" lohnt sich!\n\033[0m");
					#endif
					
					// printf("Zeiger: %p\n", buckets[divPos]);
					
					ret = buckets[divPos];
					
					//Header und Footer von abzugebenden Feld setzen
					head = (header*)((buckets[divPos]) - sizeof(size_t));
					
					// printf("head size pos: %p\n", &head->size);
					
					// printf("Size before change: %ld\n", head->size);
					
					tail = ((footer*)(((char*)head) + size));
					tail->size = size;
					// printf("pos tail front: %p\n", &tail->size);
					// printf("Tail front new: %ld\n", tail->size);
					
					// printf("head: %p %ld %p\n", head, size, tail);
					tail = ((footer*)(((char*)head) + (head->size - 1)));
					// printf("tail size back: %p\n", &tail->size);
					tail->size -= (size + 2*sizeof(size_t));
					
					head->size = size;
					
					head = ((header*)(((char*)tail) - (tail->size - 1)));
					head->size = tail->size;
					//printf("head size pos back: %p\n", &head->size);
					// printf("Tail back new: %ld\n", tail->size);
					
					buckets[divPos] = (char*)&(head->next);
					
					if(head->size <= (256 + 2*sizeof(size_t))) {
						buckets[(((head->size - 1) - 2*sizeof(size_t)) / 8) - 1] = buckets[divPos];
						#ifdef DEBUG_ALLOC 
							printf("\033[96m[ALLOC] Page-Rest eingeordnet bei %ld (Size: %ld)\n\033[0m", (((head->size - 1) - 2*sizeof(size_t)) / 8) - 1, head->size - 1 - 2*sizeof(size_t));
						#endif
						buckets[divPos] = NULL;
					} 
					#ifdef DEBUG_ALLOC 
						else {
							printf("\033[96m[ALLOC] Page-Rest nicht neu eingeordnet!\n\033[0m");
						}
					#endif
				} else {
					//Abspalten lohnt sich nicht --> direkt alles vergeben
					#ifdef DEBUG_ALLOC 
						printf(" lohnt sich nicht, vergebe alles!\n\033[0m");
					#endif
					
					ret = buckets[divPos];
					head = (header*)((char*)(buckets[divPos]) - sizeof(size_t));
					head->size -= 1;			//Flag für freies Feld entfernen
					
					tail = ((footer*)(((char*)head) + (head->size - 1)));
					tail->size -= 1;			//Flag für freies Feld entfernen
				}
			} else {
				//Abspalten von Block
				#ifdef DEBUG_ALLOC 
					printf("\033[96m[ALLOC] Abspalten von Feld mit Size %d\n\033[0m", divPos);
				#endif
				
				ret = buckets[divPos];
					
				//Header und Footer von abzugebenden Feld setzen
				head = (header*)((buckets[divPos]) - sizeof(size_t));
				
				// printf("head size pos: %p\n", &head->size);
				
				// printf("Size before change: %ld\n", head->size);
				
				tail = ((footer*)(((char*)head) + size));
				tail->size = size;
				// printf("pos tail front: %p\n", &tail->size);
				// printf("Tail front new: %ld\n", tail->size);
				
				//Nachfolgendes Feld nach vorne schieben!
				buckets[divPos] = head->next;	
				
				// printf("head: %p %ld %p\n", head, size, tail);
				tail = ((footer*)(((char*)head) + (head->size - 1)));
				// printf("tail size back: %p\n", &tail->size);
				tail->size -= (size + 2*sizeof(size_t));
				
				head->size = size;
				
				head = ((header*)(((char*)tail) - (tail->size - 1)));
				head->size = tail->size;
				//printf("head size pos back: %p\n", &head->size);
				// printf("Tail back new: %ld\n", tail->size);
				
				uint8_t toInsert = (((head->size - 1) - 2*sizeof(size_t)) / 8) - 1;
				head->next = buckets[toInsert];
				buckets[toInsert] = (char*)&(head->next);
				#ifdef DEBUG_ALLOC 
					printf("\033[96m[ALLOC] Rest eingeordnet bei Bucket %d (Size: %ld)\n\033[0m", toInsert, head->size - 2*sizeof(size_t) - 1);
				#endif
			}
		}
		
	}
	
	return ret;
	
	// char* ret;
	// if (!data || offset + size > BLOCKSIZE) {
	  // offset = 0;
	  // data = get_block_from_system();
	// }
	// ret = data + offset;
	// offset += size;
	// return ret;
}

void my_free(void* ptr) {
	
}

void initNewBlock() {
	buckets[NUMBER_OF_BUCKETS - 1] = get_block_from_system();
	// printf("Block: %p %d\n", buckets[NUMBER_OF_BUCKETS - 1], BLOCKSIZE); 
	if(!buckets[NUMBER_OF_BUCKETS - 1]) {
		#ifdef DEBUG_INIT 
			puts("\033[92mOUT OF MEMORY\033[0m");
			exit(-1);
		#endif
	}
	
	header *head = ((header*)buckets[NUMBER_OF_BUCKETS - 1]);
	head->size = BLOCKSIZE - 2 * sizeof(size_t) + 1;
	// head->next = NULL;								//Für page gar nicht gebraucht??? Schafft man es, immer nur eine Page zu halten und den Rest korrekt einzuordnen?
	
	// puts("Header erstellt");
	// printf("Head: %ld %p %p\n", head->size, head, (char*)head + (head->size - 1));
	
	//Footer erstellen
	// footer *tail = ((footer*)(buckets[NUMBER_OF_BUCKETS - 1] + head->size - sizeof(size_t));
	footer *tail = ((footer*)(((char*)head) + (head->size - 1)));
	tail->size = head->size;
	// tail->prev = NULL;								//Für page gar nicht gebraucht??? Schafft man es, immer nur eine Page zu halten und den Rest korrekt einzuordnen?
	
	// printf("head size: %p\n", &head->size);
	// printf("tail size: %p\n", &tail->size);
	
	buckets[NUMBER_OF_BUCKETS - 1] += sizeof(size_t);			//Zeiger hinter Größe verschieben
	// printf("Verschoben: %p\n", buckets[NUMBER_OF_BUCKETS - 1]);
}
