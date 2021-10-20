#include "pthread.h"

//exclusive lock version 
int glb_lock_init(pthread_mutex_t *lock);
int  glb_lock_free(pthread_mutex_t *lock);
void glb_lock_block(pthread_mutex_t *lock);
void glb_lock_unlock(pthread_mutex_t *lock);

//rw-lock version of the utilities
int     glb_rwlock_init(pthread_rwlock_t *rwlock);
int     glb_rwlock_free(pthread_rwlock_t *rwlock);

void    glb_rwlock_wrlock(pthread_rwlock_t *rwlock);
void    glb_rwlock_rdlock(pthread_rwlock_t *rwlock);
void    glb_rwlock_unlock(pthread_rwlock_t *rwlock);

