#include "spinlock.h"
#include <stdio.h>

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

void acquire(spinlock_t *lock) {
    if(lock->locked) {
        printf("spinlock is already locked\n");
        while(1);
    }
    lock->locked = 1;
}

void release(spinlock_t *lock) {
    if(!lock->locked) {
        printf("spinlock is not locked\n");
        while(1);
    }
    lock->locked = 0;
}