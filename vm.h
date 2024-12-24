#ifndef VM_H
#define VM_H

#include <stdlib.h>

#define KVA2PA(kva) ((unsigned long)(kva))
#define PA2KVA(pa) ((unsigned long)(pa))

static inline void* allocPage(){
    return (void*)malloc(BLOCK_SIZE);
}

#endif /* VM_H */