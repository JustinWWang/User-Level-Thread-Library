#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

/* sephamore - Keeps track of shared resources.
 * @count: Number of times the resource can be shared.
 * @waiting: Queue of threads waiting to use this resource.
 */
struct semaphore {
	int count;
	queue_t waiting;
};

sem_t sem_create(size_t count) {
	enter_critical_section();

	sem_t sem = (sem_t)malloc(sizeof(struct semaphore));

	sem->count = count;
	sem->waiting = queue_create();

	exit_critical_section();
	return sem;
}

int sem_destroy(sem_t sem) {
	enter_critical_section();
	if (sem == NULL || queue_length(sem->waiting))
		return -1;

	queue_destroy(sem->waiting);
	free(sem);

	exit_critical_section();
	return 0;
}

int sem_down(sem_t sem) {
	enter_critical_section();
	if (sem == NULL)
		return -1;

	// If there is no free resource, wait for one to free up.
	while (sem->count == 0) {
		pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t));
		// Malloc failed
		if (!tid)
			return -1;

		*tid = pthread_self();
		queue_enqueue(sem->waiting, (void *)tid);
		thread_block();
	}
	// There is at least one free resource now.
	(sem->count)--;

	exit_critical_section();
	return 0;
}

int sem_up(sem_t sem) {
	enter_critical_section();
	if (sem == NULL)
		return -1;

	(sem->count)++;
	// If there is a process waiting for the resource, unblock it.
	if (queue_length(sem->waiting)){
		pthread_t *tid;
		queue_dequeue(sem->waiting, (void**)&tid);
		thread_unblock(*tid);
		free(tid);
	}

	exit_critical_section();
	return 0;
}

