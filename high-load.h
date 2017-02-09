
#ifndef HIGH_LOAD_H
#define HIGH_LOAD_H


//-------------------------------------------------------------
int load_time;																	// Number of milliseconds we run with full load


//-------------------------------------------------------------
int high_load_init(void);
int isAnyChildAlive(void);
int kill_remaining_childs(void);
int high_load_manager(void);

#endif
