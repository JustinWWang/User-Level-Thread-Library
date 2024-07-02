#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

#define PAGE_SIZE 4096

/* TPS_queue: contains all created TPS's
 * We are using this queue as a map 
 * to find TPS's based on their TIDs and page addresses
 */
queue_t TPS_queue = NULL;

// page pointer
typedef struct page* page_t;

/* page - Memory page structure
 * @ addr: Starting address of page
 * @ refs: how many TPSs refer to the page
 */
struct page {
	void *addr;
	int refs;
};

typedef struct TPS* TPS_t;

/* TPS - thread private storage structure 
 * @ tid: Thread ID
 * @ page: pointer to page struct
 */
struct TPS {
	pthread_t tid;
	page_t page;
};

/* construct_TPS - allocate memory for TPS struct and initialize its tid field.
 * @ tid: Thread ID of the TPS that we are constructing
 * @ return: TPS pointer
 */
TPS_t construct_TPS(pthread_t tid) {
	TPS_t tps = (TPS_t)malloc(sizeof(struct TPS));
	tps->tid = tid;
	tps->page = NULL;
	return tps;
}

/* tps_find - finds the tps with tid @arg 
 * @ data: current TPS_t in the queue
 * @ arg: Address of TID
 * @ Return: 1 if @arg matches current TPS's tid.
 */
static int tps_find (void *data, void *arg) {
	return ((TPS_t)data)->tid == *((pthread_t*)arg);
}

/* get_TPS - finds the TPS of thread with thread ID @tid in the TPS_queue
 * 
 * Return: NULL if tps is not found, 
 * otherwise return address of target thread's TPS
 */
TPS_t get_TPS(pthread_t tid) {
	TPS_t tps = NULL;

	// Look for current tid in the queue.
	queue_iterate(TPS_queue, tps_find, (void *)&tid, (void **)&tps);

	return tps;
}

/* get_current_TPS - finds the TPS of the current thread in the TPS_queue
 * 
 * Return: NULL if tps is not found, 
 * otherwise return address of current thread's TPS
 */
TPS_t get_current_TPS(void) {
	TPS_t tps = get_TPS(pthread_self());

	return tps;
}

/* tps_find - finds the tps with page starting address @arg
 * @ data: current TPS_t in the queue
 * @ arg: Address of TID
 * @ Return: 1 if @arg matches current TID's address.
 */
static int tps_find_addr (void *data, void *arg) {
	return ((TPS_t)data)->page->addr == arg;
}

// Identify if the segfault is due to TPS protection
static void segv_handler(int sig, siginfo_t *si, void *context) {
    //hush gcc!
    (void)context;

    /*
     * Get the address corresponding to the beginning of the page where the
     * fault occurred
     */
    void *p_fault = (void*)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));

    //Iterate through all the TPS areas and find if p_fault matches one of them
   	TPS_t tps = NULL;
	// Look for the page with starting address p_fault
	queue_iterate(TPS_queue, tps_find_addr, p_fault, (void **)&tps);

    if (tps)
        fprintf(stderr, "TPS protection error!\n");

    /* In any case, restore the default signal handlers */
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    /* And transmit the signal again in order to cause the program to crash */
    raise(sig);
}

int tps_init(int segv) {
	enter_critical_section();	
	// TPS has already been initialized
	if (TPS_queue) {
		exit_critical_section();
		return -1;
	}
	
	if (segv) {
        struct sigaction sa;

        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = segv_handler;
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGSEGV, &sa, NULL);
    }

	TPS_queue = queue_create();

	// Failure to create queue
	if (!TPS_queue){
		exit_critical_section();
		return -1;
	}

	exit_critical_section();
	return 0;
}

int tps_create(void) {
	enter_critical_section();
	TPS_t tps = get_current_TPS();

	// TPS already exists or init has not been called
	if (tps || !TPS_queue){
		exit_critical_section();
		return -1;
	}

	pthread_t tid = pthread_self();
	tps = construct_TPS(tid);
	tps->page = (page_t)malloc(sizeof(struct page));
	tps->page->refs = 1;
	// Private and anonymous, with no read or write access.
	tps->page->addr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	// TPS allocation failed
	if (tps->page->addr == MAP_FAILED){
		exit_critical_section();
		return -1;
	}

	queue_enqueue(TPS_queue, tps);

	exit_critical_section();
	return 0;
}

int tps_destroy(void) {
	enter_critical_section();
	TPS_t tps = get_current_TPS();

	// Current thread never created a TPS
	if (!tps){
		exit_critical_section();
		return -1;
	}

	queue_delete(TPS_queue, tps);

	// Only free the page if there are no other references to it
	(tps->page->refs)--;
	if (tps->page->refs == 0)
		free(tps->page);
	free(tps);

	exit_critical_section();
	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer) {
	enter_critical_section();
	TPS_t tps = get_current_TPS();

	// Out of bounds, tps doesn't exist, or buffer is NULL.
	if (offset+length > PAGE_SIZE || !tps || !buffer){
		exit_critical_section();
		return -1;
	}

	// Temporarily give read permissions and read from readAddr to buffer.
	void *readAddr = (void *)((char *)(tps->page->addr) + offset);
	mprotect(tps->page->addr, PAGE_SIZE, PROT_READ);
	memcpy(buffer, readAddr, length);
	mprotect(tps->page->addr, PAGE_SIZE, PROT_NONE);

	exit_critical_section();
	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer) {
	enter_critical_section();
	TPS_t curr_tps = get_current_TPS();

	// Out of bounds, tps doesn't exist, or buffer is NULL.
	if (offset+length > PAGE_SIZE || !curr_tps || !buffer) {
		exit_critical_section();
		return -1;
	}

	// We are writing to a page that is shared by another thread.
	if (curr_tps->page->refs > 1) {
		// We are going to be referring to our new page instead of the shared one.
		(curr_tps->page->refs)--;
		page_t prev_page = curr_tps->page;
		curr_tps->page = (page_t)malloc(sizeof(struct page));
		curr_tps->page->addr = mmap(NULL, PAGE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		curr_tps->page->refs = 1;

		// Temporarily give permissions and duplicate the page.
		mprotect(curr_tps->page->addr, PAGE_SIZE, PROT_WRITE);
		mprotect(prev_page->addr, PAGE_SIZE, PROT_READ);
		memcpy(curr_tps->page->addr, prev_page->addr, PAGE_SIZE);
		mprotect(curr_tps->page->addr, PAGE_SIZE, PROT_NONE);
		mprotect(prev_page->addr, PAGE_SIZE, PROT_NONE);
	}

	// Temporarily give permissions and write from buffer to writeAddr.
	void *writeAddr = (void *)((char *)(curr_tps->page->addr) + offset);
	mprotect(curr_tps->page->addr, PAGE_SIZE, PROT_WRITE);
	memcpy(writeAddr, buffer, length);
	mprotect(curr_tps->page->addr, PAGE_SIZE, PROT_NONE);

	exit_critical_section();
	return 0;
}

int tps_clone(pthread_t tid) {
	enter_critical_section();
	TPS_t curr_tps = get_current_TPS();
	TPS_t from_tps = get_TPS(tid);

	// current thread already has a TPS, or thread @tid doesn't have a TPS.
	if (curr_tps || !from_tps) {
		exit_critical_section();
		return -1;
	}

	exit_critical_section();
	tps_create();
	enter_critical_section();

	curr_tps = get_current_TPS();
	curr_tps->page = from_tps->page;
	(curr_tps->page->refs)++;

	exit_critical_section();
	return 0;
}

