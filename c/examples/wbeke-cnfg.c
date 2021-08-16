#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pico/stdlib.h>
#include <hardware/uart.h>
#include <hardware/irq.h>
#include "wbeke-ctrl.h"

/**
 * WiFi module ESP8266 parser functions.
 * This is a rudimentary telnet server tested against Linux
 * telnet cients.
 * The client is expected to run in the “old line by line” mode
 * and thus edited complete lines are sent to this server.
 * Check the man page for telnet on linux.
 * Note that the linux telnet client will figure out this
 * mode by itself, so the telnet client needs only the
 * I.P address as the sole command line argument.
 */

#define BUFZ    2048

#define NELEMS(x)  (sizeof(x) / sizeof((x)[0])) 

/**
 * Some telnet protocol magics
 */
#define IAC                255     /* interpret as command: */
#define DONT               254     /* you are not to use option */
#define DO                 253     /* please, you use option */
#define WONT               252     /* I won't use option */
#define WILL               251     /* I will use option */
#define TELOPT_ECHO        1       /* echo */
#define TELOPT_LINEMODE    34      /* local line editing */

/**
 * We are using uart Rx interrupt privately 
 * since higher level i/o functions are far to slow
 * to keep up with ESP8266  response strings.
 */
#define UART_ID     uart0
#define BAUD_RATE   115200
#define DATA_BITS   8
#define STOP_BITS   1
#define PARITY      UART_PARITY_NONE
#define UART0_IRQ   20
#define UART1_IRQ   21
// We are using pins 0 and 1, but see the GPIO function select table in the
// datasheet for information on which other pins can be used.
#define UART_TX_PIN 0
#define UART_RX_PIN 1

static uint8_t uartChars[10240];

static int uartIndxOut;

/**
 * RX interrupt handler
 */
void on_uart_rx() {

    uint8_t ch;
    static int uartIndxIn;

    while (uart_is_readable(UART_ID)) {
        ch = uart_getc(UART_ID);
        if (ch > 0x07 && ch < 0x80) {
            uartChars[uartIndxIn++] = ch;
        }

        if (uartIndxIn > sizeof(uartChars)) {
            uartIndxIn = 0;
            memset(uartChars, 0, uartIndxOut);
        }
    }
}

/**
 * Deliver the collected Rx chars to caller.
 */
uint8_t getchar_uart(void)
{
    uint8_t ch = 0;

    if (uartChars[uartIndxOut] > 0) {
        ch = uartChars[uartIndxOut++];
    }

    if (uartIndxOut > sizeof(uartChars)) {
        uartIndxOut = 0;
    }

    return ch;
}

/**
 * See: https://github.com/raspberrypi/pico-examples/blob/master/uart/uart_advanced/uart_advanced.c
 */
static void uartInit()
{
    // Clear our Rx ring buuffer
    memset(uartChars, 0, sizeof(uartChars));

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
        {"0,CLOSED",            "0"},
        {"link is not valid",   "0"},
    };

    static bool status;
    bool newClient = false;

    if (str == NULL) {
        return status;
    }

    for (int i=0; i <NELEMS(cnSts); i++) {
        if (!strcmp(str, cnSts[i][0])) {
            status = (bool)atoi(cnSts[i][1]);
            if (i == 0) newClient = true;
            break;
        }
    }

    // Some basic telnet protocol handliing
    if (newClient == true) {

        uint8_t iac1[] = {IAC, WILL, TELOPT_LINEMODE};
        uint8_t iac2[] = {IAC, DO, TELOPT_ECHO};

        printf("AT+CIPSEND=0,%d\r\n", sizeof(iac1));
        sleep_ms(50);
        uart_write_blocking(UART_ID, iac1, sizeof(iac1));

        printf("AT+CIPSEND=0,%d\r\n", sizeof(iac2));
        sleep_ms(50);
        uart_write_blocking(UART_ID, iac2, sizeof(iac2));

        sleep_ms(50);  

        newClient == false;    
    }

    return status;
}

/**
 * A printf style readable strings for connected clients.
 */
void atprintf(const char *format , ...)
{
    char txt[256];
    size_t len;
    va_list arglist;

    va_start(arglist, format);
    vsprintf(txt, format, arglist);
    va_end(arglist);

    len = strlen(txt);

    sleep_ms(130);  // Avoid ESP8266 busy feedback

    if (len > 0 && len < sizeof(txt) && checkConnection(NULL) == true) {

        printf("AT+CIPSEND=0,%d\r\n", len);
        sleep_ms(40);
        printf("%s", txt);
        stdio_flush();
    }
}

/**
 * First time initialization (and restart).
 * The port used is 23, i.e. as a telnet server.
 */
void serialChatInit(bool how)
{
    if (how == true) {
        uartInit();
    }

    printf("AT\r\n");
    sleep_ms(15);
    printf("ATE0\r\n");
    sleep_ms(15);
    printf("AT+CIPMUX=1\r\n");
    sleep_ms(15);
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
        (void)checkConnection("0,CLOSED");
        printf("AT+CIPCLOSE=0\r\n");
    }
    sleep_ms(100);
}

/**
 * Connection restart if app reboots.
 */
void serialChatRestart(void)
{
    closeConnection();
    sleep_ms(100);
    printf("AT+RST\r\n");
    sleep_ms(3000);
    serialChatInit(false);
}

/**
 * Command array with help strings.
 */
static const char *userCmds[][3] = {
    {"help",        "0",    "this message"},
    {"stop",        "1",    "stop the initial start sequence"},
    {"rsts",        "2",    "restart the system"},
    {"quit",        "3",    "close this connection"},
    {"getip",       "4",    "ip address for STA and AP"},
    {"scan",        "5",    "scan WiFi neighborhood"},
    {"join",        "6",    "join AP <ssid> <pwd>"},
    {"cjoin",       "7",    "commit join WiFi"},
};

enum userActions {
    HELP,
    STOP, 
    RSTS,
    QUIT,
    GETIP,
    SCAN,
    JOIN,
    CJOIN,
    NOACT
};

/**
 * Print help messages.
 */
static void doHelp()
{
    for (int i=0; i <NELEMS(userCmds); i++) {
        atprintf(" %s:\t%s\r\n", userCmds[i][0], userCmds[i][2]);
    }
} 

/**
 * Parse the user commands and execute them accordingly.
 */
static int parseCommand(char* str)
{
    char *ptr = str;
    int action = NOACT;
    int rval = 0;

    if (!strncmp("+IPD,0,", str, 7)) {
        for (int i=7; i <10; i++) {
           if (str[i] == ':') {
                ptr = &str[i+1];
                break; 
            }
        }       
    }

    // Found garbage or an in lead to a command?
    if (*ptr < 0x20 || ptr == str) {
        return -1;
    }

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

        case HELP:      rval = action;
                        doHelp();
                    break;
        case STOP:      rval = action;
                    break;
        case RSTS:      rval = action;
                        atprintf("restarting system now ...\r\n");
                        serialChatRestart();
                    break;
        case QUIT:      rval = 0;
                        closeConnection();
                    break;
        case GETIP:     rval = 0;
                        printf("AT+CIFSR\r\n");
                    break;
        case SCAN:      rval = 0;
                        printf("AT+CWLAP\r\n");
                    break;
        case JOIN:      rval = 0;
                        atprintf("WARNING:\r\nThis action will restart this service and join another WiFi network.\r\n");
                        atprintf("Type \"cjoin\" to commit network  migration.\r\n");
                        jrval = sscanf( ptr, "%s %s %s", waste, ssid, pwd);
                    break;
        case CJOIN:     rval = 0;
                        if (jrval == 3) {
                            closeConnection();
                            printf("AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
                            sleep_ms(3000);
                            serialChatRestart();
                        } else {
                            atprintf("join: malformed arguments\r\n");
                            memset(ssid, 0, sizeof(ssid));
                            memset(pwd, 0, sizeof(pwd));
                        }
                    break;
        default:        rval = 0;
                        atprintf("%s: Unknown command\r\n", ptr);
                    break; 
    }

    return rval;

}


/**
 * Ignore AT feedback from ESP8266
 */
 static bool atThrow(char *str)
{
    const char *atThrowes[][1] = {
        {"OK"},
        {">"},
        {"SEND"},
        {"Recv"},
        {"ERROR"},
        {"ATE0"},
        {"no change"},
        {"busy"},
    };

    int cnt = strlen(str);

    if (cnt < 5) {
       return true;
    }

    // Be within printable ascii or throw this line
    for (int i = 0; i < cnt; i++) {
        if (str[i] < 0x20 || str[i] > 0x7f) {
            return true;
        }
    }

    // Strings that don't like
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
static bool checkResonse(char *str)
{

    if (!strncmp(str, "+CIFSR:",7)) {
        atprintf("\r\n%s", &str[7]); 
        return true;            
    }

    if (!strncmp(str, "+CWLAP:",7)) {
        atprintf("\r\n%s", &str[7]); 
        return true;            
    }

    return false;
}

/**
 * Main application entry.
 * Collect bytes to a parsable null terminated string.
 * Also present a prompt.
 * Note that (in this application context) this part
 * is running as a core1 thread on the Rp Pico.
 */
int serialChat(uint8_t byte)
{
    static int indx;
    static char str[BUFZ];
    char chr = (char)byte;
    int rval = 0;

    if (chr > 0x7e) {
        return 0;
    }

    if (indx < BUFZ-1) {
        str[indx++] = chr;
    } else {
        indx = 0;
        memset(str, 0, BUFZ);
        return 0;
    }

    if (str[indx-1] == '\r'|| str[indx-1] == '\n') {
        bool throw = true;

        str[indx-1] = '\0';

        if (checkResonse(str) == true) {
            indx = 0;
            memset(str, 0, BUFZ);
            return 0;
        }

        if (checkConnection(str) == true && (throw=atThrow(str)) == false) {
            rval = parseCommand(str);
        }

        if (throw == false) {
            atprintf("\r\n(%s)> ", GTYPE);
        }

        indx = 0;
        memset(str, 0, BUFZ);
    }

    return rval;
}
