
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

#include "high-load.h"
#include "main.h"
#include "misc.h"


//-------------------------------------------------------------
#define CHILD_STACK_SIZE		(4 * 1024)
#define CHILD_SPAWN_DELAY		150												// Delay in ms between spawned childrens
#define MAX_LOAD_TIME		3820													// Max time in ms we run with full load power consumption


struct child_t {
	int tid;
	int index;																	// Array index
	cpu_set_t cpuMask;
	int hasStarted;
	int hasExited;
	int exitStatus;																// Return code after process exit

	int (*consumer)(struct child_t *me);										// Power consumer for specific thread
	uint8_t *stack;																// Process stack
};


//-------------------------------------------------------------
static struct child_t *childs;
static struct timespec spawnTimer;												// When to spawn a child next time
static int maxChilds;															// Number of childs to spawn
static int nChilds;																// Number of childs we have spawned so far
static int hasFullLoad;															// True when we are consuming maximum power
static struct timespec loadTimer;


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

	nCpus = sysconf(_SC_NPROCESSORS_ONLN);
nCpus=1;
	//printf("System has %d processors\n", nCpus);
	maxChilds = nCpus + 1;

	// Prepare threads
	childs = calloc(maxChilds, sizeof(struct child_t));
	for(i = 0; i < maxChilds; i++) {
		childs[i].index = i;
		CPU_ZERO(&childs[i].cpuMask);
		CPU_SET(i % nCpus, &childs[i].cpuMask);
		childs[i].exitStatus = -1;
		childs[i].consumer = burn_cpu_a7;
		childs[i].stack = malloc(CHILD_STACK_SIZE);
		if(!childs[i].stack) return -1;
	}
	childs[nCpus].consumer = dump_sdcard;
childs[nCpus].consumer = idle_cpu;

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
	return 0;
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
// main() of childrens. Memory and open files are
// shared with the parent.
int child_main(void *arg) {
	struct sched_param schedParam;
	struct child_t *me;
	int res , i;

	// Wait for parent to write my tid into global struct
	me = (struct child_t*) arg;
	me->hasStarted = 1;
	while(me->tid != syscall(SYS_gettid)) sched_yield();

	// Possibly wait for scheduler to move us to correct cpu
	while(!CPU_ISSET(sched_getcpu(), &me->cpuMask)) usleep(0);

	/* Make ourself low priority. We should make minimal
	 * impact on the system if it has other important
	 * applications running. */
	schedParam.sched_priority = 0;
	res = sched_setscheduler(me->tid, SCHED_BATCH, &schedParam);
	if(res == -1) {
		perror("Error setting low priority class");
		return EXIT_FAILURE;
	}
	res = setpriority(PRIO_PROCESS, me->tid, 18);
	if(res == -1) {
		perror("Error setting child as nice prio");
		return EXIT_FAILURE;
	}
	
	srandom(me->tid + time(NULL));

	if(me->consumer) res = me->consumer(me);

	// Slow throttle when high load test has finished
	for(i = 0; i < me->index * 100; i++) sched_yield();

	return res;
}



//-------------------------------------------------------------
// Create a new process which shares memory and
// open files with the parent.
static int spawn_child(void) {
	int res;

	/* Move parent to next cpu. The child will
	 * inherit and run on very same cpu. */
	res = sched_setaffinity(syscall(SYS_gettid), sizeof(cpu_set_t),
		&childs[nChilds].cpuMask);
	if(res == -1) {
		perror("Error, couldn't set cpu affinity");
		return -1;
	}
	fflush(NULL);
	sched_yield();

	// Clone a new child process
	childs[nChilds].tid = clone(child_main, childs[nChilds].stack + 
		CHILD_STACK_SIZE, CLONE_FS | CLONE_FILES | CLONE_VM | SIGCHLD,
		&childs[nChilds]);
	if(childs[nChilds].tid == -1) {
		childs[nChilds].tid = 0;
		perror("Error spawning a child");
		return -1;
	}
	else if(childs[nChilds].tid == 0) {
		printf("Error; invalid child tid 0\n");
		return -1;
	}

	// Wait for the child to begin executing
	while(!childs[nChilds].hasStarted) sched_yield();
	fflush(NULL);
	nChilds++;

	return 0;
}



//-------------------------------------------------------------
// Poll childrens; has anyone exited? Then collect their
// exit status to prevent them from becoming a zombie.
static int collect_child_exit(void) {
	int res, i;

	for(i = 0; i < maxChilds; i++) {
		if(!childs[i].tid || childs[i].hasExited) continue;

		res = waitpid(childs[i].tid, &childs[i].exitStatus, WNOHANG | __WALL);
		if(res == -1) {
			perror("Error collecting child exit status");
		}
		else if(res == childs[i].tid) {
			free(childs[i].stack);
			childs[i].stack = NULL;
			childs[i].hasExited = 1;
			maxSleep(0);
			if(WEXITSTATUS(childs[i].exitStatus)) {
				printf("Child %d exit error %d\n", childs[i].tid,
				WEXITSTATUS(childs[i].exitStatus));
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
		if(childs[i].tid && !childs[i].hasExited) return 1;
	}

	return 0;
}



//-------------------------------------------------------------
// Returns true when all childrens have been started
int hasAllChildsStarted(void) {
	return (nChilds == maxChilds);
}



//-------------------------------------------------------------
// If any child is still alive; kill it hard.
int kill_remaining_childs(void) {
	int i;

	// Return true if any child is still alive
	for(i = 0; i < maxChilds; i++) {		
		if(childs[i].tid && !childs[i].hasExited) {
			if(syscall(SYS_tkill, childs[i].tid, SIGKILL)) {
				perror("Error killing child hard");
			}
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
	 * run with full load for a limited time. */
	if(timer_timeout(&loadTimer)) {
		do_exit = 1;
	}
	else {
		maxSleep(timer_remaining(&loadTimer));
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
					res = spawn_child();
					timer_set(&spawnTimer, CHILD_SPAWN_DELAY);
				} 
				maxSleep(timer_remaining(&spawnTimer));
			}
		}
	}

	// Check if any child has exited
	if(!res) res = collect_child_exit();

	return res;
}


