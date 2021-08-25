/*****************************************************************************
* | File      	:   wbeke-ctrl.c
* | Author      :   erland@hedmanshome.se
* | Function    :   Westerbeke Marine Generator Starter and Monitor
* | Info        :   Take relay control over the Start, Preheat and Stop switches
* | Depends     :   Rasperry Pi Pico, Waveshare Pico LCD 1.14 V1
*----------------
* |	This version:   V1.0
* | Date        :   2021-08-22
* | Info        :   Build context is within the Waveshare Pico SDK c/examples
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
#include <stdarg.h>
#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include "EPD_Test.h"
#include "LCD_1in14.h"
#include "wb50bcd.h"
#include "wbeke-ctrl.h"

/**
 * For debug purposes this app enters flash
 * mode when the reRun button is pressed.
 */
#define FLASHMODE false

/**
 * Monitor the Genertator run state either from
 * logic gpio level or by frequency measurement.
 */
#define DIRECT_HZ

#ifdef DIRECT_HZ
#include <hardware/pwm.h>
#include <hardware/clocks.h>
#include <pico/multicore.h>
#define HZ_MIN              45      // Adjust to equipment tolerances, typically the charger/inverter.
#define HZ_MAX              65      // Anything outside this band will cause a shutdown of the generator.
#define THZDELTA            2       // Tolerant time window (x*1250ms) to be out of bound for Hz/min-max (RPM drift)
#define FLAG_VALUE          123     // Multicore check flag
#endif

/**
 * Default timing properties for the generator
 */
#define PREHEAT_INTERVAL    20  // Seconds (max)
#define STARTMOTOR_INTERVAL 8   // Seconds (max)
#define RUN_INTERVAL        30  // Minutes
#define EXTRA_RUNTIME       10  // Minutes +/- increments

/**
 * Display properties
 */
#define MAX_CHAR            21
#define MAX_LINES           7
#define FONT                Font16
#define DEF_PWM             50      // Display brightness
#define LOW_PWM             4
#define HDR_OK              GREEN
#define HDR_ERROR           0xF8C0  // Reddish

#define POLLRATE            250     // Main loop interval in ms
#define ON                  1
#define OFF                 0

static UWORD *BlackImage;
static char HdrStr[100]     = { "Header" };
static int HdrTxtColor      = HDR_OK;
static bool FirstLogline    = true;
static bool MonFlag         = false;
static bool RemoteEnable    = false;
static bool RemoteRerun     = false;
static bool RemoteStop      = false;
static bool FirmwareMode    = FLASHMODE;

static const uint PreheatPin =      18; // Relay NO
static const uint StartPin =        19; // Relay NO
static const uint StopPin =         20; // Relay NC
static const uint StopButt =        15; // On LCD PCB
static const uint RerunButt =       17; // On LCD PCB
static const uint AddtimeButt =     2;  // On LCD PCB
static const uint SubtimeButt =     3;  // On LCD PCB
static const uint RtlsbPin =        14; // User DIP switch 0
static const uint RtmsbPin =        26; // User DIP switch 1
static const uint FirmwarePin =     27; // User DIP switch 3
static const uint OffPin =          7;  // On control Panel
static const uint PsuPin =          6;  // Persistent power signal
#ifdef DIRECT_HZ
static const uint HzmeasurePin =    5;  // Square wave 50/60Hz feed
static uint16_t LineFreq =          0;  // Live frequency
#else
static const uint RunPin =          21; // GPIO level logic feed
#endif

/**
 * Format the text header.
 */
static void printHdr(const char *format , ...)
{
    char txt[MAX_CHAR*2];
    va_list arglist;

    va_start(arglist, format);
    vsprintf(txt, format, arglist);
    va_end(arglist);

    txt[MAX_CHAR] = '\0';

    // Center align and trim with white spaces
    int padlen = (MAX_CHAR - strlen(txt)) / 2;
    sprintf(HdrStr, "%*s%s%*s", padlen, "", txt, padlen, "");
  
    Paint_DrawString_EN(4, 0, HdrStr, &FONT, HdrTxtColor, BLACK);

    // Refresh the picture in RAM to LCD
    LCD_1IN14_Display(BlackImage);

}

/**
 * Text display with colored fixed header and scrolled text.
 */
void printLog(const char *format , ...)
{
    static char buf[100];
    va_list arglist;
    va_start(arglist, format);
    vsprintf(buf, format, arglist);
    va_end(arglist);

    int curLine;
    static char lines[MAX_LINES][MAX_CHAR+1];
    if (FirstLogline == true) {
        for (int i=0; i <MAX_LINES; i++) {
            memset(lines[i], 0x0, MAX_CHAR+1);
        }

        FirstLogline = false;
    }

    // Notify any remote client
    atprintf("%s\r\n", buf);

    for (curLine=0; curLine <MAX_LINES; curLine++) {
        if (lines[curLine][0] == (unsigned char)0x0) {
            strncpy(lines[curLine], buf, MAX_CHAR);
            break;
        }

        // Scroll
        if (curLine >= MAX_LINES-1) {
            Paint_Clear(WHITE);
            for (curLine=1; curLine <MAX_LINES; curLine++) {
                strncpy(lines[curLine-1], lines[curLine], MAX_CHAR);
            }
            strncpy(lines[MAX_LINES-1], buf, MAX_CHAR);
        }
    }

    // Print header and text
    for (curLine=0; curLine <MAX_LINES; curLine++) {
            if (curLine == 0) { // Plug in the header first
                Paint_DrawString_EN(4, 0, HdrStr, &FONT, HdrTxtColor, BLACK);
            }
            if (lines[curLine][0] != (unsigned char)0x0) {
                Paint_DrawString_EN(1, (curLine+1)*16, lines[curLine], &FONT, WHITE, BLACK);
            }

    }

    // Refresh the picture in RAM to LCD
    LCD_1IN14_Display(BlackImage);

}

static void clearLog(void)
{
    HdrTxtColor = HDR_OK;
    FirstLogline = true;
    Paint_Clear(WHITE);
}

/**
 * Display initialization.
 * Display: https://www.waveshare.com/wiki/Pico-LCD-1.14 (V1)
 * SDK; https://www.waveshare.com/w/upload/2/28/Pico_code.7z
 */
static int initDisplay(void)
{
    if (DEV_Module_Init() != 0) {
        return -1;
    }

    // LCD Init
    LCD_1IN14_Init(HORIZONTAL);
    LCD_1IN14_Clear(WHITE);

    UDOUBLE Imagesize = LCD_1IN14_HEIGHT*LCD_1IN14_WIDTH*2;

    if((BlackImage = (UWORD *)malloc(Imagesize)) == NULL) {
        return -1;
    }

    // Create a new image cache named IMAGE_RGB and fill it with white
    Paint_NewImage((UBYTE *)BlackImage, LCD_1IN14.WIDTH, LCD_1IN14.HEIGHT, 0, WHITE);
    Paint_SetScale(65);
    Paint_SetRotate(ROTATE_0);

    return 0;
}


/**
 * Check the displays' stop button and the panels' off button.
 */
static bool stopButton(void)
{

    if (gpio_get(StopButt) == OFF) {
        return true;
    }

    if (gpio_get(OffPin) == OFF) {
        /* 
         * Stop engine and let external
         * logic turn every thing off.
         */
        printLog("User off request");
        return true;
    }

    return false;
}

/**
 * Break the generators run circuit.
 * The relay assosiated with the Wbeke
 * control panels' stop switch is connected
 * in serial (NC) with that switch.
 */
static void stopEngine(void)
{
    gpio_put(StopPin, ON);
    sleep_ms(5000); // Let the engine spin down
    gpio_put(StopPin, OFF);
}

/**
 * External circuits ensures that the Pico
 * is not loosing its power during relay
 * controlled operations here.
 * That is, the user cannot turn the
 * Pico off for a while.
 */
static void persistentPsu(int status)
{
    gpio_put(PsuPin, status);
}

/**
 * Add or subtract run-time (+/- 10 min)
 * by means of gpio actions, i.e buttons
 * on the display PCB.
 */
static int addSubTime(void)
{

    if (gpio_get(AddtimeButt) == false) {
        return 1;
    }

    if (gpio_get(SubtimeButt) == false) {
        return 2;
    }

    return 0;
}

/**
 * An external user accesable mini-dip
 * switch block can be used to set default
 * engine run times.
 */
static int getPresetTime(void)
{

    // Read the DIP-switch (two bits)
    unsigned char msb = 0;
    unsigned char lsb = 0;
    unsigned char res = 0;
    int mFact = 1;

    msb = gpio_get(RtmsbPin);
    lsb = gpio_get(RtlsbPin);
    msb <<= 1;
    res = lsb | msb;
    res  = ~res;
    res &= 3;

    if (res > 0) {
        mFact = res+1;
        printLog("Runtime: %d minutes", mFact*RUN_INTERVAL);
    } else {
        printLog("Runtime: %d minutes", RUN_INTERVAL);
    }

    return mFact;
}

/**
 * Initialize all gpio pins here.
 */
static void gpioInit(void)
{
        // Initialize our pins
        gpio_init(PreheatPin);
        gpio_set_dir(PreheatPin, GPIO_OUT);
        gpio_put(PreheatPin, OFF);

        gpio_init(StartPin);
        gpio_set_dir(StartPin, GPIO_OUT);
        gpio_put(StartPin, OFF);

        gpio_init(StopPin);
        gpio_set_dir(StopPin, GPIO_OUT);
        gpio_put(StopPin, OFF);
#ifndef DIRECT_HZ
        gpio_init(RunPin);
        gpio_set_dir(RunPin, GPIO_IN);
        gpio_pull_up(RunPin);
#endif

        gpio_init(StopButt);
        gpio_set_dir(StopButt, GPIO_IN);
        gpio_pull_up(StopButt);

        gpio_init(OffPin);
        gpio_set_dir(OffPin, GPIO_IN);

        gpio_init(RerunButt);
        gpio_set_dir(RerunButt, GPIO_IN);
        gpio_pull_up(RerunButt);

        gpio_init(AddtimeButt);
        gpio_set_dir(AddtimeButt, GPIO_IN);
        gpio_pull_up(AddtimeButt);

        gpio_init(SubtimeButt);
        gpio_set_dir(SubtimeButt, GPIO_IN);
        gpio_pull_up(SubtimeButt);

        gpio_init(RtlsbPin);
        gpio_set_dir(RtlsbPin, GPIO_IN);
        gpio_pull_up(RtlsbPin);

        gpio_init(RtmsbPin);
        gpio_set_dir(RtmsbPin, GPIO_IN);
        gpio_pull_up(RtmsbPin);

        gpio_init(PsuPin);
        gpio_set_dir(PsuPin, GPIO_OUT);
        gpio_put(PsuPin, OFF);

        gpio_init(FirmwarePin);
        gpio_set_dir(FirmwarePin, GPIO_IN);
        gpio_pull_up(FirmwarePin);

}

#ifdef DIRECT_HZ
/**
 * This is a free rinning core1 function.
 * Messure the line frequency (~50/60Hz).
 * An external Closed Type CT (current transformer)
 * can be used to sens the line frequency without
 * any physical intrusion into the the hot
 * power line.
 * This sine wave should then be represented as
 * a square wave stream (around 2.5v peak) to the
 * PWM B pin of the Pico, i.e a Schmittrigger
 * circuit function to feed GP5.
 */
static uint16_t measureFrequency(uint gpio, int pollRate)
{

    uint slice_num;
    static bool init;

    if (init == false) {
        // Only the PWM B pins can be used as inputs.
        assert(pwm_gpio_to_channel(gpio) == PWM_CHAN_B);
        slice_num = pwm_gpio_to_slice_num(gpio);

        // Count once for every cycles the PWM B input is high
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
        pwm_config_set_clkdiv(&cfg, 1.f);   // Set by default, increment count for each rising edge
        pwm_init(slice_num, &cfg, false);   // False means don't start pwm
        gpio_set_function(gpio, GPIO_FUNC_PWM);
        init = true;
    }

    pwm_set_counter(slice_num, 0);

    pwm_set_enabled(slice_num, true);
    sleep_ms(pollRate);
    pwm_set_enabled(slice_num, false);

    return (uint16_t)pwm_get_counter(slice_num) * (1000/pollRate);

}

/**
 * The thread entry for the measureFrequency() function.
 * Be somewhat tolerant for temporary RPM drifts.
 */
static void core1Thread(void)
{

    int retry = THZDELTA;
    int8_t byte = 0;

    multicore_fifo_push_blocking(FLAG_VALUE);

    uint32_t g = multicore_fifo_pop_blocking();

    if (g == FLAG_VALUE) {

        while(1) {

            if (RemoteEnable == true) { // Allow interaction if stopped
                if (byte = getchar_uart()) {

                    switch (serialChat(byte))
                    {
                        case 1:
                            RemoteStop = true;
                        break;
                        case 2:
                            RemoteRerun = true;
                        break;
                        default:
                        break;
                    }
                    continue;
                }
                sleep_ms(4);
            }

            int f = measureFrequency(HzmeasurePin, 1000);

            if (f > HZ_MAX || f < HZ_MIN) {
                if (retry-- >= 0) {   // Hz drift handling for whatever reason
                    //printLog("drift=%d/%d", retr, f);
                    sleep_ms(250);
                    continue;
                }
            }
            retry = THZDELTA;

            LineFreq = f;   // Enter result to global space
        }

    } else {
            printLog("Cannot run core1");
    }

}

#endif


/**
 * Check if Wbeke is operational either by means of
 * RPM/Frequency checks or check an gpio pin with
 * external circuitry for the same purpose.
*/
static bool wbekeIsRunning(int pollRate)
{

    sleep_ms(pollRate);

#ifdef DIRECT_HZ
    if (LineFreq > HZ_MAX || LineFreq < HZ_MIN) {
        if (MonFlag == true) {
            printLog("Out of Hz band f=%d", LineFreq);
        }
        return false;
    }

    return true;

#else
    return gpio_get(RunPin);
#endif

}

/**
 * Main control loop that maneuvers three external relays
 * (start/stop/preheat) that overrides the Wbeke panel
 * switches, but does not disable the Wbeke panel.
 * Also monitor the run state of the generator.
 * Pre-heating and engine cranking should never
 * occur if the engine, for whatever reason,
 * already is running.
 */
static void wbekeCtrlRun(bool reRun)
{
    int runFlag;
    int mFact = 1;
    int reTry = 0;
    int preHeatInterval = PREHEAT_INTERVAL;

    if (reRun == false) {
        if (initDisplay() != 0) {
            return;
        }
        gpioInit();
    }

    HdrTxtColor = HDR_OK;
    FirstLogline = true;
    RemoteRerun = false;
    RemoteEnable = true;
    RemoteStop = false;  

    DEV_SET_PWM(DEF_PWM);

    // Splash screen
    Paint_DrawImage(wb50bcd,0,0,240,135);
    Paint_DrawString_EN(2, 118, VERSION , &FONT, WHITE, BLACK);
    Paint_DrawString_EN(194, 118, GTYPE , &FONT, WHITE, BLACK);
    LCD_1IN14_Display(BlackImage);

#ifdef DIRECT_HZ
    if (reRun == false) {
        multicore_launch_core1(core1Thread);

        // Wait for it to start up
        uint32_t g = multicore_fifo_pop_blocking();

        if (g != FLAG_VALUE) {
            HdrTxtColor = HDR_ERROR;
            printLog("%d-%d Hz sens FAILED");
            while(1) sleep_ms(2000);
        } else {
            multicore_fifo_push_blocking(FLAG_VALUE);
            sleep_ms(2000);
        }
        // Initialize a client chat (full)
        serialChatInit(true);
    } else {
        // Initialize a client chat (limited)
        serialChatInit(false);
    }


#if 0
    while(1) {
        sleep_ms(250);
        printLog("LineFreq=%d", LineFreq);
    }
#endif

#endif

    for (int i=0; i < 16; i++) {    // Allow abort
        if (gpio_get(StopButt) == false || RemoteStop == true) {
            Paint_Clear(WHITE);
            HdrTxtColor = HDR_ERROR;
            printHdr("User abort");
            printLog("Start aborted!");
            return;
        }
        sleep_ms(250);
    }

    Paint_Clear(WHITE);

    // Leave PSU control to panel buttons
    persistentPsu(OFF);

    printHdr("%s Generator Start", GTYPE);

#ifdef DIRECT_HZ
    printLog("%d-%d Hz sens started", HZ_MIN, HZ_MAX);
#endif

#if 0
    for (int i = 0; i < MAX_LINES; i++)
    {
        printLog("line = %d", i);   // Scroll test
    }
    sleep_ms(6000);
#endif



#if 1
    if (wbekeIsRunning(POLLRATE)) {
        printLog("Line power already");
        printLog("present.");
        return;
    }
#endif

    // We have control over Picos' power (not control panel buttons)
    persistentPsu(ON);

    stopEngine();   // Always be sure engine is stopped before using preheater and cranker

    // Get preset runtime
    mFact = getPresetTime();

    runFlag = 3;    // retry

    atprintf("** remote input disabled during engine runtime **\r\n");
    RemoteEnable = false;

    while(runFlag-- > 0 && !wbekeIsRunning(POLLRATE)) {

        printHdr("Start Attempt %d/3", ++reTry);

        printLog("Preheat: %d seconds", preHeatInterval);
        gpio_put(PreheatPin, ON);

        for (int i=0; i < preHeatInterval*4; i++) {
            if (stopButton()) {
                runFlag = -2;
                break;
            }
            sleep_ms(250);
        }

        if (runFlag == -2) {
            printLog("Stop preheater now");
            gpio_put(PreheatPin, OFF);
            break;
        }

        preHeatInterval /=2;

        printLog("Cranker: %d seconds", STARTMOTOR_INTERVAL);
        gpio_put(StartPin, ON);

        for (int i=0; i < STARTMOTOR_INTERVAL*4; i++) {

            if (stopButton() || wbekeIsRunning(250) ) {
                break;
            }
        }

        printLog("Stop cranker now");
        gpio_put(StartPin, OFF);
        printLog("Stop preheater now");
        gpio_put(PreheatPin, OFF);

        if (stopButton()) {
            runFlag = -2;
            break;
        }

        printLog("Is %s running?", GTYPE);
        sleep_ms(2000);
        if (wbekeIsRunning(POLLRATE)) {
            printLog("%s is running!", GTYPE);
            runFlag = 0;
            break;
        } else if (runFlag > 0) {
            printLog("No. Pause and retry!");
            // The generator may run but not at proper Hz. Make sure the diesel is stopped before retry.
            stopEngine();
            for (int i=0; i < 100; i++) {
                sleep_ms(250);
                if (stopButton()) {
                    runFlag = -2;
                    break;
                }
            }
            clearLog();

            if (runFlag == -2) {
                break;
            }
        } else {
            runFlag = -1;
            break;
        }
    }

    if (runFlag < 0) {
        HdrTxtColor = HDR_ERROR;
        if (runFlag == -1) {
            printHdr("Start Failed!");
            printLog("3 attempts failed");
        } else if (runFlag == -2) {
            printLog("User aborted start");
        }
    } else {
        clearLog();
        printHdr("Runtime monitoring");
        runFlag = (RUN_INTERVAL*60)*mFact;
        int lc = 0;
        int pollRate = 1000/POLLRATE;
        printLog("Runtime: %d minutes", runFlag/60);
        runFlag *= pollRate;

        while(runFlag-- > 0) {

            MonFlag = true;

            int timeAdj = addSubTime();

            if (timeAdj == 1) {
                printLog("%d minutes added", EXTRA_RUNTIME);
                runFlag += EXTRA_RUNTIME*(60*pollRate);
            } else if (timeAdj == 2) {
                printLog("%d minutes subtracted", EXTRA_RUNTIME);
                runFlag -= EXTRA_RUNTIME*(60*pollRate);
                if (runFlag < 0) runFlag = 0;
            }

            if ( timeAdj > 0) { // Force log update
                lc = 60*pollRate;
            }

            if (!wbekeIsRunning(POLLRATE) || stopButton()) {
                HdrTxtColor = HDR_ERROR;
                printHdr("Premature stop");
                printLog("Monitoring stopped");
                runFlag = -2;
                break;
            }

            if (lc++ > 60*pollRate) {
                lc = 0;
                printLog("Time left: %d minutes", ((runFlag/pollRate) / 60)+1);
            }
#ifdef DIRECT_HZ
            if (lc > 3*pollRate) {
                static int lastHz;
                if (lastHz != LineFreq) {
                    printHdr("Monitoring@%dHz",LineFreq);
                    lastHz = LineFreq;
                }
            }
#endif
        }

        MonFlag = false;

        if (runFlag == -1) {
            printHdr("Runtime expired");
            printLog("Monitoring stopped");
        }

    }

    stopEngine();
    // Leave PSU control to control panel buttons
    persistentPsu(OFF);
}

/**
 * This is the "main" entry
 */
void wbeke_ctrl(void)
{
    bool reRun = false;
    int tmo;

    while (1) {
        wbekeCtrlRun(reRun);
        reRun = true;
        tmo = 16;
        RemoteEnable = true;
        atprintf("** remote input enabled **\r\n");

        while (gpio_get(RerunButt)) {

            sleep_ms(250);

            if (RemoteRerun == true) {
                break;
            }

#ifdef DIRECT_HZ
            if (LineFreq > 10) {
                // Manually (re)started from wbekes' panel.
                DEV_SET_PWM(DEF_PWM);
                static int lastHz;
                if (lastHz != LineFreq) {
                    HdrTxtColor = HDR_OK;
                    printHdr("Passive monitoring");
                    printLog("Line frquency is %dHz", LineFreq);
                    lastHz = LineFreq;
                }
            } else if (tmo-- <= 0) {
                DEV_SET_PWM(LOW_PWM);
            }

#else
            if (gpio_get(RunPin) == true) {
                DEV_SET_PWM(DEF_PWM);
                HdrTxtColor = HDR_OK;
                printHdr("Passive monitoring");
                printLog("Generator running #%d", tmo);
            } else if (tmo-- <= 0) {
                DEV_SET_PWM(LOW_PWM);
            }
#endif
            if (gpio_get(StopButt) == false || gpio_get(AddtimeButt) == false || gpio_get(SubtimeButt) == false) {
                DEV_SET_PWM(DEF_PWM);   // React to user activity
                tmo = 16;
            }
        }

#ifdef DIRECT_HZ
        serialChatRestart(false);
#endif

        if (gpio_get(FirmwarePin) == 0 || FirmwareMode == true) {
            // Enter rom boot mode and await new firmware
            reset_usb_boot(0,0);
        }
    }
}

