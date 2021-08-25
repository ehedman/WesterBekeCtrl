#ifndef _WBEKECTRL_H_
#define _WBEKECTRL_H_

/**
 * Version string in splash screen
 */
#define VERSION "V1.0"
#define GTYPE   "BCD"

extern void printLog(const char *format , ...);
extern void serialChatInit(bool how);
extern void serialChatRestart(bool full);
extern int serialChat(uint8_t byte);
extern void atprintf(const char *format , ...);
extern uint8_t getchar_uart(void);

#endif
