
/* Nard Linux SDK
 * http://www.arbetsmyra.dyndns.org/nard
 * Copyright (C) 2014-2017 Ronny Nilsson
 */


#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <regex.h>

#include "misc.h"
#include "main.h"


//-------------------------------------------------------------
#ifndef _POSIX_MONOTONIC_CLOCK
#error Missing monotonic clock support
#endif

#define REGEX_ERR_SIZE		1024


//-------------------------------------------------------------
static char regexErrBuff[REGEX_ERR_SIZE];



//=============================================================
// Setup a timer: now + ms_forw milliseconds in the future
//-------------------------------------------------------------
void timer_set(struct timespec* const t, const int32_t ms_forw) {
	time_t s;
	long ns;

	assert(ms_forw >= 0);
	assert(now.tv_sec);

	if(ms_forw >= 1000) {		
		s = ms_forw / 1000;
		ns = (ms_forw - s * 1000) * 1e6;
	}
	else {
		s = 0;
		ns = ms_forw * 1e6;
	}

	*t = now;
	t->tv_sec += s;
	t->tv_nsec += ns;

	while(t->tv_nsec >= 1e9) {
		t->tv_sec += 1;
		t->tv_nsec -= 1e9;
	}
}



//=============================================================
// Returns the number of millisecons from <now> 
// until the timer <t> expires.
//-------------------------------------------------------------
int32_t _timer_remaining(const struct timespec* const t) {
	int64_t ms;

	assert(now.tv_sec);
	assert(t->tv_sec);

	if(t->tv_sec < now.tv_sec) return 0;

	ms = (int64_t) (t->tv_sec - now.tv_sec) * 1000LL;
	ms += (int64_t) ((t->tv_nsec - now.tv_nsec) / 1e6L);
	
	if(ms > (int64_t) INT32_MAX)
		fprintf(stderr, "Warning, timer_remaining overflow\n");

	return (int32_t) ms;
}



//=============================================================
// Returns the number of millisecons from <now> 
// until the timer <t> expires. Note that 0 is
// never returned! Use timer_timeout() for catching
// the timer expire event!
//-------------------------------------------------------------
int32_t timer_remaining(const struct timespec* const t) {
	int32_t ms;

	ms = _timer_remaining(t);

	if(ms == 0) ms = 1;

	return (int32_t) ms;
}



//=============================================================
// Returns true if the timer <t> has expired
//-------------------------------------------------------------
short timer_timeout(const struct timespec* const t) {
	assert(now.tv_sec);
	if(t->tv_sec == 0) return 1;
	if(t->tv_sec > now.tv_sec) return 0;
	if(t->tv_sec < now.tv_sec) return 1;
	return t->tv_nsec <= now.tv_nsec;
}



//=============================================================
// Delete the timer <t>
//-------------------------------------------------------------
void timer_cancel(struct timespec* const t) {
	t->tv_sec = 0;
	t->tv_nsec = 0;
}



//=============================================================
// Returns the result of time t2 - time t1 in nanoseconds
//-------------------------------------------------------------
int64_t diffntime(struct timespec *t1, struct timespec *t2) {
	int64_t diff;

	diff = (int64_t) (t2->tv_sec - t1->tv_sec) * 1000000000LL;
	diff += t2->tv_nsec;
	diff -= t1->tv_nsec;

	return diff;
}



//=============================================================
// Fetch the current clock from the kernel
//-------------------------------------------------------------
static int _update_current_time(struct timespec *t) {
	int res;

	while((res = clock_gettime(CLOCK_MONOTONIC, t)) == -1 && 
		errno == EINTR);

	if(res == -1) {
		perror("Error, couldn't get monotonic clock");
		return -1;
	}

#ifdef NDEBUG
	assert(0);																	// Test that assert() really has been disabled	
#endif	

	return 0;
}



//=============================================================
// Fetch the current clock from the kernel
//-------------------------------------------------------------
int update_current_time(void) {
	return _update_current_time(&now);
}



//=============================================================
// Register max time in millisecons for
// program to sleep in main loop.
//-------------------------------------------------------------
void maxSleep(const int ms) {
	assert(ms >= 0);
	if(ms >=0 && ms < ioSleep) ioSleep = ms;
}



//=============================================================
// Search for the first text line matching a pattern in a
// string. Works almost like the grep utility, except for no
// substrings and only one (the first) match is returned.
// Args:
//   haystack      Nullterminated large string which will be searched.
//   regExpr       Extended regular expression.
//   matchedBegin  Returned pointer to first char of match.
//   matchedEnd    Returned pointer to last char of match.
// Returns 0 on match and -1 on error or no match.
//-------------------------------------------------------------
int grep(const char *haystack, const char *regExpr, const char **matchedBegin, const char **matchedEnd) {
	regmatch_t match;
	regex_t regCmp;
	int res, i;

	if(matchedBegin) *matchedBegin = NULL;
	if(matchedEnd) *matchedEnd = NULL;

	// "Compile" the regular expression
	memset(&regCmp, 0, sizeof(regCmp));
	res = regcomp(&regCmp, regExpr, REG_EXTENDED | REG_ICASE | REG_NEWLINE);
	if(res) {
		if(regerror(res, &regCmp, regexErrBuff, REGEX_ERR_SIZE) >= 0) {
			printf("Error preparing regex %s: %s\n", regExpr, regexErrBuff);
		}
		return -1;
	}

	// Do search
	memset(&match, 0, sizeof(match));
	res = regexec(&regCmp, haystack, 1, &match, 0);
	if(res == REG_NOMATCH) {
		printf("No grep match\n");
		res = -1;
	}
	else if(res) {
		if(regerror(res, &regCmp, regexErrBuff, REGEX_ERR_SIZE) >= 0) {
			printf("Error grepping regex %s: %s\n", regExpr, regexErrBuff);
		}
		res = -1;
	}
	else {
		//printf("Found match at %d: ", match.rm_so);
		//for(i = match.rm_so; i <= match.rm_eo; i++) putchar(haystack[i]);
		//if(haystack[i-1] != '\n') putchar('\n');
		if(matchedBegin) *matchedBegin = haystack + match.rm_so;
		if(matchedEnd) *matchedEnd = haystack + match.rm_eo;
		res = 0;
	}

	regfree(&regCmp);
	return res;
}
