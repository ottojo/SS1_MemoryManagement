#include <stdlib.h>
#include "my_alloc.h"
#include "my_system.h"
#include "stdio.h"

#define HEADER_SIZE (8+128)
#define BLOCK_SIZE 8192
#define PAYLOAD_SIZE (BLOCK_SIZE-HEADER_SIZE)
#define MIN_OBJECT_SIZE 8
#define MAX_OBJECT_SIZE 256

/*Object alignment is represented in info header of a block with length INFO_SIZE.*/
/*Every single entry in contents of our header stands for a "new Object" of MIN_OBJECT_SIZE in data section of the Block*/

/*TODO: 
 * changeing all void* typedefs to struct qith pointer to next object. void pointer cannot be dereferenced*/

typedef struct Block {
    struct Block* next;
}Block;


/*List for free Object spaces in block. Does not care about the amount of Blocks*/
/*TODO: 
 * High potential for optimizing here
 * Ideas:
 * -Thinking of filling each block first, then using freed Object space or the other way round
 * -Sorting
 * -weighted Predictions
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

FreeObj* rootFree;
Block* rootBlock;

void init_my_alloc() {
    rootBlock = get_block_from_system();
    if(rootBlock==NULL){
        fprintf(stderr,"System MEM failure!");
        exit(0);
    }
    initBlock(rootBlock);
    rootFree = (FreeObj*) (rootBlock + HEADER_SIZE);
    rootFree->next = NULL;
    updateContents(rootFree);
}

void* my_alloc(size_t size) {
    FreeObj* object; 
    object = rootFree;

    /*case: rootFree matches with query*/
    if(getObjSize(rootFree)>=size){
        if(rootFree->next!=NULL && rootFree+size==rootFree->next){
            rootFree = rootFree->next;
            return object;
        }
        rootFree += size;
        rootFree->next = object->next; //because *object now holds the next element
        updateContents(rootFree);
        return object;
    }
    /*case: any other element does match*/
    
    while(object->next!=NULL){
        FreeObj* nextfree = object->next;

        //nextfree is matching
        if(getObjSize(nextfree)>=size){
           if(nextfree->next!=NULL && nextfree+size == nextfree->next){
               object->next = nextfree->next;
               return nextfree;
           }

           /*object here is the new freepointer between nextfree, wich will be returned, and *nextfree*/
           object->next = nextfree+size;
           object->next->next = nextfree->next;
           updateContents(nextfree->next);
           return nextfree;
        }
        /*else jump to nextfree*/
        object = nextfree;
    }

    /*no matching free Object found -> allocate new block*/

    Block* newBlock = get_block_from_system();
    if(newBlock==NULL){
        fprintf(stderr,"System MEM failure!");
        exit(0);
    }
    initBlock(newBlock);

    //inserting newBlock as rootBlock of List
    newBlock->next = rootBlock;
    rootBlock = newBlock;
    
    //assume that new block will definetly be large enough for Object
    
    object = (FreeObj*) (newBlock + HEADER_SIZE);
    object->next = (FreeObj*) (object + size);
    object->next->next = rootFree;
    rootFree = object->next;
    
    return object;

}

void my_free(void* ptr) {

}

int updateContents(void* ptr){
    Block* block = getBlock(ptr);
    if(block==NULL){
        fprintf(stderr,"Updating contents failed!");
        exit(0);
    }
    //position is the bit position
    unsigned int position = (int) (ptr -(void*) block - HEADER_SIZE)/8;
    /*not sure if indices are correct*/

    ((char*) block)[position/8] |= (1)<<(7-(position % 8));
    return 0;
}

    

/*finds corresponding block to given Object. Returns NULL in case of no result*/
Block* getBlock(void*ptr){
    Block* block = rootBlock;
    while(ptr<(void*)block || ptr>(void*)(block+BLOCK_SIZE)){
      if(block->next==NULL) return NULL;
      block = block->next;
    }
    return block;
}


/*returns size to next pointer according to contents in header
* size is amount of Objects of MIN_OBJECT_SIZE*/ 
int getObjSize(void* ptr){

    Block* block = getBlock(ptr);
    unsigned int position = (ptr - (void*)block - HEADER_SIZE)/8;
    int pos0 = position;
    //going bitwise through conntents, looking for next object marker
    do{
        position++;
    }while(!(((char*) block)[position/8] & (1)<<(position % 8)));
    return (position-pos0); //returns size in amount of Objects of MIN_OBJECT_SIZE 

}

int initBlock(Block* block){
    char* cp = (char*) block;
    //filling header with zeros
    for(;cp<((char*) (block + HEADER_SIZE)); cp++) *cp=0;
    return 0;
}

