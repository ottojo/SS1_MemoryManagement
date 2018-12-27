#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "my_alloc.h"
#include "my_system.h"

#define NUMBER_OF_BUCKETS 33
#define MIN_PAGE_DIFF 24

// #define DEBUG_INIT
// #define DEBUG_PAGE_INIT
// #define DEBUG_ALLOC
// #define DEBUG_BLOCK 150
// #define DEBUG_GETBLOCK
// #define DEBUG_FREE
// #define DEBUG_BUCKETS
// #define DEBUG_SPECIFIED 50000
#define COLOURED

#ifdef DEBUG_SPECIFIED
	uint64_t actionCounter = 0;
#endif

#ifdef DEBUG_BLOCK
	void* blocks[DEBUG_BLOCK];
	uint8_t blockCounter = 0;
#endif

#define extractHeader(ptr) ((header*)((void*)ptr - sizeof(header))) 

//Notwendig für die Umsetzung des Konzepts einer BucketList
static void** buckets[NUMBER_OF_BUCKETS];

typedef struct header {
	uint32_t tailSize;
	uint32_t headSize;
} header;

//Verwendete Hilfsmethoden
void** initNewBlock();
void removeFreeSpaceFromList(void **ptr);

#ifdef DEBUG_BLOCK
	void printBlocks();
#endif

#ifdef DEBUG_BUCKETS
	void printBuckets();
	void printBucketState(uint64_t state);
#endif


/* 
	Erster Versuch einer Speicherverwaltung, verwendete Konzepte: Header und Footer sowie Binning 
	(vgl. http://www.cs.princeton.edu/courses/archive/spr09/cos217/lectures/19DynamicMemory2.pdf)
	
	Konvention: Ist die Größenangabe in Header oder Footer (bzw. dem zusammengelegten Part) ungerade (LSB == 1),
				so ist das Feld frei. Die "wirkliche" Größe muss dann durch Subtraktion dieser 1 errechnet werden.
				Insbesondere sind also gerade bzw. durch 8 teilbare Größenangaben belegte Felder.
				
	Die Implementierung deckt sich vom Ansatz her mit dem des Branches "doubly-linked list" von ottojo,
	implementiert das ganze aber etwas anders. 
				
	TODO:
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
	
	//Alle Zeiger auf NULL setzen, da von der Methode get_block_from_system() nicht gewährleistet
	//wird, dass keine alten Daten mehr vorhanden sind --> da meist aber dennoch nichts in den Pages steht,
	//könnte dies zur Effizienzoptimierung eventuell vernachlässigt werden
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
	
	#ifdef DEBUG_SPECIFIED
		printf("Action: %ld\n", actionCounter++);
	#endif
	
	void *ret = NULL;
	uint8_t pos = (size >> 3) - 1;
	
	#ifdef DEBUG_BUCKETS
		printBuckets();
	#endif
	
	#ifdef DEBUG_BLOCK
		printBlocks();
	#endif
	
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
		
		header *head = extractHeader(ret);
		head->headSize &= 0xFFFFFFFE;
		((header*)(ret + head->headSize))->tailSize &= 0xFFFFFFFE;
		
		void *nextElem = *((void**)ret);
		
		if(nextElem) {
			if(pos > 0) {
				//>8 Byte Datensegment --> doppelt verkettete Liste
				//--> prev Zeiger im nächsten Element muss auf null gesetzt werden
				//Beim 8 Byte Datensegment entfällt dies, da die Liste hier mangels Platz nur einfach verkettet ist
				header *nextElemHeader = extractHeader(nextElem);
				*((void**)(((void*)nextElemHeader) + (nextElemHeader->headSize & 0xFFFFFFFE))) = NULL;
			}
		}
		
		//Feld aus Liste entfernen
		buckets[pos] = (void**)(nextElem);
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
			
			ret = (void*)buckets[divPos];
			header *head = extractHeader(ret);
			header *footer = extractHeader((void*)head + (head->headSize & 0xFFFFFFFE) + 2*sizeof(header));
			
			//Mittelheader einbauen
			header *middleHeader = (header*)(ret + size);
			middleHeader->tailSize = (uint32_t)size;
			middleHeader->headSize = (uint32_t)(head->headSize - size - sizeof(header));
			
			//Front- und Tail-Header Felder anpassen
			head->headSize = size;
			footer->tailSize = middleHeader->headSize;
			
			void *restElem = ((void*)middleHeader + sizeof(header));
			
			//Next Zeiger im Rest des Blocks auf NULL setzen
			*((void**)restElem) = NULL;
			
			//Prev Zeiger setzen (sicher NULL, da Präferenzabspaltung wegen Fehlen eines passenden Feldes)
			*(void**)((void*)footer - sizeof(void*)) = NULL;
			
			//Restfeld im Bucket ablegen
			buckets[pos] = (void**)(restElem);
			
			//Im Bucket des gespaltenen Feldes aufs nächste Feld wechseln und dessen prev Zeiger entsprechend NULL setzen
			void *nextElem = *((void***)ret);
			buckets[divPos] = nextElem;
			if(nextElem) {
				header *nextElemHeader = extractHeader(nextElem);
				*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = NULL;
			}
			
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
			
			divPos = pos + 2;
			while (divPos < NUMBER_OF_BUCKETS - 1 && !buckets[divPos]) {
				++divPos;
			}
			
			if(divPos < NUMBER_OF_BUCKETS - 1) {
				//Abspaltung von größerem Block
				#ifdef DEBUG_ALLOC 
					#ifdef COLOURED
						printf("\033[96m[ALLOC] Spalte von groesserem Block [%d] ab\n\033[0m", divPos);
					#else 
						printf("[ALLOC] Spalte von groesserem Block ab\n");
					#endif
				#endif
				
				ret = (void*)buckets[divPos];
				header *head = extractHeader(ret);
				header *footer = (header*)(ret + (head->headSize & 0xFFFFFFFE));
				
				//Mittelheader einbauen
				header *middleHeader = (header*)(ret + size);

				middleHeader->tailSize = (uint32_t)size;
				middleHeader->headSize = (uint32_t)(head->headSize - size - sizeof(header));

				//Front- und Tail-Header Felder anpassen
				head->headSize = size;
				footer->tailSize = middleHeader->headSize;

				//Prev Zeiger im Rest des Blocks auf NULL setzen
				*((void**)((void*)footer - sizeof(void*))) = NULL;

				uint8_t insertPos = ((middleHeader->headSize - 1) >> 3) - 1;
		
				//Next-Zeiger setzen
				void *nextElem = buckets[insertPos];
				*((void**)((void*)middleHeader + sizeof(header))) = nextElem;
	
				//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
				if(nextElem) {
					if(insertPos != 0) {
						header *headNext = extractHeader(nextElem); 
						*((void**)((void*)headNext + (headNext->headSize & 0xFFFFFFFE))) = ((void*)(middleHeader) + sizeof(header));
					}
				}
				
				buckets[insertPos] = (void**)((void*)(middleHeader) + sizeof(header));
				
				//Im Bucket des gespaltenen Feldes aufs nächste Feld wechseln und dessen prev Zeiger entsprechend NULL setzen
				void *nextSplittedElem = *((void***)ret);
				buckets[divPos] = nextSplittedElem;
				if(nextSplittedElem) {		// &&divPos != 0 -->immer wahr
					header *nextElemHeader = extractHeader(nextSplittedElem);
					*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = NULL;
				}
				
				#ifdef DEBUG_ALLOC 
					#ifdef COLOURED
						printf("\033[96m[ALLOC] Element-Rest eingeordnet bei %d (Size: %d)\n\033[0m", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
					#else 
						printf("[ALLOC] Element-Rest eingeordnet bei %d (Size: %d)\n", ((middleHeader->headSize - 1) >> 3) - 1, middleHeader->headSize - 1);
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
				
				//Auslesen der Restgröße der Page / des > 256 Byte Blocks
				header *head = extractHeader((void*)buckets[NUMBER_OF_BUCKETS - 1]);
				header *footer = (header*)((void*)head + (head->headSize & 0xFFFFFFFE) + sizeof(header));
				
				if(head->headSize >= size + MIN_PAGE_DIFF) {
					//Abspalten lohnt sich
					#ifdef DEBUG_ALLOC 
						#ifdef COLOURED
							printf("\033[96m\t\t--> Abspalten lohnt sich!\n\033[0m");
						#else 
							printf("\t\t--> Abspalten lohnt sich!\n");
						#endif
					#endif
					
					ret = (void*)buckets[NUMBER_OF_BUCKETS - 1];
					void *nextElem = *(buckets[NUMBER_OF_BUCKETS - 1]);

					//Mittelheader einbauen
					header *middleHeader = (header*)((void*)ret + size);
					
					middleHeader->tailSize = (uint32_t)size;
					middleHeader->headSize = (uint32_t)(head->headSize - size - sizeof(header));
					
					//Front- und Tail-Header Felder anpassen
					head->headSize = size;
					footer->tailSize = middleHeader->headSize;
					
					void **restElem = (void**)((void*)middleHeader + sizeof(header));
					
					if(middleHeader->headSize <= 256) {
						uint8_t insertPos = ((middleHeader->headSize - 1) >> 3) - 1;
						//Next-Zeiger setzen
						*restElem = buckets[insertPos];
						//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
						if(insertPos != 0 && buckets[insertPos]) {
							header *headNext = extractHeader(buckets[insertPos]);		
							*((void**)((void*)headNext + (headNext->headSize & 0xFFFFFFFE))) = (void*)restElem;
						} 
						
						buckets[insertPos] = restElem;
						#ifdef DEBUG_ALLOC 
							#ifdef COLOURED
								printf("\033[96m[ALLOC] Page-Rest eingeordnet bei %d (Size: %d)\n\033[0m", insertPos, middleHeader->headSize & 0xFFFFFFFE);
								
							#else 
								printf("[ALLOC] Page-Rest eingeordnet bei %d (Size: %d)\n", insertPos, middleHeader->headSize & 0xFFFFFFFE);
							#endif
						#endif
						
						//Im Bucket des gespaltenen Feldes aufs nächste Feld wechseln und dessen prev Zeiger entsprechend NULL setzen
						buckets[NUMBER_OF_BUCKETS - 1] = nextElem;
						if(nextElem) {
							header *nextElemHeader = extractHeader(nextElem);
							*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = NULL;
						}	
					} else {
						//Next-Zeiger mitnehmen um Liste konsistent zu halten
						*restElem = nextElem;
						
						//Prev-Zeiger des nachfolgenden Listenelements entsprechend verschieben
						if(nextElem) {
							header *nextElemHeader = extractHeader(nextElem);
							*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = (void*)restElem;
						}
						
						//Neuen Listenanfang setzen
						buckets[NUMBER_OF_BUCKETS - 1] = restElem;
						
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
					head->headSize &= 0xFFFFFFFE;
					footer->tailSize &= 0xFFFFFFFE;
					
					//Auf nächstes Element wechseln und prev Pointer von diesem entsprechend NULL setzen 
					void *nextElem = *(buckets[NUMBER_OF_BUCKETS - 1]);
					buckets[NUMBER_OF_BUCKETS - 1] = nextElem;
					if(nextElem) {
						header *nextElemHeader = extractHeader(nextElem);
						*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = NULL;
					}
				}
			}
		}
	}
	
	#ifdef DEBUG_SPECIFIED
		if(actionCounter > DEBUG_SPECIFIED) {
			#ifdef DEBUG_BLOCK
				printBlocks();
			#endif
			
			#ifdef DEBUG_BUCKETS
				printBuckets();
			#endif
		}
	#endif
	return ret;
}

void my_free(void* ptr) {	
	#ifdef DEBUG_SPECIFIED
		printf("Action: %ld\n", actionCounter++);
	#endif

	header *head = extractHeader(ptr);
	header *footer = (header*)((void*)head + (head->headSize & 0xFFFFFFFE) + sizeof(header));
	uint32_t freeSize = head->headSize + 1;
	
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Freeing %p [Size: %d]\n\033[0m", ptr, freeSize & 0xFFFFFFFE); 
	#endif
	
	header *newHeader = head;
	header *newFooter = footer;
	
	//Überprüfe, ob vor dem freizugebenden Feld ebenfalls ein freies Feld liegt
	if(head->tailSize & 1) {
		//Vorderes Feld ist frei
		uint32_t realSize = head->tailSize & 0xFFFFFFFE;
		uint8_t bucketPos = (realSize > 257) ? NUMBER_OF_BUCKETS - 1 : (realSize >> 3) - 1;
		
		#ifdef DEBUG_FREE
			printf("\033[32m[FREE] Vorderes Feld frei [Size: %d, Pos: %d]\n\033[0m", realSize, bucketPos);
		#endif
		
		freeSize += realSize + sizeof(header);
		
		newHeader = (header*)((void*)head - realSize - sizeof(header));
		
		//Element aus Liste entfernen
		if(head->tailSize == 9) {
			void** ptrToDel = (void*)newHeader + sizeof(header);
			void **elem = buckets[0];
			void **prevElem = NULL;
			
			#ifdef DEBUG_FREE
				printf("\033[32m[FREE] Entferne %p aus einfach verketteter Liste!\033[0m\n", ptrToDel);
			#endif
			
			while((void*)elem != ptrToDel) {
				prevElem = elem;
				elem = (void**)(*elem);
			}
			
			//Element aus Liste aushängen
			if(prevElem == NULL) {
				//Element steht ganz vorne in der Liste
				buckets[0] = (void**)*elem;
			} else {
				*prevElem = *elem;
			}
			
		} else {
			#ifdef DEBUG_FREE
				printf("\033[32m[FREE] Entferne %p aus doppelt verketteter Liste!\033[0m\n", (void*)newHeader + sizeof(header));
			#endif
			
			void*** prevPtr = (void***)((void*)head - sizeof(void*));
			void*** nextPtr = (void***)((void*)newHeader + sizeof(header));
			
			//Next Pointer des in der Liste vorher eingeordneten Feldes umbiegen
			if(prevPtr && *prevPtr) {
				**prevPtr = (void*)*nextPtr;
			} else {
				//Element steht am Anfang der Liste
				buckets[bucketPos] = *nextPtr;
			}
			
			//Prev Pointer des in der Liste dahinter eingeordneten Feldes umbiegen	
			if(nextPtr && *nextPtr) {
				header *nextElemHeader = (header*)((void*)(*nextPtr) - sizeof(header));
				*(void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE)) = (void*)(*prevPtr);
			} //Else: Element steht am Ende der Liste --> nichts zu tun
		}
	}
	
	//Überprüfe, ob hinter dem freizugebenden Feld ebenfalls ein freies Feld liegt
	if(footer->headSize & 1) {
		//Hinteres Feld ist frei
		uint32_t realSize = footer->headSize & 0xFFFFFFFE;
		uint8_t bucketPos = (realSize > 257) ? NUMBER_OF_BUCKETS - 1 : (realSize >> 3) - 1;
		
		#ifdef DEBUG_FREE
			printf("\033[32m[FREE] Hinteres Feld frei [Size: %d, Pos: %d]\n\033[0m", realSize, bucketPos);
		#endif
		
		freeSize += realSize + sizeof(header);
		
		newFooter = (header*)((void*)footer + + sizeof(header) + realSize);
		
		//Element aus Liste entfernen
		if(footer->headSize == 9) {
			void** ptrToDel = (void*)footer + sizeof(header);
			void **elem = buckets[0];
			void **prevElem = NULL;
			
			#ifdef DEBUG_FREE
				printf("\033[32m[FREE] Entferne %p aus einfach verketteter Liste!\033[0m\n", (void*)footer + sizeof(header));
			#endif
			
			while((void*)elem != ptrToDel) {
				prevElem = elem;
				elem = (void**)(*elem);
			}
			
			//Element aus Liste aushängen
			if(prevElem == NULL) {
				//Element steht ganz vorne in der Liste
				buckets[0] = (void**)*elem;
			} else {
				*prevElem = *elem;
			}
			
		} else {
			#ifdef DEBUG_FREE
				printf("\033[32m[FREE] Entferne %p aus doppelt verketteter Liste!\033[0m\n", (void*)footer + sizeof(header));
			#endif
			
			void*** prevPtr = (void***)((void*)newFooter - sizeof(void*));
			void*** nextPtr = (void***)((void*)footer + sizeof(header));
			
			//Next Pointer des in der Liste vorher eingeordneten Feldes umbiegen
			if(prevPtr && *prevPtr) {
				**prevPtr = (void*)*nextPtr;
			} else {
				//Element steht am Anfang der Liste
				buckets[bucketPos] = *nextPtr;
			}
			
			//Prev Pointer des in der Liste dahinter eingeordneten Feldes umbiegen
			if(nextPtr && *nextPtr) {
				header *nextElemHeader = (header*)((void*)(*nextPtr) - sizeof(header));
				*((void**)((void*)nextElemHeader + (nextElemHeader->headSize & 0xFFFFFFFE))) = (void*)(*prevPtr);
			} //Else: Element steht am Ende der Liste --> nichts zu tun
		}
	}
	
	newHeader->headSize = freeSize;
	newFooter->tailSize = freeSize;
	
	uint8_t insertPos = (freeSize > 257) ? NUMBER_OF_BUCKETS - 1 : ((freeSize - 1) >> 3) - 1;
	
	void *nextElem = buckets[insertPos];
	
	void **elemPtr = (void**)((void*)newHeader + sizeof(header)); 

	//Next-Zeiger setzen
	*elemPtr = nextElem;
	
	//Prev-Zeiger auf NULL setzen (falls Elementgröße >8 Bytes)
	if(insertPos > 0) {
		*((void**)((void*)newFooter - sizeof(void*))) = NULL;

		//Falls nachfolgendes Element vorhanden (und länger als 1 Byte) prev Zeiger setzen
		if(nextElem) {
			header *headNext = extractHeader(nextElem); 
			*((void**)((void*)headNext + (headNext->headSize & 0xFFFFFFFE))) = ((void*)(newHeader) + sizeof(header));
		}
	}
	
	buckets[insertPos] = elemPtr;
	
	#ifdef DEBUG_FREE 
		printf("\033[32m[FREE] Element eingeordnet bei %d (Size: %d)\n\033[0m", insertPos, freeSize - 1);
	#endif
	
	#ifdef DEBUG_SPECIFIED
		if(actionCounter > DEBUG_SPECIFIED) {
			#ifdef DEBUG_BLOCK
				printBlocks();
			#endif
			
			#ifdef DEBUG_BUCKETS
				printBuckets();
			#endif
		}
	#endif
}

/**
Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer).
Die Page erhält keine next oder prev Zeiger, da immer nur eine einzige Page verwaltet werden soll.
*/
void** initNewBlock() {
	void** ret = (void**)get_block_from_system();
	
	#ifdef DEBUG_BLOCK
		if(blockCounter < DEBUG_BLOCK) {
		blocks[blockCounter++] = ret;
		printf("Added %d. page to block array\n", blockCounter);
	} else {
		printf("\033[90m[ERROR] BLOCK ARRAY OUT OF SPACE \033[0m\n");
	}
	
	#endif	
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Block: %p (Size: %d)\n\033[0m", ret, BLOCKSIZE); 
	#endif
	
	if(!ret) {
		puts("\033[92m[ERROR] --> initNewBlock: OUT OF MEMORY\033[0m");
		exit(-1);
	}
	
	//Header an den Anfang der Page setzen
	header *head = (header*)ret;
	head->tailSize = 0;
	head->headSize = (BLOCKSIZE - 2*sizeof(header)) | 1;	//+1 als Flag für freies Feld
	
	//"Footer" (header verwendet als Footer) an das Ende der Page setzen
	header *footer = ((header*)((void*)ret + BLOCKSIZE - sizeof(header))); //+1 als Flag für freies Feld
	footer->tailSize = head->headSize;
	footer->headSize = 0;	
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Header und Footer erstellt (Footer-pos: %p, Nutzgroesse: %d)\n\033[0m", head, footer->tailSize - 1); 
	#endif
	
	ret ++;			//Zeiger hinter Header-Paket verschieben (--> verschieben um 8 Byte (entspricht Pointergröße))
	
	//Eventuell überflüssig
	*(void**)ret = NULL;					//next-Pointer auf NULL setzen
	*(void**)(footer - sizeof(void*)) = NULL; 	//prev-Pointer auf NULL setzen
	
	#ifdef DEBUG_PAGE_INIT
		printf("\033[90m[PAGE INIT] Zeiger verschoben: %p\n\033[0m", ret); 
	#endif
	
	#ifdef DEBUG_BLOCK
		printBlocks();
	#endif
	
	return ret;
}

#ifdef DEBUG_BLOCK
void printBlocks() {
	uint32_t length = BLOCKSIZE / sizeof(uint32_t);
	
	//printf("Start: %p\t length: %d\n", ptr, length);
	printf("############## BLOCK MAP ######################\n");
	
	for(uint8_t z = 0; z < blockCounter; z++) {
		printf("Block %d:\n", z);
		uint32_t *ptr = (uint32_t*)blocks[z];
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
void printBucketState(uint64_t state) {
	for(int8_t i = 1; i <= 40; i++) {
		printf("%ld", (state >> (40 - i)) & 1);
		if(i % 8 == 0) {printf(" ");} 
	}
	
	// printf("\t%#x\n", state);
	printf("\n");
}

void printBuckets() {
	// printf("\nBucketState: "); 
	// printBucketState(bucketState);
	for(uint8_t i = 0; i < NUMBER_OF_BUCKETS; i++) {
		printf("\n[%2d]: ", i);
		
		void **ptr = buckets[i];
		uint32_t counter = 0;
		while(ptr && counter < 100) {
			printf("%p --> ", ptr);
			fflush(stdout);
			if(*ptr) {
				ptr = *ptr;
				counter ++;
			} else {
				printf("NULL");
				fflush(stdout);
				break;
			}
		}
	}
	printf("\n");
}
#endif