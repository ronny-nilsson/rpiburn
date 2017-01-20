
#ifndef HIGH_LOAD_H
#define HIGH_LOAD_H

int high_load_init(void);
int isAnyChildAlive(void);
int hasAllChildsStarted(void);
int kill_remaining_childs(void);
int high_load_manager(void);

#endif
