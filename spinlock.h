#ifndef SPINLOCK_H
#define SPINLOCK_H

typedef struct spinlock {
    int locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void acquire(spinlock_t *lock);
void release(spinlock_t *lock);

#endif /* SPINLOCK_H */