/*****************************************************************************
* | File      	:   wbeke-cnfg.c
* | Author      :   erland@hedmanshome.se
* | Function    :   Westerbeke Marine Generator Starter and Monitor
* | Info        :   This is the telnet port of the application
* | Depends     :   Rasperry Pi Pico, WiFi module ESP8266
*----------------
* |	This version:   V1.0
* | Date        :   2021-08-22
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pico/stdlib.h>
#include <hardware/uart.h>
#include <hardware/irq.h>
#include "wbeke-ctrl.h"

/**
 * WiFi module ESP8266 command parser section.
 * This is a rudimentary single session telnet server tested
 * against a Linux client, an Android client and putty on windows.
 * The client will run in either line mode or char-by-char mode.
 * If line mode is not set, the system will work with a slight
 * performance degradation in feedback response due to the fat 
 * AT+ protocol of the ESP8266 that appears rather Halv-duplex 
 * in its behaviour.
 * Turn on the clients local echo function in such cases.
 * NOTE: For debugging purposes, the "printLog(char *)"
 * function can be used here, and those strings appears on the
 * 1.4" display connected to the Rp Pico in this app.
 */

/*
 * Fixed AP params
 */
#define BCDAPNAME           "bcd-50"
#define BCDAPPWD            "12345bcd"
#define CIPAP               "192.168.4.3"

#define BUFZ                2048
#define ATSENDSZ            256

/**
 * Some telnet protocol magics
 */
#define IAC                 255     // interpret as command:
#define DONT                254     // you are not to use option
#define DO                  253     // please, you use option
#define WONT                252     // I won't use option
#define WILL                251     // I will use option
#define TELOPT_ECHO         1       // echo
#define TELOPT_SGA          3       // suppress go ahead
#define TELOPT_TTYPE        24      // terminal type
#define TELOPT_NAWS         31      // window size
#define TELOPT_TSPEED       32      // terminal speed
#define TELOPT_LFLOW        33      // remote flow control
#define TELOPT_LINEMODE     34      // local line editing

#define NELEMS(x)  (sizeof(x) / sizeof((x)[0])) 

/**
 * We are using uart Rx interrupt privately 
 * since higher level i/o functions are far to slow
 * to keep up with ESP8266 response strings at 115200.
 */
#define UART_ID     uart0
#define BAUD_RATE   115200
#define DATA_BITS   8
#define STOP_BITS   1
#define PARITY      UART_PARITY_NONE
#define UART0_IRQ   20
#define UART1_IRQ   21

/**
 * We are using pins 0 and 1, but see the GPIO function select table in the
 * datasheet for information on which other pins can be used.
 */
#define UART_TX_PIN 0
#define UART_RX_PIN 1

/**
 * Global i.o properties
 */
typedef struct {
    uint8_t uartChars[10240];
    uint8_t iacBuf[256];
    int uartIndxOut;
    int uartIndxIn;
    int iacIndx;
    bool lineMode;
    bool doEcho;
} serial;
static serial io;

/**
 * RX interrupt handler
 */
void on_uart_rx() {

    uint8_t ch;
    static int iacCnt;
    static int chCnt;

    while (uart_is_readable(UART_ID)) {
        ch = uart_getc(UART_ID);

        if (ch == IAC || iacCnt > 0) {  // Collect three bytes IACs from the client
            io.iacBuf[chCnt++] = ch;
            if (++iacCnt > 2) {
                iacCnt = 0;
                io.iacIndx++;
                continue;
            }
            continue;
        }

        chCnt = 0;

        if (ch > 0x07 && ch < 0x80) {
            io.uartChars[io.uartIndxIn++] = ch;
        }

        if (io.uartIndxIn > sizeof(io.uartChars)) {
            io.uartIndxIn = 0;
            memset(io.uartChars, 0, io.uartIndxOut);
        }
    }
}

/**
 * Deliver the collected Rx chars to caller.
 */
uint8_t getchar_uart(void)
{
    uint8_t ch = 0;
    static bool init;

    if (init == false) {
        memset(io.uartChars, 0, sizeof(io.uartChars));
        init = true;   
        io.uartIndxIn = io.uartIndxOut = 0; 
        return 0;
    }

    if (io.uartChars[io.uartIndxOut] > 0) {
        ch = io.uartChars[io.uartIndxOut++];
    }

    if (io.uartIndxOut > sizeof(io.uartChars)) {
        io.uartIndxOut = 0;
    }

    return ch;
}

/**
 * See: https://github.com/raspberrypi/pico-examples/blob/master/uart/uart_advanced/uart_advanced.c
 */
static void uartInit()
{
    // Clear our Rx ring buuffer
    memset(io.uartChars, 0, sizeof(io.uartChars));

    // Set up our UART with a basic baud rate.
    uart_init(UART_ID, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // Actually, we want a different speed
    // The call will return the actual baud rate selected, which will be as close as
    // possible to that requested
    int __unused actual = uart_set_baudrate(UART_ID, BAUD_RATE);

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(UART_ID, false, false);

    // Set our data format
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(UART_ID, false);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);
}

/**
 * Monitor the connection state.
 */
static bool checkConnection(char* str)
{

    const char *cnSts[][2] = {
        {"0,CONNECT",           "1"},
        {"1,CONNECT",           "0"},
        {"0,CLOSED:",           "0"},
        {"link is not valid",   "0"},
    };

    static bool connected;
    bool newClient = false;
    bool goneClient = false;
    bool badClient = false;

    if (str == NULL || !strlen(str)) {
        return connected;
    }

    for (int req=0; req <NELEMS(cnSts); req++) {
        if (!strcmp(str, cnSts[req][0])) {
            if (req == 1)   {badClient = true; break;}
            connected =     (bool)atoi(cnSts[req][1]);
            if (req == 0)   newClient = true;
            if (req > 1)    goneClient = true;
            break;        
        }
    }

    if (badClient == true) {
        int len;
        char *badc = "An active session is already ongoing!\r\n";
        len = strlen(badc);
        printf("AT+CIPSEND=1,%d\r\n", len);
        sleep_ms(40);
        uart_write_blocking(UART_ID, badc, len);
        sleep_ms(2000);
        printf("AT+CIPCLOSE=1\r\n");
        return false;
    }

    if (newClient == true || goneClient == true) {
        // Defaults
        io.lineMode = false;
        io.doEcho = false;
    }

    // Telnet protocol handliing: "Client said"
    if (newClient == true && io.iacIndx > 0) {

        int next = 0;

        while (io.iacIndx-- > 0) {

            bool wont = false;
            bool dont = false;
            bool doit = false;
            bool will = false;

            for (int i =0+next; i < 3+next; i++) {
                switch (io.iacBuf[i]) {
                    case IAC:               break;
                    case WONT:              wont =      true;       break;
                    case DONT:              dont =      true;       break; 
                    case WILL:              will =      true;       break; 
                    case DO:                doit =      true;       break; 
                    case TELOPT_LINEMODE:   if (will == true)
                                            {io.lineMode = true;}   break;
                    case TELOPT_ECHO:       if (wont == true)
                                            {io.doEcho = true;}
                                            else if (doit == true)
                                            {io.lineMode = true;}   break; // ?? fix for putty
                    default:  break;
                }
            }
            next += 3;
        }     
    }

    // Telnet protocol handliing: "tell client"
    if (newClient == true && io.lineMode == false) {
        uint8_t iac[] = {IAC, WONT, TELOPT_ECHO};  // Avoid echo chars here
        printf("AT+CIPSEND=0,%d\r\n", sizeof(iac));
        sleep_ms(50);
        uart_write_blocking(UART_ID, iac, sizeof(iac));
        sleep_ms(100);
    }

    if (newClient == true) {
        newClient = false;
        io.iacIndx = 0;
    }

    return connected;
}

/**
 * Output a printf style readable strings for connected clients.
 */
void atprintf(const char *format , ...)
{
    char txt[ATSENDSZ*2];
    size_t len;
    va_list arglist;

    va_start(arglist, format);
    vsprintf(txt, format, arglist);
    va_end(arglist);

    len = strlen(txt);

    if (len > 0 && len < sizeof(txt)/2 && checkConnection(NULL) == true) {
        sleep_ms(200);  // Avoid ESP8266 busy feedback
        printf("AT+CIPSEND=0,%d\r\n", len);
        sleep_ms(40);
        uart_write_blocking(UART_ID, txt, len);
        sleep_ms(40);
    }
}

/**
 * First time initialization (and restart).
 * The IP port used is 23, i.e. as a telnet server.
 */
void serialChatInit(bool how)
{
    if (how == true) {
        uartInit();
    }

    printf("AT\r\n");
    sleep_ms(100);
    printf("ATE0\r\n");
    sleep_ms(100);
    printf("AT+CWMODE=3\r\n");
    sleep_ms(100);
    printf("AT+CWSAP=\"%s\",\"%s\",5,3\r\n", BCDAPNAME,  BCDAPPWD);
    sleep_ms(100);
    printf("AT+CIPAP=\"%s\"\r\n", CIPAP);
    sleep_ms(200);
    printf("AT+CWDHCP=1,1\r\n");
    sleep_ms(200);
    printf("AT+CIPMUX=1\r\n");
    sleep_ms(200);
    printf("AT+CIPSERVER=1,23\r\n");
    sleep_ms(1000);
}

/**
 * Force connection closure
 */
static void closeConnection(void)
{
    sleep_ms(100);
    if (checkConnection(NULL) == true) {
        (void)checkConnection("0,CLOSED:");
        printf("AT+CIPCLOSE=0\r\n");
    }
    sleep_ms(100);
}

/**
 * Connection restart if app reboots.
 */
void serialChatRestart(bool full)
{
    closeConnection();
    sleep_ms(100);
    printf("AT+RST\r\n");
    sleep_ms(2000);

    if (full == true) {
        serialChatInit(false);
    }
}

/**
 * Command array with help strings.
 */
static const char *userCmds[][3] = {
    {"help",        "1",    "this message"},
    {"stop",        "2",    "stop the initial start sequence"},
    {"rsts",        "3",    "restart the system"},
    {"quit",        "4",    "close this connection"},
    {"getip",       "5",    "ip address for STA and AP"},
    {"getap",       "6",    "get AP parameters"},
    {"scan",        "7",    "scan WiFi neighborhood"},
    {"join",        "8",    "join AP <ssid> <pwd>"},
    {"cjoin",       "9",    "commit join to new WiFi"},
};

enum userActions {
    HELP = 1,
    STOP, 
    RSTS,
    QUIT,
    GETIP,
    GETAP,
    SCAN,
    JOIN,
    CJOIN,
    NOACT
};

/*
 * Present a prompt with a short delay
 * to avoid ESP8266 busy situations.
 */
static void prompt(int hold)
{
    sleep_ms(hold);
    atprintf("\r\n(%s)> ", GTYPE);
}

/**
 * Print help messages.
 */
static void doHelp()
{
    static char hbuf[NELEMS(userCmds)*50];

    sleep_ms(1000);
    if (!strlen(hbuf)) {
        for (int i=0; i <NELEMS(userCmds); i++) {
            strcat(hbuf, userCmds[i][0]);
            strcat(hbuf, "\t");
            strcat(hbuf, userCmds[i][2]);
            strcat(hbuf, "\r\n");
            if (strlen(hbuf) >= ATSENDSZ) {
                hbuf[ATSENDSZ] = '\0';
                break;
            }
        }
    }
    atprintf("%s", hbuf);
    prompt(400);
}

/**
 * Parse the user commands and execute them accordingly.
 */
static int parseCommand(char* str)
{
    char *ptr = str;
    int action = NOACT;
    int rval = 0;

    if (!strlen(str)) {
        return -1;
    }

    if (io.lineMode == true) {
        if (!strncmp("+IPD,0,", str, 7)) {
            for (int i=7; i <10; i++) {
               if (str[i] == ':') {
                    ptr = &str[i+1];
                    break; 
                }
            }       
        }

        // Found non printable or an in lead to a command?
        if (ptr == str || *ptr < ' ') {
            return -1;
        }
    }

    // Get the numeric action
    for (int i=0; i <NELEMS(userCmds); i++) {
        if (!strncmp(ptr, userCmds[i][0], strlen(userCmds[i][0]))) {
            action = (int)atoi(userCmds[i][1]);
            break;
        }
    }

    switch(action)
    {
        static char ssid[60];
        static char pwd[60];
        static int jrval;
        char waste[60];

        case HELP:      atprintf("\r\n");
                        doHelp();
                    break;
        case STOP:      rval = 1;   // Stop
                    break;
        case RSTS:      rval = 2;   // Re-boot
                        atprintf("\r\nrestarting system now ...\r\n");
                        closeConnection();
                        serialChatRestart(false);
                    break;
        case QUIT:      closeConnection();
                    break;
        case GETIP:     printf("AT+CIFSR\r\n");
                    break;
        case GETAP:     printf("AT+CWSAP_CUR?\r\n");
                    break;
                        
        case SCAN:      printf("AT+CWLAP\r\n");
                    break;
        case JOIN:      atprintf("\r\nWARNING:\r\nThis action will restart this service and join another WiFi network.\r\n");
                        atprintf("Type \"cjoin\" to commit to the network migration.\r\n");
                        atprintf("If it fails, reconnect to this machines AP:\r\n  ssid = '%s' password = '%s'\r\n", BCDAPNAME,  BCDAPPWD);
                        atprintf("Then telnet to I.P '%s'\r\n", CIPAP);
                        prompt(100);
                        jrval = sscanf( ptr, "%s %s %s", waste, ssid, pwd);
                    break;
        case CJOIN:     if (jrval == 3 && strlen(pwd) >7) {
                            closeConnection();
                            printf("AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
                            sleep_ms(3000);
                            serialChatRestart(true);
                        } else {
                            atprintf("join: malformed arguments\r\n");
                            if (strlen(pwd) < 8) {
                                atprintf("password too short (< 8 chars)\r\n");
                            }
                            memset(ssid, 0, sizeof(ssid));
                            memset(pwd, 0, sizeof(pwd));
                            prompt(100);
                        }
                    break;
        default:        atprintf("%s: Unknown command\r\n", ptr);
                        prompt(200);
                    break; 
    }


    return rval;

}

/**
 * Ignore AT feedback from ESP8266
 */
 static bool atThrow(char *str)
{
    if (strlen(str) < 2) {
        return true;
    }

    const char *atThrowes[][1] = {
        {"OK"},
        {">"},
        {"SEND"},
        {"AT"},
        {"Recv"},
        {"ERROR"},
        {"no change"},
        {"busy"},
    };

    // Strings that we waste
    for (int i=0; i < NELEMS(atThrowes); i++) {
        if (!strncmp(str, *atThrowes[i], strlen(*atThrowes[i]))) {
            return true;
        }
    }

    return false;
}

/**
 * Now print the resolved query and print the results.
 */
static int checkResonse(char *str)
{

    static char buf[200];
    static int cifsrIndx;

    sleep_ms(300);

    // GETIP
    if (!strncmp(str, "+CIFSR:",7)) {  
        strcat(buf, &str[7]);
        strcat(buf, "\r\n");
        if (cifsrIndx++ == 3) {
            atprintf("\r\n%s", buf); 
            cifsrIndx = 0;
            *buf = '\0';
            return 1000;
        }
        return -1;  
        
    } 
    // SCAN
    else if (!strncmp(str, "+CWLAP:",7)) {
        atprintf("\r\n%s", &str[7]);
        sleep_ms(500);
        return -1;
    
    } 
    // GETAP
    else if (!strncmp(str, "+CWSAP_CUR:",11)) {
        atprintf("\r\n%s", &str[11]); 
        return 1000;         
    }

    if (!strncmp("+IPD,0,2:", str, 9)) { // CR/LF
        prompt(1000);
    }

    return  -1;
}

/*
 * Collect bytes to a parsable null command terminated string in single char mode fashion.
 * Also present a prompt.
 */
static int doCharmode(uint8_t byte)
{

    char chr = (char)byte;
    int rval = 0;
    int hold;
    static char inStr[BUFZ];
    static int inIndx;
    static char ctrlStr[BUFZ];
    static char preStr[BUFZ];
    static int ctrlIndx;
    static int preIndx;
    static int plCR;
    static bool inLead;
    static bool plNext;
    static bool conn;
  
    if ((chr >= ' ' && chr < 0x7f) && inIndx == 0) {
        // This char goes to the control buffer
        ctrlStr[ctrlIndx++] = chr;
    }

    if (plCR >0) {
        inLead == false;
        plCR--;
        return 0;
    }
   
    if (chr == '\r' || chr == '\n' ) {
        /* 
         * This is the end of an AT feeback control string 
         * or the begining of a user input string i.e,
         * if the string begins with "\r\n" or ends with it.
         */
        inLead = true;
        memset(preStr, 0, sizeof(preStr));
        plNext = false;
        preIndx = 0;
        plCR = 0;

        if (atThrow(ctrlStr) == true)  {
            ctrlIndx = 0;
            memset(ctrlStr, 0, sizeof(ctrlStr));
            return 0;
        }

        if (strncmp("+IPD,0,", ctrlStr, 4) && chr == '\n' && strlen(ctrlStr)) {
            //printLog("ctrl='%s'", ctrlStr);
            if ((hold=checkResonse(ctrlStr)) >= 0) {
                prompt(hold);
            }
            conn = checkConnection(ctrlStr);
        }

        if (chr == '\n') {
            // Here remains non wasted strings and CR/LF
            if (!strncmp("+IPD,0,2:", ctrlStr, 9)) {
                prompt(300);
            }
            ctrlIndx = 0;
            memset(ctrlStr, 0, sizeof(ctrlStr));
        }

        return 0;
    }

    if (plNext == true) {
        // This single char goes to the payload buffer. Also echo back the char.
        inStr[inIndx++] = chr;
        if (io.doEcho == true) {
            atprintf("%c", chr);
        }
        plNext == false;
        return 0;
    } 

    if (inLead == true) {
        // Got a single char or a CR.
        preStr[preIndx++] = chr;
        if (!strncmp("+IPD,0,1:", preStr, 9)) {
            // Expect nex tchar to be addad the parseCommand() string.
            plNext = true;
            return 0;
        }

        if (!strncmp("+IPD,0,2:", preStr, 9)) {

            if (conn == true && strlen(inStr)) {
                rval = parseCommand(inStr);
            }
            plCR = 1;   // Expect and throw one more CR
            inIndx = 0; 
            memset(inStr, 0, sizeof(inStr));  
        }             
    } 
    
    return rval;
}

/*
 * Collect bytes to a parsable null command terminated string in line mode fashion.
 * Also present a prompt.
 */
static int dolineMode(uint8_t byte)
{
    char chr = (char)byte;
    int rval = 0;
    int hold;
    static char inStr[BUFZ];
    static int inIndx;

    if (chr > 0x7e) {
        return 0;
    }

    if (inIndx < BUFZ-1) {
        inStr[inIndx++] = chr;
    } else {
        inIndx = 0;
        memset(inStr, 0, BUFZ);
        return 0;
    }

    if (inStr[inIndx-1] == '\r'|| inStr[inIndx-1] == '\n') {

        inStr[inIndx-1] = '\0';

        if (atThrow(inStr) == true)  {
            inIndx = 0;
            memset(inStr, 0, BUFZ);
            return 0;
        }

        if ((hold=checkResonse(inStr)) >= 0) {
            prompt(hold);
            inIndx = 0;
            memset(inStr, 0, BUFZ);
            return 0;
        }
        

        if (checkConnection(inStr) == true) {
            rval = parseCommand(inStr);
        }      

        inIndx = 0;
        memset(inStr, 0, BUFZ);
    }

    return rval;
}

/**
 * Main application entry.
 * Note that (in this application context) this part
 * is running as a core1 thread on the Rp Pico.
 */
int serialChat(uint8_t byte)
{
    int rval = 0;

    if (io.lineMode == true) {
        rval = dolineMode(byte);
    } else {
        rval = doCharmode(byte);
    }

    return rval;
}


