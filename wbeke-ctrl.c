/*****************************************************************************
* | File      	:   wbeke-ctrl.c
* | Author      :   erland@hedmanshome.se
* | Function    :   WesterBeke Marine Generator Starter and Monitor
* | Info        :   Take relay control over the Start, Preheat and Stop switches
* | Depends     :   Rasperry Pi Pico, Waveshare Pico LCD 1.14
*----------------
* |	This version:   V1.0
* | Date        :   2021-07-16
* | Info        :   Build context is within the Waveshare Pico SDK
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

/**
 * For debug purposes this app enters
 * flash mode when the rerun button is pressed.
 */
#define FLASHMODE true

/**
 * Monitor the Genertator run state either from
 * logic gpio level 3.3v or by frequency measurement.
*/
#define DIRECT_50Hz

#ifdef DIRECT_50Hz
#include <hardware/pwm.h>
#include <hardware/clocks.h>
#include <pico/multicore.h>
#define HZ_MIN              45      // Adjust to the 220V equipment tolerances, typically the charger/inverter.
#define HZ_MAX              65      // Anything outside this band will cause a shutdown of the generator.
#define THZDELTA            2       // Tolerant time window (x*1250ms) to be out of bound for Hz/min-max (RPM drift)
#define FLAG_VALUE          123
#endif

#define MAX_CHAR            21
#define MAX_LINES           7
#define PREHEAT_INTERVAL    20
#define STARTMOTOR_INTERVAL 8
#define RUN_INTERVAL        30
#define EXTRA_RUNTIME       10
#define ON                  1
#define OFF                 0
#define DEF_PWM             50
#define POLLRATE            250

static UWORD *BlackImage;
static char HdrStr[100]     = { "Header" };
static int HdrTxtColor      = GREEN;
static bool FirstLogline    = true;
static bool MonFlag         = false;
static bool FlashMode       = FLASHMODE;

static const uint PreheatPin =      18;
static const uint StartPin =        19;
static const uint StopPin =         20;
static const uint RunPin =          21;
static const uint StopButt =        15;
static const uint RerunButt =       17;
static const uint AddtimeButt =     2;
static const uint SubtimeButt =     3;
static const uint RtlsbPin =        26;
static const uint RtmsbPin =        27;
static const uint PsuPin =          28;
static const uint DebugPin =        14;
#ifdef DIRECT_50Hz
static const uint HzmeasurePin =    5;
static uint16_t LineFreq =          0;
#endif

/**
 * Format the text header.
 */
static void printHdr(const char *format , ...)
{
    int len = 0;
    va_list arglist;
    va_start( arglist, format );
    memset(HdrStr, 0, sizeof(HdrStr));
    vsprintf(HdrStr, format, arglist );
    va_end( arglist );

    len = strlen(HdrStr);
    for (;len < MAX_CHAR; len++) {
        HdrStr[len] = ' ';
    }

    Paint_DrawString_EN(4, 0, HdrStr, &Font16, HdrTxtColor, BLACK);
    // Refresh the picture in RAM to LCD
    LCD_1IN14_Display(BlackImage);

}

/**
 * Text display with colored fixed header and scrolled text.
 */
static void printLog(const char *format , ...)
{
    static char buf[100];
    va_list arglist;
    va_start( arglist, format );
    vsprintf(buf, format, arglist );
    va_end( arglist );

    int curLine;
    static char lines[MAX_LINES][MAX_CHAR+1];
    if (FirstLogline == true) {
        for (int i=0; i <MAX_LINES; i++) {
            memset(lines[i], 0x0, MAX_CHAR+1);
        }

        FirstLogline = false;
    }

    printf("%s\r\n", buf);

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

    for (curLine=0; curLine <MAX_LINES; curLine++) {
            if (curLine == 0) { // Plug in the header first
                Paint_DrawString_EN(4, 0, HdrStr, &Font16, HdrTxtColor, BLACK);
            }
            if (lines[curLine][0] != (unsigned char)0x0) {
                Paint_DrawString_EN(1, (curLine+1)*16, lines[curLine], &Font16, WHITE, BLACK);
            }

    }

    // Refresh the picture in RAM to LCD
    LCD_1IN14_Display(BlackImage);

}

/**
 * Display initialization.
 * Display: https://www.waveshare.com/wiki/Pico-LCD-1.14
 * SDK; https://www.waveshare.com/w/upload/2/28/Pico_code.7z
 */
int initDisplay(void)
{
    DEV_Delay_ms(100);
    printf("initDisplay start\r\n");
    if(DEV_Module_Init()!=0){
        return -1;
    }
    /* LCD Init */
    printf("initDisplay params ...\r\n");
    LCD_1IN14_Init(HORIZONTAL);
    LCD_1IN14_Clear(WHITE);

    //LCD_SetBacklight(1023);
    UDOUBLE Imagesize = LCD_1IN14_HEIGHT*LCD_1IN14_WIDTH*2;

    if((BlackImage = (UWORD *)malloc(Imagesize)) == NULL) {
        printf("initDisplay Failed to apply for black memory...\r\n");
        return -1;
    }
    // Create a new image cache named IMAGE_RGB and fill it with white
    Paint_NewImage((UBYTE *)BlackImage,LCD_1IN14.WIDTH,LCD_1IN14.HEIGHT, 0, WHITE);
    Paint_SetScale(65);
    Paint_Clear(WHITE);
    Paint_SetRotate(ROTATE_0);
    Paint_Clear(WHITE);

    // /* GUI */
    printf("initDisplay Done!...\r\n");

    return 0;
}


/**
 * Check the displays' stop button
 */
static bool stopButton()
{
    return !(gpio_get(StopButt));
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
static void psuEnable(int status)
{
    gpio_put(PsuPin, status);
}

/**
 * Add or subtract run-time (+/- 10 min)
 * by means of gpio actions, i.e buttons
 * on the display PCB.
 */
static int addSubTime()
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
 * An external mini-dip switch block
 * can be used to set default engine
 * run times.
 */
static int getPresetTime()
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
        printLog("Runtime = %d minutes", mFact*RUN_INTERVAL);
    } else {
        printLog("Runtime = %d minutes", RUN_INTERVAL);
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

        gpio_init(RunPin);
        gpio_set_dir(RunPin, GPIO_IN);
        gpio_pull_up(RunPin);

        gpio_init(StopButt);
        gpio_set_dir(StopButt, GPIO_IN);
        gpio_pull_up(StopButt);

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

        gpio_init(DebugPin);
        gpio_set_dir(DebugPin, GPIO_IN);
        gpio_pull_up(DebugPin);

}

#ifdef DIRECT_50Hz
/**
 * This is a free rinning core1 function.
 * Messure the line frequency (~50Hz).
 * An external inductor pickup ring can be
 * used to sens the line frequency without
 * any physical intrusion into the the hot
 * power line.
 * This sine wave should the be represented as
 * a square wave stream (around 2.5v peak) to the
 * PWM B pin of the Pico, i.e a Schmittrigger
 * circuit function to feed GP5.
 */
static uint16_t measure_frequency(uint gpio, int pollrate)
{

    uint slice_num;
    static int first;

    if (first == 0) {
        // Only the PWM B pins can be used as inputs.
        assert(pwm_gpio_to_channel(gpio) == PWM_CHAN_B);
        slice_num = pwm_gpio_to_slice_num(gpio);

        // Count once for every cycles the PWM B input is high
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING | PWM_DIV_B_FALLING);
        pwm_config_set_clkdiv(&cfg, 1.f); //set by default, increment count for each rising edge
        pwm_init(slice_num, &cfg, false);  //false means don't start pwm
        gpio_set_function(gpio, GPIO_FUNC_PWM);
        first = 1;
    }

    pwm_set_counter(slice_num, 0);

    pwm_set_enabled(slice_num, true);
    sleep_ms(pollrate);
    pwm_set_enabled(slice_num, false);

    return (uint16_t)pwm_get_counter(slice_num) * (1000/pollrate);

}

/**
 * The thread entry for the measure_frequency() function.
 * Be somewhat tolerant for temporary RPM drifts.
 */
static void core1Thread()
{

    int retry = THZDELTA;

    multicore_fifo_push_blocking(FLAG_VALUE);

    uint32_t g = multicore_fifo_pop_blocking();

    if (g == FLAG_VALUE) {

        while(1) {

            int f = measure_frequency(HzmeasurePin, 1000);

            if (f > HZ_MAX || f < HZ_MIN) {
                if (retry-- >= 0) {   // Hz drift handling for whatever reason
                    //printLog("drift=%d/%d", retr, f);
                    sleep_ms(250);
                    continue;
                }
            }
            retry = THZDELTA;

            LineFreq = f;
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
static bool wbekeIsRunning(int pollrate)
{

    sleep_ms(pollrate);

#ifdef DIRECT_50Hz
    if (LineFreq > HZ_MAX || LineFreq < HZ_MIN) {
        if (MonFlag == true) {
            printLog("Out of Hz band f=%d", LineFreq);
        }
        return false;
    }

    return true;

#else
    return gpio_get(RunPin);;
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
static void wbeke_ctrl_run(bool rerun)
{
    int runFlag;
    int mFact = 1;
    int reTry = 0;
    int c = 0;
    int preHeatInterval = PREHEAT_INTERVAL;
    char buf[100];

    if (rerun == false) {
        if (initDisplay() != 0) {
            return;
        }
        gpioInit();
    }

    DEV_SET_PWM(DEF_PWM);

    Paint_Clear(WHITE);

    HdrTxtColor = GREEN;
    FirstLogline = true;

    // Leave PSU control to panel buttons
    psuEnable(OFF);

    printHdr("WBEKE Generator Start");
    sleep_ms(3000);

#if 0
    for (int i = 0; i < MAX_LINES; i++)
    {
        printLog("line = %d", i);   // Scroll test
    }
    sleep_ms(6000);
#endif


#ifdef DIRECT_50Hz
    if (rerun == false) {
        multicore_launch_core1(core1Thread);

        // Wait for it to start up
        uint32_t g = multicore_fifo_pop_blocking();

        if (g != FLAG_VALUE) {
            printLog("Error: 50Hz detection");
            while(1) sleep_ms(2000);
        } else {
            multicore_fifo_push_blocking(FLAG_VALUE);
            printLog("%d-%d Hz sens started", HZ_MIN, HZ_MAX);
            sleep_ms(1500);
        }
    }

#if 0
    while(1) {
        sleep_ms(250);
        printLog("LineFreq=%d", LineFreq);
    }
#endif

#endif

#if 1
    if (wbekeIsRunning(POLLRATE)) {
        printLog("Line power already");
        printLog("present. Check shore");
        printLog("power cord or turn");
        printLog("off gen manually");
        return;
    }
#endif

    // We have control over Picos' power (not panel buttons)
    psuEnable(ON);

    stopEngine();   // Always be sure engine is stopped before using preheater and cranker

    // Get preset time
    mFact = getPresetTime();

    runFlag = 3;    // retry

    while(runFlag-- > 0 && !wbekeIsRunning(POLLRATE)) {

        printHdr("   Start Attempt %d", ++reTry);

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

        printLog("Is WBEKE running?");
        sleep_ms(2000);
        if (wbekeIsRunning(POLLRATE)) {
            printLog("WBEKE is running!");
            runFlag = 0;
            break;
        } else if (runFlag > 0) {
            printLog("No. Wait and retry!");
            // The generator may run but not at 50Hz. Make sure the diesel is stopped before retry.
            stopEngine();
            for (int i=0; i < 100; i++) {
                sleep_ms(250);
                if (stopButton()) {
                    runFlag = -2;
                    break;
                }
            }
            if (runFlag == -2) {
                break;
            }
        } else {
            runFlag = -1;
            break;
        }
    }

    if (runFlag < 0) {
        HdrTxtColor = RED;
        if (runFlag == -1) {
            printHdr("   Start Failed!");
            printLog("3 attempts failed");
        } else if (runFlag == -2) {
            printLog("User aborted start");
        }
    } else {
        printHdr(" Runtime monitoring");
        runFlag = (RUN_INTERVAL*60)*mFact;
        c = 0;
        int pollFact = 1000/POLLRATE;
        printLog("Runtime: %d minutes", runFlag/60);
        runFlag *= pollFact;

        while(runFlag-- > 0) {

            MonFlag = true;

            int timeAdj = addSubTime();

            if (timeAdj == 1) {
                printLog("%d minutes added", EXTRA_RUNTIME);
                runFlag += EXTRA_RUNTIME*(60*pollFact);
            } else if (timeAdj == 2) {
                printLog("%d minutes subtracted", EXTRA_RUNTIME);
                runFlag -= EXTRA_RUNTIME*(60*pollFact);
                if (runFlag < 0) runFlag = 0;
            }

            if (!wbekeIsRunning(POLLRATE) || stopButton()) {
                HdrTxtColor = RED;
                printHdr("   Premature stop");
                printLog("Monitoring stopped");
                runFlag = -2;
                break;
            }
            if (c++ > 60*pollFact) {
                c = 0;
                printLog("Time left: %d minutes", ((runFlag/pollFact) / 60)+1);
#ifdef DIRECT_50Hz
                static int lastHz;
                if (lastHz != LineFreq) {
                    printHdr("Linefrquency is %dHz",LineFreq);
                    lastHz = LineFreq;
                }
#endif
            }
        }

        MonFlag = false;

        if (runFlag == -1) {
            printHdr("   Runtime expired");
            printLog("Monitoring stopped");
        }

    }

    stopEngine();
    // Leave PSU control to panel buttons
    psuEnable(OFF);
}

void wbeke_ctrl(void)
{

    bool rerun = false;
    int tmo;

    while (1) {
        wbeke_ctrl_run(rerun);
        rerun = true;
        tmo = 16;
        while (gpio_get(RerunButt)) {

            sleep_ms(250);

#ifdef DIRECT_50Hz
            if (LineFreq > 10) {
                // Manually (re)started from wbekes' panel.
                DEV_SET_PWM(DEF_PWM);
                static int lastHz;
                if (lastHz != LineFreq) {
                    HdrTxtColor = GREEN;
                    printHdr(" Passive monitoring");
                    printLog("Linefrquency is %dHz",LineFreq);
                    lastHz = LineFreq;
                }
            } else if (tmo-- <= 0) {
                DEV_SET_PWM(4);
            }

#else
            if (tmo-- <= 0) {
                DEV_SET_PWM(4);
            }
#endif
        }
        if (gpio_get(DebugPin) == 0 || FlashMode == true) {
            // Enter rom boot mode
            reset_usb_boot(0,0);
        }
    }

}
