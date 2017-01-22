
/* Nard Linux SDK
 * http://www.arbetsmyra.dyndns.org/nard
 * Copyright (C) 2014-2017 Ronny Nilsson
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>														/* For syscall SYS_xxx definitions */
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <pthread.h>

#include "high-load.h"
#include "main.h"
#include "misc.h"


//-------------------------------------------------------------
#define CHILD_SPAWN_DELAY		150												// Delay in ms between spawned childrens
#define MAX_LOAD_TIME			750												// Max time in ms we run with full load power consumption

enum child_state_t {
	THREAD_NONE,
	THREAD_STARTUP,
	THREAD_RUNNING,
	THREAD_ENDING,
	THREAD_HALTED
};

struct child_t {
	volatile enum child_state_t state;											// The child thread state
	int tid;																	// Linux PID of thread
	pthread_t thread;															// Posix thread ID
	int index;																	// Array index
	cpu_set_t cpuMask;															// Affinity
	int exitStatus;																// Return code after process exit

	int (*consumer)(struct child_t *me);										// Power consumer for specific thread
};


//-------------------------------------------------------------
static struct child_t *childs;
static struct timespec spawnTimer;												// When to spawn a child next time
static int maxChilds;															// Number of childs to spawn
static int hasFullLoad;															// True when we are consuming maximum power
static struct timespec loadTimer;
static pthread_t parentThread;													// Posix thread ID of parent


//-------------------------------------------------------------
int burn_cpu(struct child_t *me);
int idle_cpu(struct child_t *me);
extern int burn_cpu_a7(struct child_t *me);
int dump_sdcard(struct child_t *me);



//-------------------------------------------------------------
// Initialize high load testing
int high_load_init(void) {
	int nCpus, i;

	timer_set(&spawnTimer, CHILD_SPAWN_DELAY);
	timer_set(&loadTimer, 9999999);
	parentThread = pthread_self();

	nCpus = sysconf(_SC_NPROCESSORS_ONLN);
	//printf("System has %d processors\n", nCpus);
	maxChilds = nCpus + 1;

	// Prepare threads
	childs = calloc(maxChilds, sizeof(struct child_t));
	for(i = 0; i < maxChilds; i++) {
		childs[i].state = THREAD_NONE;
		childs[i].index = i;
		CPU_ZERO(&childs[i].cpuMask);
		CPU_SET(i % nCpus, &childs[i].cpuMask);
		childs[i].exitStatus = -1;
		childs[i].consumer = burn_cpu_a7;
	}
	childs[nCpus].consumer = dump_sdcard;
	//childs[nCpus].state = THREAD_HALTED;										// Disabled thread; for testing

	return 0;
}



//-------------------------------------------------------------
// Power consumer: generate random numbers in a loop
// until told to exit.
int burn_cpu(struct child_t *me) {
	volatile int dummy;

	// Burn cpu! :)
	while(!do_exit) {
		dummy = random();
		sched_yield();
	}

	return EXIT_SUCCESS;
}



//-------------------------------------------------------------
// Power consumer: null dummy. Do nothing (for test)
int idle_cpu(struct child_t *me) {
	return EXIT_SUCCESS;
}



//-------------------------------------------------------------
// Power consumer: read the SD card from begining to
// end in a loop until told to exit. This will make
// the board consume some extra mA.
int dump_sdcard(struct child_t *me) {
	const int SDREAD_LEN = 4 * 1024;
	off64_t sdSize, sdOffs;
	int sdFd, res;
	char *buf;

	sdFd = open("/dev/mmcblk0", O_RDONLY | O_LARGEFILE | O_NOATIME);
	if(sdFd == -1 || sdFd == 0) {
		perror("Error opening SD card block device");
		return EXIT_FAILURE;
	}

	// How large is the SD card?
	sdSize = lseek64(sdFd, 0, SEEK_END);
	if(sdSize == -1 || sdSize <= SDREAD_LEN) {
		printf("Error determining SD card size");
		close(sdFd);
		return EXIT_FAILURE;
	}

	res = 0;
	buf = malloc(SDREAD_LEN);

	do {
		// Read from a random address in the SD card.
		sdOffs = random() % (sdSize - (off64_t) SDREAD_LEN);
		res = lseek64(sdFd, sdOffs, SEEK_SET);
		if(res == -1) {
			perror("Error setting random SD card offset");
			break;
		}

		res = read(sdFd, buf, SDREAD_LEN);
		if(res == -1) {
			if(errno == EINTR) {
				res = 0;
			}
			else {
				perror("Error reading from SD card block device");
				break;
			}
		}

		sched_yield();
	} while(!do_exit);

	close(sdFd);
	free(buf);

	return (res == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
}



//-------------------------------------------------------------
// Read child state twice to a local variable to
// prevent race conditions between threads while
// still avoiding mutexes.
static const enum child_state_t child_state(const int idx) {
	enum child_state_t state[2];

	do {
		state[0] = childs[idx].state;
		pthread_yield();
		state[1] = childs[idx].state;
	} while(state[0] != state[1]);

	return state[0];
}



//-------------------------------------------------------------
// Cleanup handler for childrens. Executed when
// the thread is about to terminate.
static void child_exit_clean(void *arg) {
	struct child_t *me = arg;
	int i;

	// Slow throttle when high load test has finished
	for(i = 0; i < me->index * 50; i++) pthread_yield();

	//printf("Child %lu exits\n", me->thread);
	//fflush(NULL);

	/* Wake up parent from sleep so it can collect
	 * our exit code. We would have preferrd the kernel
	 * to send a signal instead, as with fork(). */
	me->state = THREAD_ENDING;
	pthread_kill(parentThread, SIGCHLD);
}



//-------------------------------------------------------------
// main() of childrens. Memory and open files are
// shared with the parent.
static void* child_main(void *arg) {
	struct sched_param schedParam;
	struct child_t *me;
	int res;

	/* Wait for parent to write my thread ID into global struct.
	 * Read it twice to prevent race conditions between threads. */
	me = (struct child_t*) arg;
	while(pthread_self() != me->thread && pthread_self() != me->thread) {
		pthread_yield();
	}
	me->tid = syscall(SYS_gettid);
	me->state = THREAD_RUNNING;
	pthread_cleanup_push(child_exit_clean, me);
	//printf("Child %lu %d has started\n", me->thread, me->tid);
	//fflush(NULL);

	// Possibly wait for scheduler to move us to correct cpu
	while(!CPU_ISSET(sched_getcpu(), &me->cpuMask)) usleep(0);

	/* Make ourself low priority. We should make minimal
	 * impact on the system if it has other important
	 * applications running. */
	schedParam.sched_priority = 0;
	res = pthread_setschedparam(me->thread, SCHED_BATCH, &schedParam);
	if(res == -1) {
		perror("Error setting low priority class");
		pthread_exit((void*) EXIT_FAILURE);
	}
	res = setpriority(PRIO_PROCESS, me->tid, 18);
	if(res == -1) {
		perror("Error setting child as nice prio");
		pthread_exit((void*) EXIT_FAILURE);
	}
	
	// Run the power consumer algorithm
	srandom(me->tid + time(NULL));
	if(me->consumer) res = me->consumer(me);

	pthread_cleanup_pop(1);
	pthread_exit((void*) res);
}



//-------------------------------------------------------------
// Create a new process which shares memory and
// open files with the parent.
static int child_spawn(void) {
	pthread_attr_t attr;
	int res, cIdx;

	// Find next free child index in list
	for(cIdx = 0; cIdx < maxChilds &&
		child_state(cIdx) != THREAD_NONE; cIdx++);
	if(cIdx == maxChilds) return -1;

	pthread_attr_init(&attr);

	// Set the CPU the child will run on
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	res = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t),
		&childs[cIdx].cpuMask);
	if(res == -1) {
		perror("Error, couldn't set child cpu affinity");
		return -1;
	}

	// Start a new child process
	childs[cIdx].state = THREAD_STARTUP;
	res = pthread_create(&childs[cIdx].thread, &attr,
		child_main, &childs[cIdx]);
	if(res == -1) {
		perror("Error spawning a child");
		return -1;
	}
	//printf("Parent %lu spawned %lu\n", pthread_self(), childs[cIdx].thread);
	//fflush(NULL);

	// Wait for the child to begin executing or terminate instantly
	while(child_state(cIdx) != THREAD_RUNNING &&
			child_state(cIdx) != THREAD_ENDING) {
		pthread_yield();
	}

	pthread_attr_destroy(&attr);

	return 0;
}



//-------------------------------------------------------------
// Has any child exited? Then collect their exit
// status to prevent them from becoming a zombie.
static int collect_child_exit(void) {
	void *exitVal;
	int res, i;

	for(i = 0; i < maxChilds; i++) {
		if(child_state(i) != THREAD_ENDING) continue;

		res = pthread_join(childs[i].thread, &exitVal);
		if(res == -1) {
			perror("Error collecting child exit status");
		}
		else if(res == 0) {
			childs[i].state = THREAD_HALTED;
			childs[i].exitStatus = (int) exitVal;
			//printf("Collected child %lu exit status %d\n",
			//	childs[i].thread, childs[i].exitStatus);
			maxSleep(0);
			if(childs[i].exitStatus) {
				printf("Child %lu exit error %d\n", childs[i].thread,
					childs[i].exitStatus);
				return -1;
			}
		}
	}

	return 0;
}



//-------------------------------------------------------------
// Returns true as long as any child is still alive.
int isAnyChildAlive(void) {
	int i;

	for(i = 0; i < maxChilds; i++) {
		switch(child_state(i)) {
			case THREAD_STARTUP:
			case THREAD_RUNNING:
			case THREAD_ENDING:
				return 1;
			default:
				break;
		}
	}

	return 0;
}



//-------------------------------------------------------------
// Returns true when all childrens have been started
int hasAllChildsStarted(void) {
	int i;

	for(i = 0; i < maxChilds; i++) {
		switch(child_state(i)) {
			case THREAD_NONE:
			case THREAD_STARTUP:
				return 0;
			default:
				break;
		}
	}

	return 1;
}


//-------------------------------------------------------------
// If any child is still alive; kill it hard.
int kill_remaining_childs(void) {
	int i;

	for(i = 0; i < maxChilds; i++) {		
		switch(child_state(i)) {
			case THREAD_STARTUP:
			case THREAD_RUNNING:
			case THREAD_ENDING:
				if(pthread_kill(childs[i].thread, SIGKILL)) {
					perror("Error killing child hard");
				}
				break;
			default:
				break;
		}
	}

	return 0;
}



//-------------------------------------------------------------
int high_load_manager(void) {
	int res;

	res = 0;

	/* When all childrens have started and
	 * consume max power; start a timer. We only
	 * run with full load for a limited time.
	 * If the we for any reason gets interrupted
	 * the program will exit with a failure
	 * return code. */
	if(timer_timeout(&loadTimer)) {
		do_exit = 1;
	}
	else {
		maxSleep(timer_remaining(&loadTimer));
		if(do_exit) res = -1;
	}

	if(!res && !do_exit) {
		if(hasFullLoad) {
			if(!isAnyChildAlive()) res = -1;
		}
		else {
			if(hasAllChildsStarted()) {
				hasFullLoad = 1;
				printf("Power consumption test in progress...\n");
				timer_set(&loadTimer, MAX_LOAD_TIME);
			}
			else  {
				/* Time to spawn another child? We need some
				 * delay between them due to the Raspberry
				 * firmware polls the brown out sensor only
				 * every 100 ms. Plus there might be some
				 * capacitances to drain. */
				if(timer_timeout(&spawnTimer)) {
					res = child_spawn();
					timer_set(&spawnTimer, CHILD_SPAWN_DELAY);
				} 
				maxSleep(timer_remaining(&spawnTimer));
			}
		}
	}

	// Check if any child has exited
	if(collect_child_exit()) res = -1;

	return res;
}


