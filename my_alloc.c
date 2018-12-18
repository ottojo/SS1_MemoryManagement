#include <stdlib.h>
#include "my_alloc.h"
#include "my_system.h"
#include "stdio.h"

#define HEADER_SIZE (8+126+2)
#define BLOCK_SIZE 8192
#define DATA_SIZE (BLOCK_SIZE-HEADER_SIZE)
#define MIN_OBJECT_SIZE 8
#define MAX_OBJECT_SIZE 256

//#define DEBUG
//#define DEBUG_INIT
//#define DEBUG_CONTENT
typedef struct Block {
    struct Block* next;
}Block;


/*List for free Object spaces in block. Does not care about the amount of Blocks*/
/*TODO: 
 * High potential for optimizing here
 * Ideas:
 * -seperated FreeList for each size
 * -Sorting
 */
typedef struct FreeObj{
    struct FreeObj* next;
} FreeObj, Object;

void init_my_alloc(); 
void* my_alloc(size_t size); 
void my_free(void* ptr); 
int updateContents(void* ptr);
Block* getBlock(void*ptr);
int getObjSize(void* ptr);
int initBlock(Block* block);
void printContents(Block* block);

FreeObj* rootFree;
Block* rootBlock;

void init_my_alloc() {
#ifdef DEBUG_INIT
    printf("##########################################################################################################\n");
    printf("Initializing...\n");
#endif
    rootBlock = get_block_from_system();
    if(rootBlock==NULL){
        fprintf(stderr,"System MEM failure!\n");
        exit(0);
    }
    initBlock(rootBlock);
    rootFree = (FreeObj*) ((void*)rootBlock + HEADER_SIZE);
    rootFree->next = NULL;
    updateContents(rootFree);
#ifdef DEBUG_INIT
    printf("Initialization finished!\n");
    printf("rootFree: %p", rootFree);
#endif
}

void* my_alloc(size_t size) {
#ifdef DEBUG
    printf("\n\nallocating mem of size: %ld\n",size);
#endif
    FreeObj* object; 
    object = rootFree;

    /*case: rootFree matches with query*/
    int rootFreeSize = getObjSize(rootFree);
    if(rootFreeSize>=size){
#ifdef DEBUG
        printf("rootFree matches with query!\n");
#endif
        if(rootFreeSize==size){
            if(rootFree->next!=NULL){
                rootFree = rootFree->next;
            }else{
                rootFree = (FreeObj*) ((void*) rootFree + size);
                updateContents(rootFree);
            }
            return object;
        }
        rootFree = (FreeObj*) ((void*)rootFree + size);
        rootFree->next = object->next; //because *object now holds the next element
        updateContents(rootFree);
        return object;
    }
    /*case: any other element does match*/
#ifdef DEBUG
    printf("root does not match with query!\n");
#endif
    while(object->next!=NULL){
        FreeObj* nextfree = object->next;
#ifdef DEBUG
        printf("try next\n");
#endif
        //nextfree is matching
        int nextfreeSize = getObjSize(nextfree);
        if(nextfreeSize>=size){
#ifdef DEBUG
            printf("found one matching\n");
#endif
           if(nextfreeSize==size){
               if(nextfree->next!=NULL){
                   object->next = nextfree->next;
               }else{
                   object->next = (FreeObj*) ((void*) nextfree + size);
                   updateContents(object->next);
               }
               return nextfree;
           }

           /*object here is the new freepointer between nextfree, wich will be returned, and *nextfree*/
           object->next = (FreeObj*) ((void*)nextfree+size);
           object->next->next = nextfree->next;
           updateContents(object->next);
           return nextfree;
        }
        /*else jump to nextfree*/
        object = nextfree;
    }

    /*no matching free Object found -> allocate new block*/
#ifdef DEBUG
    printf("no nextFree matching. Allocating new Block!\n");
#endif
    Block* newBlock = get_block_from_system();
    if(newBlock==NULL){
        fprintf(stderr,"System MEM failure!\n");
        exit(0);
    }
    initBlock(newBlock);

    //inserting newBlock as rootBlock of List
    newBlock->next = rootBlock;
    rootBlock = newBlock;
    
    //assume that new block will definetly be large enough for Object
    
    object = (FreeObj*) ((void*)newBlock + HEADER_SIZE);
    object->next = (FreeObj*) ((void*)object + size);
    object->next->next = rootFree;
    rootFree = object->next;
    updateContents(rootFree);
    updateContents(object);
#ifdef DEBUG
    printf("look at first entry:");
    printContents(newBlock);
    printf("allocation finished!\n");
#endif
    return object;

}

void my_free(void* ptr) {
    FreeObj* newFree = (FreeObj*) ptr;
    newFree->next = rootFree;
    rootFree = newFree;
}

int updateContents(void* ptr){
#ifdef DEBUG
    printf("\nupdating contents with pointer: %p\n",ptr);
    
#endif
    Block* block = getBlock(ptr);
    if(block==NULL){
        fprintf(stderr,"Updating contents failed!\n");
        fprintf(stderr,"pointer: %p\n",ptr);
        fprintf(stderr,"blockp: %p\n",block);
        exit(0);
    }
    //position is the bit position
    unsigned int position = (int) (ptr -(void*) block - HEADER_SIZE)/8;
    /*pretty sure that indices are correct*/
    void* contents = (void*)block + 8;

    ((char*) contents)[position/8] |= (1)<<(7-(position % 8));
#ifdef DEBUG
    printf("Updating finished!");
#endif
    return 0;
}

    

/*finds corresponding block to given Object. Returns NULL in case of no result*/
Block* getBlock(void*ptr){
#ifdef DEBUG
    printf("trying to find corresponding block to ptr: %p\n",ptr);
#endif
    Block* block = (Block*) rootBlock;
    while((void*)ptr<(void*)block || (void*)ptr>(void*)(block+BLOCK_SIZE)){
      if(block->next==NULL){
          return NULL;
      }
      block = block->next;
    }
#ifdef DEBUG
    printf("found block: %p\n",block);
#endif
    return block;
}


/*returns size to next pointer according to contents in header
* size is in Bytes*/ 
int getObjSize(void* ptr){
#ifdef DEBUG
    printf("Getting object size...\n");
#endif
    Block* block = getBlock(ptr);
#ifdef DEBUG
    printf("Contents of block:\n");
    printContents(block);
#endif
    unsigned int position = (ptr - (void*)block - HEADER_SIZE)/8;
    int pos0 = position;
    //going bitwise through contents, looking for next object marker
    void* contents = (void*)block + 8;
    do{
        position++;
    }while(!(((char*) contents)[position/8] & (1)<<(7-(position % 8))) && position<DATA_SIZE/8);
#ifdef DEBUG
    printf("found object size: %d\n",(position-pos0)*8);
#endif
    return (position-pos0)*8; 

}

int initBlock(Block* block){
    char* cp = (char*) block;
    //filling header with zeros
    for(;cp<(char*) (DATA_SIZE/8); cp++) *cp=0;
#ifdef DEBUG
    printf("NEW BLOCK//////////////////////////////////////////////////////////////////////////////////////////////\n");
    printf("new block allocated %p and filled with zeros.\n",block);
#endif
    return 0;
}

void printContents(Block* block){
    void* content = (void*)block + 8;
    int position = 0;
    for(; position<1007; position++){
#ifdef DEBUG_CONTENT
        if(position % 8==0){
            printf("\n %d pointer: %p ", position, &(((char*) content)[position/8]));
        }
#endif
        (((char*) content)[position/8] & (1)<<(7-(position % 8))) ? printf("%d",1) : printf("%d",0);

    }
    printf("\n");
}


