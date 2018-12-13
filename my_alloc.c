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
// #define DEBUG_ALLOC
// #define DEBUG_EXTRACT
#define DEBUG_BLOCK
// #define DEBUG_GETBLOCK
// #define DEBUG_FREE

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
				
				
	Diese Variante sollte eigentlich der von ottojo getesteten Variante entsprechen
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
		
		combinedHeaders headers = extractHeaderData(ret);
		
		//Flags für freie Felder entfernen
		headers.frontHeader->headSize -= 1;
		headers.tailHeader->tailSize -= 1;

		//Feld aus Liste entfernen
		buckets[pos] = headers.next;

		if(pos > 0) {
			//>8 Byte Datensegment --> doppelt verkettete Liste
			//--> prev Zeiger im nächsten Element muss auf null gesetzt werden
			//Beim 8 Byte Datensegment entfällt dies, da die Liste hier mangels Platz nur einfach verkettet ist
			if(headers.next) {
				*((void**)(headers.next)) = NULL;
			}
		}
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
	/*
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
	
	return ret;*/
}

void my_free(void* ptr) {
	/////////
	// WIP //
	/////////
}

/**
Methode zum Vorbereiten einer neuen Page (setzen von richtigem Header und Footer).
Die Page erhält keine next oder prev Zeiger, da immer nur eine einzige verwaltet werden soll.
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
		#ifdef DEBUG_PAGE_INIT
			puts("\033[92m[ERROR] --> initNewBlock: OUT OF MEMORY\033[0m");
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
			headers.next = *(void**)((void*)headers.tailHeader - sizeof(void*));
			headers.prev = *(void**)ptr;
		} else {			//Ist dieses else überhaupt notwendig???
			headers.next = NULL;
			headers.prev = NULL;
		}
	} else {
		headers.tailHeader = (header*)(ptr + (headers.frontHeader->headSize));
	}
	
	#ifdef DEBUG_EXTRACT
		printf("\033[34m[EXTRACT] front: %d | %d\t back: %d | %d\n\033[0m", headers.frontHeader->tailSize, headers.frontHeader->headSize, headers.tailHeader->tailSize, headers.tailHeader->headSize);
	#endif
	
	return headers;
}

#ifdef DEBUG_BLOCK
void printBlock(uint32_t *ptr, uint32_t length) {
	length /= sizeof(uint32_t);
	
	//printf("Start: %p\t length: %d\n", ptr, length);
	printf("############## BLOCK MAP ######################");
	for(uint32_t i = 0; i < length; i++) {
		if(i % 40 == 0) {
			printf("\n%5d: ", i);
		}
		if((*ptr & 0xFFFF0000)) { 
			printf("---- ");
		} else {
			printf("%04d ", *ptr);
		}
		ptr ++;
	}
	printf("\n############## BLOCK MAP ######################\n");
}
#endif

