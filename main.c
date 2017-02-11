
/* Program for testing the board power supply. We load
 * the system to the max (to consume lots of power)
 * while we monitor the board brown out sensor.
 *
 * Measured values (without external peripherals)
 *   Pi model    Current
 *      1B+      320 mA
 *      2B       650 mA
 *      3B       1300 mA with Neon and 830 mA without
 *
 * Nard Linux SDK
 * http://www.arbetsmyra.dyndns.org/nard
 * Copyright (C) 2014-2017 Ronny Nilsson
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#include "main.h"
#include "misc.h"
#include "high-load.h"
#include "vchiq.h"


//-------------------------------------------------------------
#define MAX_TOT_TIME		999999999											// Max total time in ms we allow the test to run
#define DFLT_TOT_TIME		10000												// Default total time ms we allow the test to run


//-------------------------------------------------------------
static int sigFd = -1;															// Signal file descriptor
static int tot_time;															// Run for this many millisecons maximum
static const char progVer[] = "v0.10";											// Program version


//-------------------------------------------------------------
// Initialize signals. We use a synchronous filedescriptor
// for accepting signal as oppose to an asynchronous handler.
static int signal_init(void) {
	sigset_t sigsBlk;
	int res;

	sigemptyset(&sigsBlk);
	sigaddset(&sigsBlk, SIGCHLD);
	sigaddset(&sigsBlk, SIGHUP);
	sigaddset(&sigsBlk, SIGINT);
	sigaddset(&sigsBlk, SIGQUIT);
	sigaddset(&sigsBlk, SIGTERM);

	/* Block signals so that they aren't handled
	 * according to their default dispositions. */
	res = sigprocmask(SIG_BLOCK, &sigsBlk, NULL);
	if(res == -1) {
		perror("Error sigprocmask");
		return -1;
	}

	sigFd = signalfd(-1, &sigsBlk, 0);
	if(sigFd == -1) {
		perror("Error opening signal fd");
		return -1;
	}

	return 0;
}



//-------------------------------------------------------------
// Manage signals. We get here when a signal is
// delivered in our signal file descriptor.
static int signal_manager(void) {
	struct signalfd_siginfo sigBuf;
	int res;

	res = read(sigFd, &sigBuf, sizeof(struct signalfd_siginfo));
	if(res == -1) {
		perror("Error reading from signal fd");
		return -1;
	}
	else if(res == sizeof(struct signalfd_siginfo)) {
		maxSleep(0);
		//printf("Got signal %d\n", sigBuf.ssi_signo);

		switch(sigBuf.ssi_signo) {
			case SIGCHLD:
				//printf("Signal from child %d exit\n", sigBuf.ssi_pid);
				break;

			case SIGHUP:
			case SIGQUIT:
			case SIGTERM:
				//printf("Time to exit\n");
				do_exit = 1;
				break;

			case SIGINT:
				abort();
				break;
		}
	}

	return 0;
}



//-------------------------------------------------------------
// Parse commandline arguments
static int parse_args(int argc, char *argv[]) {
	int arg, res = 0;

	opterr=0;																	// Disable lib error msg's

	while((arg=getopt(argc, argv, ":ht:v")) != -1 && !res) {
		switch(arg) {
			case 'h':
				printf("Usage: rpiburn [options]\n");
				printf("High power load testing of Raspberry Pi while ");
				printf("monitoring system for anomalies.\n");
				printf("\n");
				printf("    -h          This help\n");
				printf("    -t <msec>   Run test for <msec> milliseconds.\n");
				printf("    -v          Display program version and copyrights\n");
				res = -1;
				break;

			case 't':
				errno = 0;
				arg = strtol(optarg, NULL, 10);
				if(errno || arg < 1 || arg > MAX_TOT_TIME) {
					fprintf(stderr, "Error, invalid time argument\n");
					res = -1;
				}
				else {
					load_time = arg;
					tot_time = arg * 2 + 3000;									// Is divided by two later and add extra
				}																//  marging for hung task timeout.
				break;

			case 'v':
				printf("rpiburn %s\n", progVer);
				printf("Copyright (C) 2014-2017 Ronny Nilsson, ");
				printf("2013 Siarhei Siamashka\n");
				res = -1;
				break;

			case ':':															// Errorhandling
				fprintf(stderr, "Error, missing option ");						//  missing options.
				if(isprint(optopt)) fprintf(stderr, "to -%c", optopt);
				fprintf(stderr, "\n");
				res = -1;
				break;

			default:															// Errorhandling
				fprintf(stderr, "Error, unknown option ");						//  unknown options.
				if(isprint(optopt)) fprintf(stderr, "-%c", optopt);
				fprintf(stderr, "\n");
				res = -1;
				break;
		}
	}

	return res;
}



//-------------------------------------------------------------
// Read and write all our file descriptors
static int ioExchange(void) {
	struct timeval timeout;
	fd_set rfds, wfds;
	int highFd, res;

	res = 0;
	highFd = -1;
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	timeout.tv_sec = ioSleep / 1000;
	timeout.tv_usec = (ioSleep % 1000) * 1000;

	assert(ioSleep >= 0);
	if(do_exit) ioSleep = 0;

	if(sigFd >= 0 && sigFd < FD_SETSIZE) {
		FD_SET(sigFd, &rfds);
		if(sigFd > highFd) highFd = sigFd;
	}

	fflush(NULL);

	if(highFd >= 0) {															// Any reader or write active?
		res = select(highFd + 1, &rfds, &wfds, NULL, &timeout);
	}
	else if(timeout.tv_sec || timeout.tv_usec) {
		res = select(0, NULL, NULL, NULL, &timeout);							// Just sleep for a while
	}

	ioSleep = 5000;																// Reset sleep time			
	if(update_current_time()) return -1;

	if(res == -1) {
		if(errno == EINTR) return 0;
		perror("Error on select()");
		return -1;
	}
	else if(res == 0) {
		return 0;
	}

	/* Any posix signal waiting? We need to handle them
	 * before collecting any child exit status to
	 * prevent race conditions. */
	if(sigFd >= 0 && sigFd < FD_SETSIZE && FD_ISSET(sigFd, &rfds)) {
		res = signal_manager();
	}
	
	return 0;
}



//-------------------------------------------------------------
// The main
int main(int argc, char *argv[]) {
	struct timespec hungTimer;
	int res;

 	res = 0;
	do_exit = 0;
	tot_time = DFLT_TOT_TIME;
	if(!res) update_current_time();
	if(!res) res = signal_init();
	if(!res) res = parse_args(argc, argv);
	if(!res) res = vchiq_init();
	if(!res) res = high_load_init();

	// Main loop
	timer_set(&hungTimer, tot_time / 2);
	while(!res && !do_exit) {
		/* Use a timer so we don't hang here
		 * forever in case of a bug. */
		if(timer_timeout(&hungTimer)) res = -1;
		if(!res) maxSleep(timer_remaining(&hungTimer));

		if(!res) res = vchiq_manager();
		if(hasBrownOut()) do_exit = 1;
		if(isHeated()) do_exit = 1;
		if(!res) res = high_load_manager();
		if(!res) res = ioExchange();
	}
	
	/* When time to exit, wait for all childrens to die.
	 * Ignore errors, but use a timer so we don't
	 * hang here forever in case of a bug. */
	do_exit = 1;
	timer_set(&hungTimer, tot_time / 2);
	while(isAnyChildAlive() && !timer_timeout(&hungTimer)) {
		high_load_manager();
		maxSleep(timer_remaining(&hungTimer));
		ioExchange();
	}

	kill_remaining_childs();
	vchiq_close();

	if(hasBrownOut()) {
		printf("Warning, PSU brownout!\n");
		return 30;																// Same as SIGPWR
	}
	else if(isHeated()) {
		printf("Warning, overheated!\n");
		return 70;
	}
	else if(res) {
		return EXIT_FAILURE;
	}
	else {
		printf("PSU OK\n");
		return EXIT_SUCCESS;
	}
}

