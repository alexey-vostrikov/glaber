/**********the copyright ************/
/*"No time to explain, just stick cucumbers into your ...."
But in reality zbx_locks* isn't extendable to use any 
number of locks, so this qute simple set of functions
to ease creation and use rw locks and mutex locks
rw locks are intended to be used when there are lot's 
of reads happening on rarely changed data
while mutex locks are good and more cpu efficient on 
exclusive locks
It maybe also a good idea to introduce atomic spin locks
here either*/
#include "pthread.h"
#include "common.h"
#include "log.h"

#define GLB_LOCK_SLEEP_US 95 //how many microseconds to sleep waiting for spinlock

int glb_lock_init(pthread_mutex_t *lock) {
	pthread_mutexattr_t	mta;

	if (0 != pthread_mutexattr_init(&mta))
		return FAIL;
	
	if (0 != pthread_mutexattr_setpshared(&mta, PTHREAD_PROCESS_SHARED))
		return FAIL;
	
	if (pthread_mutex_init(lock, &mta)!= 0)
        return FAIL;
    
	return SUCCEED;
}

int  glb_lock_free(pthread_mutex_t *lock) {
    return pthread_mutex_destroy(lock);
}

void glb_lock_block(pthread_mutex_t *lock) {
    while ( pthread_mutex_trylock(lock)) {
		usleep(GLB_LOCK_SLEEP_US);
	};
}

void glb_lock_unlock(pthread_mutex_t *lock) {
    pthread_mutex_unlock(lock);
}

//rw-lock version of the utilities
int glb_rwlock_init(pthread_rwlock_t *rwlock) {
	pthread_rwlockattr_t	rwa;

    if (0 != pthread_rwlockattr_init(&rwa))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot initialize read write lock attribute: %s", zbx_strerror(errno));
		return FAIL;
	}

	if (0 != pthread_rwlockattr_setpshared(&rwa, PTHREAD_PROCESS_SHARED))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot set shared read write lock attribute: %s", zbx_strerror(errno));
		return FAIL;
	}

	if (0 != pthread_rwlock_init(rwlock, &rwa))
	{
		zabbix_log(LOG_LEVEL_WARNING,  "cannot create rwlock: %s", zbx_strerror(errno));
		return FAIL;
	}
	
	return SUCCEED;
}

int  glb_rwlock_free(pthread_rwlock_t *rwlock) {
    pthread_rwlock_destroy(rwlock);
    rwlock = NULL;
}

void glb_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    if ( NULL == rwlock)
		return;

	while ( pthread_rwlock_trywrlock(rwlock)) {
		usleep(GLB_LOCK_SLEEP_US);
	};

}

void glb_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    if ( NULL == rwlock)
		return;
	
	while ( pthread_rwlock_tryrdlock(rwlock)) {
		usleep(GLB_LOCK_SLEEP_US);
	};
}

void  glb_rwlock_unlock(pthread_rwlock_t *rwlock)
{
	if (0 != pthread_rwlock_unlock(rwlock))
	{
		zabbix_log(LOG_LEVEL_WARNING, "[file:'%s',line:%d] read-write lock unlock failed: %s",  __FILE__, __LINE__, zbx_strerror(errno));
		exit(EXIT_FAILURE);
	}
}
