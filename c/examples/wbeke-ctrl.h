#ifndef _WBEKECTRL_H_
#define _WBEKECTRL_H_

#if __has_include("wbekectrl.h")
 #include "wbekectrl.h"
#else
 #error Run cmake to create wbekectrl.h
#endif

#define GTYPE   "BCD"

extern void printLog(const char *format , ...);
extern void serialChatInit(bool how);
extern void serialChatRestart(bool full);
extern int serialChat(uint8_t byte);
extern void atprintf(const char *format , ...);
extern uint8_t getchar_uart(void);

#endif
