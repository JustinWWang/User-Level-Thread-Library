# Project #3 - User-level thread library (part 2)

## Phase 1 - Semaphore

### Semaphore structure
Our semaphore structure contains a count (generally, the number of available 
resources) and a queue of waiting threads. We initially had a maximum count, 
but discovered that it was not neccessary. 

### sem_create() and sem_destroy()
The implementations of `sem_create` and `sem_destroy` are fairly 
straightforward.

### sem_down()
`sem_down` decrements the counter if it is greater than 0. If not, then the 
TID of the calling thread is placed in the waiting queue and the thread is 
blocked until the resource is made available by another thread's `sem_up`.

### sem_up()
`sem_up` increments the counter. If the waiting queue is not empty, the TID is 
popped and the corresponding thread is unblocked. The thread can then resume 
in `sem_down` and decrement the count.

## Phase 2 - Thread Private Storage

### TPS structure
Our TPS structure contains a TID and a pointer to a page structure.

### Page structure
The page structure wraps the memory address of a page and additionally 
contains a count of the number of references to that address (more or less as 
specified in the assignment).

### TPS queue
We are storing the TPS structs in a TPS queue. We are essentially treating the 
queue as a map, since we only call `find` and `delete` on the items inside it

### tps_init()
`tps_init` initializes the TPS queue and sets up the signal handler if `segv` 
is nonzero.

### tps_create()
`tps_create` constructs a new TPS and its page and maps a new memory page to 
the page struct's `addr` field using mmap with the private and anonymous flags.

### tps_destroy()
`tps_destroy` first finds the calling thread's TPS (returning if it does not 
exist) and then removes it from the queue and deallocates it. Its page 
structure is deallocated if and only if no other TPS is pointing to it.

### tps_read()
`tps_read` checks the bounds of the request, temporarily sets read permission 
on the page, and copies the data into the buffer using memcpy.

### tps_write()
Similarly, `tps_write` checks the bounds, sets write permissions, and copies 
data from the buffer into the page. In the case where we write to a shared 
page (number of references is greater than 1), we first allocate a new page 
and copy over the contents of the shared page before writing to the newly 
allocated page.

### tps_clone()
`tps_clone` sets the current thread's page pointer to the target thread's 
page. We don't copy the contents of the page until one of the sharing threads 
calls `tps_write`.

## Placement of critical sections
We opted to treat every library function as a critical section for 
consistency, and because we assumed the implementation of 
`enter_critical_section` using some variant of the pthread `mutex_lock` and 
therefore would have no effect on the execution of threads that do not need to 
access library functions.

## my_tps.c tester
Our tester is relatively self-documented. In general, we are testing the edge 
cases of each function, as well as common use cases and some interactions 
between functions. Specifically, we test copy-on-write (using serial reads and 
writes between multiple threads) and that `tps_destroy` successfully cleans 
our envionment such that `tps_create` can be called afterwards. We also tested 
incorrect calls to all of the functions. 

## Sources
We only used the man pages associated with pthread, mmap, mprotect, and 
miscellaneous other functions. We also referenced stackexchange for some minor 
pointer issues, such as arithmetic with void \* (we solved this by casting to 
char \*, because the size of a char is one byte).