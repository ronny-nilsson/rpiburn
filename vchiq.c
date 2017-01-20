/*
 * What is VCHIQ?
 * https://www.raspberrypi.org/forums/viewtopic.php?f=43&t=110103
 * http://raspberrypi.stackexchange.com/questions/54571/what-is-dev-vchiq-in-raspberry-pi
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <endian.h>
#include <errno.h>
#include <assert.h>


#include "vchiq.h"
#include "vchiq_ioctl.h"
#include "vchiq_cfg.h"
#include "vc_gencmd_defs.h"
#include "misc.h"


//-------------------------------------------------------------
#define BROWNOUT_POLL_DELAY		45												// Delay in ms between polls of firmware for if we have a brown out
#define MSGBUF_SIZE (VCHIQ_MAX_MSG_SIZE + sizeof(VCHIQ_HEADER_T))


//-------------------------------------------------------------
enum vchiq_state_t {
	R_VCHIQ_INIT,
	R_VCHIQ_VERSION,															// We are about to query firmware for a version
	R_VCHIQ_COMMANDS,															// We are checking if firmware support the commands we need
	R_VCHIQ_BROWNOUT,
};



//-------------------------------------------------------------
static enum vchiq_state_t vchiqState;
static int vchiqFd = -1;
static int isConnected;															// True when has established communicatin with kernel driver
static unsigned int handle = VCHIQ_INVALID_HANDLE;								// Kernel internal ref
static int maxMsgSize;
static char responseBuf[MSGBUF_SIZE];											// Where we receive an answer from firmware
static int responseLen;															// Length of received data
static int responseErr;															// Received error from firmware (if any)
static unsigned int throttVal;													// Lates "throttled" value as recived from firmware
static unsigned int throttSaved;												// Saved "throttled" value as recived from firmware



//-------------------------------------------------------------
int vchiq_init(void) {
	VCHIQ_CREATE_SERVICE_T srvArg;
	VCHIQ_GET_CONFIG_T cnfArg;
	VCHIQ_CONFIG_T config;
	int res;

	vchiqState = R_VCHIQ_INIT;

	vchiqFd = open("/dev/vchiq", O_RDWR);
	if(vchiqFd == -1) {
		perror("Error opening vchiq");
		return -1;
	}


	// Query kernel; has it a VCHIQ version we can use?
	cnfArg.config_size = sizeof(config);
	cnfArg.pconfig = &config;
	res = ioctl(vchiqFd, VCHIQ_IOC_GET_CONFIG, &cnfArg);
	if(res == -1) {
		perror("Error vchiq config");
		return -1;
	}
	else if(config.version < VCHIQ_VERSION_MIN ||
			config.version_min > VCHIQ_VERSION) {
		printf("Error, incompatible vchiq version %d\n", config.version);
		return -1;
	}
	else if(res == 0) {
		//printf("Opened vchqi version %d\n", config.version);
		maxMsgSize = config.max_msg_size;
	}
	else {
		printf("Can't get vchiq config\n");
		return -1;
	}


	// Connect to kernel VCHIQ
	res = ioctl(vchiqFd, VCHIQ_IOC_CONNECT, 0);
	if(res == -1) {
		perror("Error vchiq connect");
		return -1;
	}
	else if(res == 0) {
		//printf("Connected to vchiq\n");
		isConnected = 1;
	}
	else {
		printf("Can't connect to vchiq\n");
		return -1;
	}


	// Create a service. (Why is this necessary?)
	memset(&srvArg, 0, sizeof(srvArg));
	srvArg.params.fourcc = 0x47434d44u;											// FourCC GCMD
	srvArg.params.version = VC_GENCMD_VER;
	srvArg.params.version_min = VC_GENCMD_VER;
	srvArg.is_open = 1;
	srvArg.is_vchi = 1;
	srvArg.handle = VCHIQ_SERVICE_HANDLE_INVALID;
	res = ioctl(vchiqFd, VCHIQ_IOC_CREATE_SERVICE, &srvArg);
	if(res == -1) {
		perror("Error vchiq create service");
		return -1;
	}
	else if(srvArg.handle == VCHIQ_SERVICE_HANDLE_INVALID ||
			srvArg.handle == VCHIQ_INVALID_HANDLE) {
		printf("Error, vchiq service invalid handle\n");
		return -1;
	}
	else if(res == 0) {
		handle = srvArg.handle;
		vchiqState = R_VCHIQ_VERSION;
		maxSleep(0);
		//printf("Service GCMD created with handle %u\n", handle);
	}
	else {
		printf("Can't create vchiq service\n");
		return -1;
	}

	return 0;
}



//-------------------------------------------------------------
// Clean up at exit time
int vchiq_close(void) {
	int res;

	if(vchiqFd == -1) return 0;

	if(handle != VCHIQ_INVALID_HANDLE && handle != VCHIQ_SERVICE_HANDLE_INVALID) {
		res = ioctl(vchiqFd, VCHIQ_IOC_CLOSE_SERVICE, handle);
		if(res == -1) perror("Error vchiq close service");
		handle = VCHIQ_INVALID_HANDLE;
	}

	res = ioctl(vchiqFd, VCHIQ_IOC_SHUTDOWN, 0);
	if(res == -1) perror("Error vchiq shutdown");
	isConnected = 0;

	close(vchiqFd);
	vchiqFd = -1;

	return 0;
}



//-------------------------------------------------------------
// Send a query to firmware
static int vchiq_send_string(const char *msg) {
	VCHIQ_QUEUE_MESSAGE_T arg;
	VCHIQ_ELEMENT_T element;
	int res;

	element.data = msg;
	element.size = strlen(msg) + 1;
	arg.handle = handle;
	arg.elements = &element;
	arg.count = 1;

	assert(vchiqFd >= 0);
	res = ioctl(vchiqFd, VCHIQ_IOC_QUEUE_MESSAGE, &arg);
	if(res == -1) {
		perror("Error sending message");
		return -1;
	}

	return 0;
}



//-------------------------------------------------------------
// Blocking wait for a reply from firmware
static int vchiq_receive_string(void) {
	VCHIQ_DEQUEUE_MESSAGE_T arg;
	int res, errCode;

	memset(responseBuf, 0, MSGBUF_SIZE);
	responseLen = 0;
	responseErr = 0;
	arg.handle = handle;
	arg.blocking = 1;
	arg.bufsize = MSGBUF_SIZE;
	arg.buf = malloc(MSGBUF_SIZE);

	assert(vchiqFd >= 0);
	res = ioctl(vchiqFd, VCHIQ_IOC_DEQUEUE_MESSAGE, &arg);

	if(res == -1) {
		perror("Error reciving message");
		free(arg.buf);
		return -1;
	}
	else if(res == 0) {
		// No data was received
		free(arg.buf);
		return 0;
	}

	/* Copy received data to a global response buffer.
	 * The first word is an error code from the firmware. */
	responseLen = res - sizeof(int);
	memcpy(&errCode, arg.buf, sizeof(int));
	responseErr = le32toh(errCode);
	memcpy(responseBuf, arg.buf + sizeof(int), responseLen);
	responseBuf[sizeof(responseBuf) - 1] = 0;									// Ensure terminating NULL

	return 0;
}



//-------------------------------------------------------------
// Return true if we have a brown out situation
int hasBrownOut(void) {
	/* The bits in firmware throttled value represent:
	 *   0: under-voltage
	 *   1: arm frequency capped
	 *   2: currently throttled
	 *   16: under-voltage has occurred
	 *   17: arm frequency capped has occurred
	 *   18: throttling has occurred
	 * We check a saved value where the bits are only set,
	 * never cleared. */
	return ((throttSaved & 1u) ? 1 : 0);
}



//-------------------------------------------------------------
// Handle VCHIQ state machine. Regularly query the
// firmware for if we have a brown out situation.
int vchiq_manager(void) {
	int res = 0;

	// Send query
	switch(vchiqState) {
		case R_VCHIQ_VERSION:
			if(!res) res = vchiq_send_string("version");
			break;

		case R_VCHIQ_COMMANDS:
			if(!res) res = vchiq_send_string("commands");
			break;

		case R_VCHIQ_BROWNOUT:
			if(!res) res = vchiq_send_string("get_throttled");
			break;

		default:
			return -1;
	}	

	// Blocking wait for response
	if(!res) res = vchiq_receive_string();
	if(res) return res;

	// Parse ASCII response from firmware
	if(strstr(responseBuf, "Broadcom")) {
		vchiqState = R_VCHIQ_COMMANDS;
		maxSleep(0);
		//printf("Got valid firmware version; good.\n");
	}
	else if(strstr(responseBuf, "get_throttled")) {
		vchiqState = R_VCHIQ_BROWNOUT;
		maxSleep(0);
		//printf("Firmware has throttled command; good.\n");
	}
	else if(strstr(responseBuf, "throttled=")) {
		res = sscanf(responseBuf, "throttled=%x", &throttVal);
		if(res == 1) {
			//printf("Trottled value %x\n", throttVal);
			throttSaved |= throttVal;
			res = 0;
		}
		else {
			printf("Error parsing throttled value\n");
			res = -1;
		}
		maxSleep(BROWNOUT_POLL_DELAY);
	}
	else {
		printf("Warning, invalid response from VCHIQ\n");
		res = -1;
	}

	return res;
}


