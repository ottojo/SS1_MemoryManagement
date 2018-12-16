#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 33
#define MIN_PAGE_DIFF 24

//Notwendig für die Umsetzung des Konzepts des Binnings
static void** buckets[NUMBER_OF_BUCKETS];

// #define DEBUG_INIT
// #define DEBUG_PAGE_INIT
// #define DEBUG_ALLOC
// #define DEBUG_EXTRACT
// #define DEBUG_BLOCK
// #define DEBUG_GETBLOCK
// #define DEBUG_FREE
// #define DEBUG_BUCKETS
#define COLOURED

#ifdef DEBUG_BLOCK
	void* block[100];
	uint8_t blockCounter = 0;
#endif

typedef struct header {
	uint32_t tailSize;
	uint32_t headSize;
} header;

//Bessere Namensvorschläge dringend erwünscht!
typedef struct combinedHeaders {
	header *frontHeader;
	header *tailHeader;
	void **next;
	void **prev;
} combinedHeaders;

//Verwendete Hilfsmethoden
void** initNewBlock();
combinedHeaders extractHeaderData(void**);				//Extrahiert Header-Daten (auch RestHeader von umliegenden Elementen)
#ifdef DEBUG_BLOCK
	void printBlocks();
#endif

#ifdef DEBUG_BUCKETS
	void printBuckets();
#endif


/* 
	Erster Versuch einer Speicherverwaltung, verwendete Konzepte: Header und Footer sowie Binning 
	(vgl. http://www.cs.princeton.edu/courses/archive/spr09/cos217/lectures/19DynamicMemory2.pdf)
	
	Konvention: Ist die Größenangabe in Header oder Footer (bzw. dem zusammengelegten Part) ungerade (LSB == 1),
				so ist das Feld frei. Die "wirkliche" Größe muss dann durch Subtraktion dieser 1 errechnet werden.
				Insbesondere sind also gerade bzw. durch 8 teilbare Größenangaben belegte Felder.
				
	TODO:
	- free implementieren
	- Abspaltung von größeren Blöcken implementieren
	- Effizienzoptimierung, insbesondere in Bezug auf die Geschwindigkeit
*/

void init_my_alloc() {
	#ifdef DEBUG_INIT
		#ifdef COLOURED
			printf("\033[95m[INIT] Initialisiere.\n\033[0m");
		#else 
			printf("[INIT] Initialisiere.\n");
		#endif
	#endif
	
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS - 1; i++) {
		buckets[i] = NULL;
	}
	
	buckets[NUMBER_OF_BUCKETS - 1] = initNewBlock();
	
	#ifdef DEBUG_INIT
		#ifdef COLOURED
			printf("\033[95m[INIT] Initialisierung beendet.\n\033[0m");
		#else 
			printf("[INIT] Initialisierung beendet.\n");
		#endif
	#endif
}

void* my_alloc(size_t size) {
	#ifdef DEBUG_ALLOC 
		#ifdef COLOURED
			printf("\033[96m\n[ALLOC] Allokiere: %ld\n\033[0m", size);
		#else 
			printf("\n[ALLOC] Allokiere: %ld\n", size);
		#endif
	#endif
	
	#ifdef DEBUG_BUCKETS
		printBuckets();
	#endif
	
	void *ret = NULL;
	uint8_t pos = (size >> 3) - 1;
	
	//Überprüfe, ob im entsprechenden Fach der geforderten Größe noch Speicher vorhanden ist
	if(buckets[pos]) {
		#ifdef DEBUG_ALLOC 
			#ifdef COLOURED
				printf("\033[96m[ALLOC] Feld vorhanden in Bucket %d\n\033[0m", pos);
			#else 
				printf("[ALLOC] Feld vorhanden in Bucket %d\n", pos);
			#endif
		#endif

		ret = (void*)buckets[pos];
		//Hier könnte eine Methode eingesetzt werden, welche den prev Zeiger nicht ausliest --> minimaler Effizienzgewinn
		combinedHeaders headers = extractHeaderData((void**)ret);
		//Flags für freie Felder entfernen
		headers.frontHeader->headSize -= 1;
		headers.tailHeader->tailSize -= 1;

		if(pos > 0) {
			//>8 Byte Datensegment --> doppelt verkettete Liste
			//--> prev Zeiger im nächsten Element muss auf null gesetzt werden
			//Beim 8 Byte Datensegment entfällt dies, da die Liste hier mangels Platz nur einfach verkettet ist
			if(headers.next && *headers.next) {
				*((void**)headers.next) = NULL;
			}
		}
		
		//Feld aus Liste entfernen		
		buckets[pos] = *headers.next;
	} else {
		//Kein freies Feld in gesuchter Größe vorhanden
		#ifdef DEBUG_ALLOC 
			#ifdef COLOURED
				puts("\033[96m[ALLOC] Feld nicht vorhanden\033[0m");
			#else 
				puts("[ALLOC] Feld nicht vorhanden");
			#endif
		#endif
		
		//Überprüfe, ob größere Felder vorhanden sind
		//--> präferiere Feld mit 2*pos + 2 (+2 wegen neuem Verwaltungssegment in der Mitte und hinten), dann liegt für 
		// 	  nächste Anfrage ein solches Feld mit der geforderten Größe bereit
		uint8_t divPos = 2*pos + 2;
		if(divPos < NUMBER_OF_BUCKETS - 1 && buckets[divPos]) {
			#ifdef DEBUG_ALLOC 
				#ifdef COLOURED
					printf("\033[96m[ALLOC] Praeferenzabspaltung aus bucket %d\n\033[0m", divPos);
				#else 
					printf("[ALLOC] Praeferenzabspaltung aus bucket %d\n", divPos);
				#endif
			#endif
			
			combinedHeaders headers = extractHeaderData(buckets[divPos]);
			
			ret = (void*)buckets[divPos];
					
			//Mittelheader einbauen
			header *middleHeader = (header*)((buckets[divPos]) + (size >> 3));
					
			middleHeader->tailSize = (uint32_t)size;
			middleHeader->headSize = (uint32_t)(headers.frontHeader->headSize - size - 2*sizeof(uint32_t));
					
			//Front- und Tail-Header Felder anpassen
			headers.frontHeader->headSize = size;
			headers.tailHeader->tailSize = middleHeader->headSize;
						
			//Prev Zeiger im Rest des Blocks auf NULL setzen
			*(void**)((void*)middleHeader + 2*sizeof(uint32_t)) = NULL;
			
			//Next Zeiger setzen (sicher NULL, da Präferenzabspaltung wegen Fehlen eines passenden Feldes)
			*(void**)((void*)headers.tailHeader - sizeof(void*)) = NULL;
			
			//Restfeld im Bucket ablegen
			buckets[pos] = (void**)((void*)(middleHeader) + 2*sizeof(uint32_t));
			
			//Im Bucket des gespaltenen Feldes aufs nächste Feld wechseln
			buckets[divPos] = *buckets[divPos];
			
			#ifdef DEBUG_ALLOC 
				#ifdef COLOURED
					printf("\033[96m[ALLOC] Element-Rest eingeordnet bei %d (Size: %d)\n\033[0m", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
				#else 
					printf("[ALLOC] Element-Rest eingeordnet bei %d (Size: %d)\n", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
				#endif
			#endif
		} else {
			#ifdef DEBUG_ALLOC 
				#ifdef COLOURED
					printf("\033[96m[ALLOC] Versuche, aus groesserem Feld abzuspalten\n\033[0m");
				#else 
					printf("[ALLOC] Versuche, aus groesserem Feld abzuspalten\n");
				#endif
			#endif
			
			if(false) {
				//Abspaltung von größerem Block
				#ifdef DEBUG_ALLOC 
					#ifdef COLOURED
						printf("\033[96m[ALLOC] Spalte von groesserem Block ab\n\033[0m");
					#else 
						printf("[ALLOC] Spalte von groesserem Block ab\n");
					#endif
				#endif
			} else {
				//Abspaltung von Page
				#ifdef DEBUG_ALLOC 
					#ifdef COLOURED
						printf("\033[96m[ALLOC] Spalte von Page ab\n\033[0m");
					#else 
						printf("[ALLOC] Spalte von Page ab\n");
					#endif
				#endif
				
				//Auslesen der Restgröße der Page
				if(!buckets[NUMBER_OF_BUCKETS - 1]) {
					//Keine Page mehr vorhanden --> neue holen
					#ifdef DEBUG_ALLOC 
						#ifdef COLOURED
							printf("\033[96m[ALLOC] Hole neue page\n\033[0m");
						#else 
							printf("[ALLOC] Hole neue page\n");
						#endif
					#endif
					buckets[NUMBER_OF_BUCKETS - 1] = initNewBlock();
				}
				
				combinedHeaders headers = extractHeaderData(buckets[NUMBER_OF_BUCKETS - 1]);
				//printf("restPageSize: %d\n", headers.frontHeader->headSize);
				
				if(headers.frontHeader->headSize >= size + MIN_PAGE_DIFF) {
					//Abspalten lohnt sich
					#ifdef DEBUG_ALLOC 
						#ifdef COLOURED
							printf("\033[96m\t\t--> Abspalten lohnt sich!\n\033[0m");
						#else 
							printf("\t\t--> Abspalten lohnt sich!\n");
						#endif
					#endif
					
					ret = (void*)buckets[NUMBER_OF_BUCKETS - 1];
					
					//Mittelheader einbauen
					header *middleHeader = (header*)((buckets[NUMBER_OF_BUCKETS - 1]) + (size >> 3));
					
					//printf("Middle: %p\n", middleHeader);
					
					middleHeader->tailSize = (uint32_t)size;
					middleHeader->headSize = (uint32_t)(headers.frontHeader->headSize - size - 2*sizeof(uint32_t));
					
					//Front- und Tail-Header Felder anpassen
					headers.frontHeader->headSize = size;
					headers.tailHeader->tailSize = middleHeader->headSize;
					
					if(middleHeader->headSize <= 256) {
						uint8_t insertPos = ((middleHeader->headSize - 1) >> 3) - 1;
						
						//Prev Zeiger im Rest des Blocks auf NULL setzen
						*(void**)((void*)middleHeader + 2*sizeof(uint32_t)) = NULL;
						*(void**)((void*)headers.tailHeader - sizeof(void*)) = buckets[insertPos];
						
						//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
						if(buckets[insertPos] && insertPos != 0) {
							*((void**)buckets[insertPos]) = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						} 
						
						buckets[insertPos] = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						
						#ifdef DEBUG_ALLOC 
							#ifdef COLOURED
								printf("\033[96m[ALLOC] Page-Rest eingeordnet bei %d (Size: %d)\n\033[0m", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
							#else 
								printf("[ALLOC] Page-Rest eingeordnet bei %d (Size: %d)\n", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
							#endif
						#endif
						
						buckets[NUMBER_OF_BUCKETS - 1] = NULL;
					} else {
						buckets[NUMBER_OF_BUCKETS - 1] = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						#ifdef DEBUG_ALLOC 
							#ifdef COLOURED
								printf("\033[96m[ALLOC] Page-Rest nicht neu eingeordnet!\n\033[0m");
							#else 
								printf("[ALLOC] Page-Rest nicht neu eingeordnet!\n");
							#endif
						#endif
					}
				} else {
					//Abspalten lohnt sich nicht --> direkt alles vergeben
					#ifdef DEBUG_ALLOC 
						#ifdef COLOURED
							printf("\033[96m\t\t--> lohnt sich nicht, vergebe alles!\n\033[0m");
						#else 
							printf("\t\t--> lohnt sich nicht, vergebe alles!\n");
						#endif
					#endif
					
					ret = buckets[NUMBER_OF_BUCKETS - 1];
					
					//Flags für freies Feld entfernen
					headers.frontHeader->headSize -= 1;
					headers.tailHeader->tailSize -= 1;
					
					buckets[NUMBER_OF_BUCKETS - 1] = NULL;
				}
			}
		}
	}
	
	#ifdef DEBUG_BLOCK
		printBlocks();
	#endif
	
	return ret;
}

void my_free(void* ptr) {	
	//Triviale Implementierung, lediglich einhängen in Liste
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Freeing %p\n\033[0m", ptr); 
	#endif
	
	#ifdef DEBUG_BUCKETS
		printBuckets();
	#endif
	
	combinedHeaders headers = extractHeaderData(ptr);

	// uint8_t insertPos = (((headers.frontHeader->headSize)) >> 3) - 1;
	uint8_t insertPos = (((headers.frontHeader->headSize)) >> 3) - 1;
	
	//Feld als frei markieren
	headers.frontHeader->headSize += 1;
	headers.tailHeader->tailSize += 1;
	
	//Prev Zeiger im einzufügenden Feld auf NULL setzen
	*((void**)ptr) = NULL;
	
	//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
	if(buckets[insertPos]) {
		if(insertPos != 0) {
			*buckets[insertPos] = ptr;
		}
		headers.next = buckets[insertPos];
	} else {
		*headers.next = NULL;
	}
	
	
	// headers.next ? printf("Next set *: %p\n", *headers.next) : printf("Next set: %p\n", headers.next);
	buckets[insertPos] = (void**)ptr;
	
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Element eingeordnet bei %d (Size: %d)\n\033[0m", insertPos, headers.frontHeader->headSize - 1);
	#endif
	
	#ifdef DEBUG_BLOCK
		printBlocks();
	#endif
}

/**
Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer).
Die Page erhält keine next oder prev Zeiger, da immer nur eine einzige Page verwaltet werden soll.
*/
void** initNewBlock() {
	void** ret = (void**)get_block_from_system();
	
	#ifdef DEBUG_BLOCK
		block[blockCounter++] = (void*)ret;	
	#endif	
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Block: %p (Size: %d)\n\033[0m", ret, BLOCKSIZE); 
	#endif
	
	if(!ret) {
		puts("\033[92m[ERROR] --> initNewBlock: OUT OF MEMORY\033[0m");
		exit(-1);
	}
	
	//Header an den Anfang der Page setzen
	header *head = ((header*)ret);
	head->tailSize = 0;
	head->headSize = BLOCKSIZE - 4* sizeof(uint32_t) + 1;	//+1 als Flag für freies Feld
	
	//"Footer" (header verwendet als Footer) an das Ende der Page setzen
	head = ((header*)((void*)ret + BLOCKSIZE - 2*sizeof(uint32_t))); //+1 als Flag für freies Feld
	head->tailSize = BLOCKSIZE - 4*sizeof(uint32_t) + 1;
	head->headSize = 0;	
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Header und Footer erstellt (Footer-pos: %p, Nutzgroesse: %d)\n\033[0m", head, head->tailSize - 1); 
	#endif
	
	ret ++;			//Zeiger hinter Header-Paket verschieben (--> verschieben um 8 Byte (entspricht Pointergröße))
	
	//Eventuell überflüssig
	// *(void**)ret = NULL;
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Zeiger verschoben: %p\n\033[0m", ret); 
	#endif
	
	#ifdef DEBUG_BLOCK
		printBlocks();
	#endif
	
	return ret;
}

/**
Methode zum Extrahieren der beiden Header um das Datenpaket herum. Ausgelesen werden sollen dabei eigentlich auch die
Zeiger next und prev, aber dies funktioniert aktuell noch nicht. 
*/
combinedHeaders extractHeaderData(void **ptr) {
	// #ifdef DEBUG_BLOCK
		// printBlocks();
	// #endif

	combinedHeaders headers;
	
	headers.frontHeader = (header*)((void*)ptr - 2*sizeof(uint32_t));
	
	if((headers.frontHeader->headSize & 0x01)) {
		//Feld ist frei
		headers.tailHeader = (header*)((void*)ptr + (headers.frontHeader->headSize & 0xfffe));
		//Nur falls nicht von der Page gelesen wird (da immer nur eine Page gehalten wird sind die Pointer hier immer NULL)
		if(headers.frontHeader->headSize <= 257) {
			headers.next = (void**)((void*)headers.tailHeader - sizeof(void*));
			headers.prev = ptr;
		} else {			//Ist dieses else überhaupt notwendig???
			headers.next = (void**)NULL;
			headers.prev = (void**)NULL;
		}
	} else {
		headers.tailHeader = (header*)((void*)ptr + headers.frontHeader->headSize);
		headers.prev = ptr;
		// headers.next = (void**)((void*)headers.tailHeader - sizeof(void*));
		headers.next = (void**)((void*)headers.tailHeader - sizeof(void*));
	}
	
	#ifdef DEBUG_EXTRACT
		if(headers.next && headers.prev) {
			printf("\033[34m[EXTRACT] front: %d | %d\t back: %d | %d\t*prev: %p | *next: %p\n\033[0m", headers.frontHeader->tailSize, headers.frontHeader->headSize, headers.tailHeader->tailSize, headers.tailHeader->headSize, *headers.prev, *headers.next);
		} else {
			printf("\033[34m[EXTRACT] front: %d | %d\t back: %d | %d\tprev: %p | next: %p\n\033[0m", headers.frontHeader->tailSize, headers.frontHeader->headSize, headers.tailHeader->tailSize, headers.tailHeader->headSize, NULL, NULL);		
		}
	#endif
	
	return headers;
}

#ifdef DEBUG_BLOCK
void printBlocks() {
	uint32_t length = BLOCKSIZE / sizeof(uint32_t);
	
	//printf("Start: %p\t length: %d\n", ptr, length);
	printf("############## BLOCK MAP ######################\n");
	
	for(uint8_t z = 0; z < blockCounter; z++) {
		printf("Block %d:\n", z);
		uint32_t *ptr = (uint32_t*)block[z];
		uint32_t firstPointer = BLOCKSIZE + 5, secondPointer = BLOCKSIZE + 5;
		bool used = false;
		
		#ifdef COLOURED
			printf("\n    0: \033[32m%09d \033[0m", *ptr++);
		#else 
			printf("\n    0: %09d ", *ptr++);
		#endif
		
		for(uint32_t i = 1; i < length - 1; i++, ptr++) {
			if(i % 20 == 0) {
				printf("\n%5d: ", i);
			}
			
			if(i == firstPointer || i == secondPointer) {
				#ifdef COLOURED
					printf("\033[36m%19p \033[0m", *(void**)ptr);
				#else 
					printf("%19p ", *(void**)ptr);
				#endif
				i ++;
				ptr++;
				if(i % 20 == 0) {
					printf("\n%5d: ", i);
				}
			} else {
				if(!(*ptr & 0xFFFF0000) && *ptr != 0) { 
					#ifdef COLOURED
						printf("\033[32m%09d \033[0m", *ptr);
					#else 
						printf("%09d ", *ptr);
					#endif
					used = !(*ptr & 0x01);
					if(((uintptr_t)ptr & 7) && !used) {
						firstPointer = i + 1;
						secondPointer = i + *ptr/sizeof(uint32_t) - 1;
					}
				} else {
					// printf("%07d ", *ptr);
					used ? printf("||||||||| ") : printf("--------- ");
				}
			}
		}
		#ifdef COLOURED
			printf("\033[32m%09d\033[0m \n\n", *ptr);
		#else 
			printf("%09d \n\n", *ptr);
		#endif
	}
	printf("\n############## BLOCK MAP ######################\n");
}
#endif

#ifdef DEBUG_BUCKETS
void printBuckets() {
	printf("\nBuckets:");
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS; i++) {
		printf("\n[%2d]: ", i);
		
		void **ptr = buckets[i];
		
		while(ptr) {
			printf("%p --> ", ptr);
			if(*ptr) {
				ptr = *ptr;
			} else {
				printf("NULL");
				break;
			}
		}
	}
	printf("\n");
}
#endif