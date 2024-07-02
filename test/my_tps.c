//my_tps - Custom test file
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tps.h>
#include <sem.h>

static char goodbye_msg[TPS_SIZE] = "Goodbye World!\n";
static char hello_msg[TPS_SIZE] = "Hello ";
static char world_msg[TPS_SIZE] = "World!\n";

void *latest_mmap_addr; // global variable to make the address returned by mmap accessible

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    latest_mmap_addr = __real_mmap(addr, len, prot, flags, fildes, off);
    return latest_mmap_addr;
}

/* thread1 - Tests copy on write
 * Clones main's tps and writes to it.
 */
void *thread1(void* arg) {
	// tps_clone()
	assert(tps_clone(*(pthread_t *)arg) == 0);
	assert(tps_clone(*(pthread_t *)arg) == -1);

	char readBuf[TPS_SIZE];
	tps_read(0, 14, readBuf);
	// Cloned thread can read properly
	assert(strcmp(readBuf, "Hello World!\n") == 0);

	// Copy on write
	tps_write(0, 16, goodbye_msg);
	tps_read(0, 16, readBuf);
	assert(strcmp(readBuf, "Goodbye World!\n") == 0);

	assert(tps_destroy() == 0);

	return NULL;
}

/* thread2 - Testing interactions between destroy, create, and clone. */
void *thread2(void *arg) {
	// Cannot destroy before TPS is created
	assert(tps_destroy() == -1);
	tps_create();
	assert(tps_destroy() == 0);

	// tps_destroy() destroyed properly
	assert(tps_clone(*(pthread_t *)arg) == 0);

	char readBuf[TPS_SIZE];
	tps_read(0, 14, readBuf);
	assert(strcmp(readBuf, "Hello World!\n") == 0);

	assert(tps_destroy() == 0);
	
	return NULL;
}

void *thread3(void *arg) {
	tps_create();
	/* Get TPS page address as allocated via mmap() */
    char *tps_addr = latest_mmap_addr;
    /* Cause an intentional TPS protection error */
    tps_addr[0] = '\0';
    return NULL;
}

int main() {
	pthread_t tid;
	pthread_t main_tid = pthread_self();
	char readBuf[TPS_SIZE];

	// Before initialization of TPS, nothing should work
	assert(tps_create() == -1);
	assert(tps_destroy() == -1);	
	assert(tps_write(0, 0, hello_msg) == -1);
	assert(tps_read(0, 0, readBuf) == -1);

	// tps_init()
	assert(tps_init(1) == 0);
	assert(tps_init(1) == -1);

	// tps_create()
	assert(tps_create() == 0);
	assert(tps_create() == -1);

	// tps_write()
	assert(tps_write(0, 0, NULL) == -1); // Cannot write from NULL
	assert(tps_write(0, 4097, hello_msg) == -1); // Out of bounds
	tps_write(0, 6, hello_msg);
	tps_write(6, 8, world_msg);

	//tps_read()
	assert(tps_read(0, 0, NULL) == -1); // Cannot read to NULL
	assert(tps_read(0, 4097, readBuf) == -1); // Out of bounds
	tps_read(0, 14, readBuf);
	assert(strcmp(readBuf, "Hello World!\n") == 0);

	pthread_create(&tid, NULL, thread1, &main_tid);
	pthread_join(tid, NULL);

	tps_read(0, 15, readBuf);
	// thread1 copied before writing to its new page, so
	// main's page is unchanged.
	assert(strcmp(readBuf, "Hello World!\n") == 0);

	pthread_create(&tid, NULL, thread2, &main_tid);
	pthread_join(tid, NULL);

	// tps_destroy()
	assert(tps_destroy() == 0);
	assert(tps_destroy() == -1);

	printf("\nAll asserts passed. About to test page protection, exxpect segfault.\n\n");
	pthread_create(&tid, NULL, thread3, &main_tid);
	pthread_join(tid, NULL);

	return 0;
}