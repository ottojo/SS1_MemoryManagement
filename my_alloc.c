#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 33
#define MIN_PAGE_DIFF 24

//Notwendig für die Umsetzung des Konzepts des Binnings
static void* buckets[NUMBER_OF_BUCKETS];

// #define DEBUG_INIT
// #define DEBUG_PAGE_INIT
#define DEBUG_ALLOC
#define DEBUG_EXTRACT
#define DEBUG_BLOCK
// #define DEBUG_GETBLOCK
#define DEBUG_FREE

#ifdef DEBUG_BLOCK
	void* block;
#endif

typedef struct header {
	uint32_t tailSize;
	uint32_t headSize;
} header;

//Bessere Namensvorschläge dringend erwünscht!
typedef struct combinedHeaders {
	header *frontHeader;
	header *tailHeader;
	void *next;
	void *prev;
} combinedHeaders;

//Verwendete Hilfsmethoden
void* initNewBlock();
combinedHeaders extractHeaderData(void*);				//Extrahiert Header-Daten (auch RestHeader von umliegenden Elementen)
#ifdef DEBUG_BLOCK
	void printBlock(uint32_t*, uint32_t);
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
		printf("\033[95m[INIT] Initialisiere.\n\033[0m");
	#endif
	
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS - 1; i++) {
		buckets[i] = NULL;
	}
	
	buckets[NUMBER_OF_BUCKETS - 1] = initNewBlock();
	
	#ifdef DEBUG_INIT
		printf("\033[95m[INIT] Initialisierung beendet.\n\033[0m");
	#endif
}

void* my_alloc(size_t size) {
	#ifdef DEBUG_ALLOC 
		printf("\033[96m\n[ALLOC] Allokiere: %ld\n\033[0m", size);
	#endif
	
	void *ret;
	uint8_t pos = (size / 8) - 1;
	
	//Überprüfe, ob im entsprechenden Fach der geforderten Größe noch Speicher vorhanden ist
	if(buckets[pos]) {
		#ifdef DEBUG_ALLOC 
			printf("\033[96m[ALLOC] Feld vorhanden in Bucket %d\n\033[0m", pos);
		#endif
		
		ret = buckets[pos];
		puts("H1");
		//Hier könnte eine Methode eingesetzt werden, welche den prev Zeiger nicht ausliest --> minimaler Effizienzgewinn
		combinedHeaders headers = extractHeaderData(ret);
		puts("H2");
		//Flags für freie Felder entfernen
		headers.frontHeader->headSize -= 1;
		headers.tailHeader->tailSize -= 1;
		puts("H3");
		//Feld aus Liste entfernen
		buckets[pos] = headers.next;
		puts("H4");
		printf("pos: %p\n", headers.next);
		if(pos > 0) {
			//>8 Byte Datensegment --> doppelt verkettete Liste
			//--> prev Zeiger im nächsten Element muss auf null gesetzt werden
			//Beim 8 Byte Datensegment entfällt dies, da die Liste hier mangels Platz nur einfach verkettet ist
			if(headers.next) {
				puts("H5a");
				*((void**)(headers.next)) = NULL;
				puts("H5b");
			} else {
				puts("H6");
			}
		}
		puts("H7");
	} else {
		//Kein freies Feld in gesuchter Größe vorhanden
		#ifdef DEBUG_ALLOC 
			puts("\033[96m[ALLOC] Feld nicht vorhanden\033[0m");
		#endif
		
		//Überprüfe, ob größere Felder vorhanden sind
		//--> präferiere Feld mit 2*pos + 2 (+2 wegen neuem Verwaltungssegment in der Mitte und hinten), dann liegt für 
		// 	  nächste Anfrage ein solches Feld mit der geforderten Größe bereit
		uint8_t divPos = 2*pos + 2;
		if(divPos < NUMBER_OF_BUCKETS - 1 && buckets[divPos] && false) {
			#ifdef DEBUG_ALLOC 
				printf("\033[96m[ALLOC] Praeferenzabspaltung aus bucket %d\n\033[0m", divPos);
			#endif
		} else {
			#ifdef DEBUG_ALLOC 
				printf("\033[96m[ALLOC] Versuche, aus groesserem Feld abzuspalten\n\033[0m");
			#endif
			
			if(false) {
				//Abspaltung von größerem Block
				#ifdef DEBUG_ALLOC 
					printf("\033[96m[ALLOC] Spalte von groesserem Block ab\n\033[0m");
				#endif
			} else {
				//Abspaltung von Page
				#ifdef DEBUG_ALLOC 
					printf("\033[96m[ALLOC] Spalte von Page ab\n\033[0m");
				#endif
				
				//Auslesen der Restgröße der Page
				if(!buckets[NUMBER_OF_BUCKETS - 1]) {
					//Keine Page mehr vorhanden --> neue holen
					#ifdef DEBUG_ALLOC 
						printf("\033[96m[ALLOC] Hole neue page\n\033[0m");
					#endif
					buckets[NUMBER_OF_BUCKETS - 1] = initNewBlock();
				}
				
				combinedHeaders headers = extractHeaderData(buckets[NUMBER_OF_BUCKETS - 1]);
				//printf("restPageSize: %d\n", headers.frontHeader->headSize);
				
				if(headers.frontHeader->headSize >= size + MIN_PAGE_DIFF) {
					//Abspalten lohnt sich
					#ifdef DEBUG_ALLOC 
						printf("\033[96m\t\t--> Abspalten lohnt sich!\n\033[0m");
					#endif
					
					ret = buckets[NUMBER_OF_BUCKETS - 1];
					
					//Mittelheader einbauen
					header *middleHeader = (header*)((buckets[NUMBER_OF_BUCKETS - 1]) + size);
					
					//printf("Middle: %p\n", middleHeader);
					
					middleHeader->tailSize = (uint32_t)size;
					middleHeader->headSize = (uint32_t)(headers.frontHeader->headSize - size - 2*sizeof(uint32_t));
					
					//Front- und Tail-Header Felder anpassen
					headers.frontHeader->headSize = size;
					headers.tailHeader->tailSize = middleHeader->headSize;
					
					if(middleHeader->headSize <= 256) {
						uint8_t insertPos = (((middleHeader->headSize - 1)) / 8) - 1;
						
						//Prev Zeiger im Rest des Blocks auf NULL setzen
						*(void**)((void*)middleHeader + 2*sizeof(uint32_t)) = NULL;
						*(void**)((void*)headers.tailHeader - sizeof(void*)) = buckets[insertPos];
						
						//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
						if(buckets[insertPos] && insertPos != 0) {
							*((void**)buckets[insertPos]) = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						} 
						
						buckets[insertPos] = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						
						#ifdef DEBUG_ALLOC 
							printf("\033[96m[ALLOC] Page-Rest eingeordnet bei %d (Size: %d)\n\033[0m", ((middleHeader->headSize - 1) / 8) - 1, middleHeader->headSize - 1);
						#endif
						
						buckets[NUMBER_OF_BUCKETS - 1] = NULL;
					} else {
						buckets[NUMBER_OF_BUCKETS - 1] = ((void*)(middleHeader) + 2*sizeof(uint32_t));
						#ifdef DEBUG_ALLOC 
							printf("\033[96m[ALLOC] Page-Rest nicht neu eingeordnet!\n\033[0m");
						#endif
					}
				} else {
					//Abspalten lohnt sich nicht --> direkt alles vergeben
					#ifdef DEBUG_ALLOC 
						printf("\033[96m\t\t--> lohnt sich nicht, vergebe alles!\n\033[0m");
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
	
	return ret;
}

void my_free(void* ptr) {
	//Triviale Implementierung, lediglich einhängen in Liste
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Freeing %p\n\033[0m", ptr); 
	#endif
	
	combinedHeaders headers = extractHeaderData(ptr);

	// uint8_t insertPos = (((headers.frontHeader->headSize)) / 8) - 1;
	uint8_t insertPos = (((headers.frontHeader->headSize)) >> 3) - 1;
	
	//Feld als frei markieren
	headers.frontHeader->headSize += 1;
	headers.tailHeader->tailSize += 1;
	
	//Prev Zeiger einzufügenden Feld auf NULL setzen
	*((void**)ptr) = NULL;
	
	//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
	if(buckets[insertPos] && insertPos != 0) {
		*((void**)buckets[insertPos]) = ptr;
	}
	
	headers.next = buckets[insertPos];
	buckets[insertPos] = ptr;
	
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Element eingeordnet bei %d (Size: %d)\n\033[0m", insertPos, headers.frontHeader->headSize - 1);
	#endif
}

/**
Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer).
Die Page erhält keine next oder prev Zeiger, da immer nur eine einzige Page verwaltet werden soll.
*/
void* initNewBlock() {
	void* ret = get_block_from_system();
	
	#ifdef DEBUG_BLOCK
		block = ret;	
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
	head = ((header*)(ret + BLOCKSIZE - 2*sizeof(uint32_t))); //+1 als Flag für freies Feld
	head->tailSize = BLOCKSIZE - 4*sizeof(uint32_t) + 1;
	head->headSize = 0;
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Header und Footer erstellt (Footer-pos: %p, Nutzgroesse: %d)\n\033[0m", head, head->tailSize - 1); 
	#endif
	
	ret += 2*sizeof(uint32_t);			//Zeiger hinter Header-Paket verschieben
	
	//Eventuell überflüssig
	*(void**)ret = 0;
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Zeiger verschoben: %p\n\033[0m", ret); 
	#endif
	
	return ret;
}

/**
Methode zum Extrahieren der beiden Header um das Datenpaket herum. Ausgelesen werden sollen dabei eigentlich auch die
Zeiger next und prev, aber dies funktioniert aktuell noch nicht. 
*/
combinedHeaders extractHeaderData(void *ptr) {
	#ifdef DEBUG_BLOCK
		printBlock((uint32_t*)block, BLOCKSIZE);
	#endif

	combinedHeaders headers;
	
	headers.frontHeader = (header*)(ptr - 2*sizeof(uint32_t));
	
	if((headers.frontHeader->headSize & 0x01)) {
		//Feld ist frei
		headers.tailHeader = (header*)(ptr + (headers.frontHeader->headSize & 0xfffe));
		
		//Nur falls nicht von der Page gelesen wird (da immer nur eine Page gehalten wird sind die Pointer hier immer NULL)
		if(headers.frontHeader->headSize >= 256) {
			headers.next = *((void**)headers.tailHeader - sizeof(void*));
			headers.prev = *(void**)ptr;
		} else {			//Ist dieses else überhaupt notwendig???
			headers.next = NULL;
			headers.prev = NULL;
		}
	} else {
		headers.tailHeader = (header*)(ptr + (headers.frontHeader->headSize));
	}
	
	#ifdef DEBUG_EXTRACT
		printf("\033[34m[EXTRACT] front: %d | %d\t back: %d | %d\n\033[0m\tprev: %p | next: %p\n", headers.frontHeader->tailSize, headers.frontHeader->headSize, headers.tailHeader->tailSize, headers.tailHeader->headSize, headers.prev, headers.next);
	#endif
	
	return headers;
}

#ifdef DEBUG_BLOCK
void printBlock(uint32_t *ptr, uint32_t length) {
	length /= sizeof(uint32_t);
	
	//printf("Start: %p\t length: %d\n", ptr, length);
	printf("############## BLOCK MAP ######################");
	
	// Primitiv
	// for(uint32_t i = 0; i < length; i++, ptr++) {
		// if(i % 16 == 0) {
			// printf("\n%5d: ", i);
		// }
		
		// if(!(*ptr & 0xFFFF0000) && *ptr != 0) { 
			// printf("\033[32m%011d \033[0m", *ptr);
		// } else {
			// printf("%011d ", *ptr);
		// }
	// }
	
	uint32_t firstPointer = 9000, secondPointer = 9000;
	bool used = false;
	printf("\n    0: \033[32m%07d \033[0m", *ptr++);
	for(uint32_t i = 1; i < length - 1; i++, ptr++) {
		if(i % 28 == 0) {
			printf("\n%5d: ", i);
		}
		
		if(i == firstPointer || i == secondPointer) {
			printf("\033[36m%15p \033[0m", (void*)ptr);
			i ++;
			ptr++;
			if(i % 28 == 0) {
				printf("\n%5d: ", i);
			}
		} else {
			if(!(*ptr & 0xFFFF0000) && *ptr != 0) { 
				printf("\033[32m%07d \033[0m", *ptr);
				used = !(*ptr & 0x01);
				if(((uintptr_t)ptr & 7) && !used) {
					firstPointer = i + 1;
					secondPointer = i + *ptr/sizeof(uint32_t) - 1;
				}
			} else {
				// printf("%07d ", *ptr);
				used ? printf("||||||| ") : printf("------- ");
			}
		}
	}
	printf("\033[32m%07d\033[0m \n", *ptr);
	
	// uint32_t headSize;
	
	// printf("\n\n%5d: %07d ", 0, *ptr++);
	// for(uint32_t i = 1; i < length; i++) {
		// headSize = *ptr;
		// printf("\033[32m%07d \033[0m", headSize);
		// i++;
		// ptr++;
		
		// if(i % 26 == 0) {
			// printf("\n%5d: ", i);
		// }
		
		// //Nächste zwei Felder als Pointer interpretieren --> geht davon aus, dass die Header Struktur passt
		// printf("%p  ", (void*)ptr);
		
		// for(uint8_t a = 1; a < 3; a++) {
			// if(*(ptr + a) == headSize) {
				// printf("%07d ", *ptr);
				// break;
			// }
		// }
		
		
		// ptr += 2; i+= 2;
		// while(*(ptr + 2) != headSize && i < length) {
			// printf("%07d %07d ", *ptr, *(ptr + 1));
			// ptr += 2;
			// i += 2;
			
			// if(i % 26 == 0) {
				// printf("\n%5d: ", i);
			// }
		// }
		
		// printf("%p  ", (void*)ptr);
		// ptr++;
		// i++;
		
		// if(i % 26 == 0) {
			// printf("\n%5d: ", i);
		// }
	// }
	
	
	
	printf("\n############## BLOCK MAP ######################\n");
}
#endif