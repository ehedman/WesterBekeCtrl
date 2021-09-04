# WesterBekeCtrl
Control application for Westerbeke Marine Generators (in progress Sept 2021)

This application is based on the Rasperry Pi Pico platform, a WiFi module, and an 1.4 inch graphical display with four control buttons (GPIO) and a custom made relay control box.

The purpose of this application is to automatimize the pre-heat, start and stop functions of the generator as well as monitor the generators run state in terms of RPM and its line frequency drift.

The user functions is to either start the generator with one single buttom press or set the application in stand-by mode to be stimulated to a start by external means, typically a signal from a battery monitor gadget that alerts a low charge level event.

It is also possible to logon to the Pico by means of the WiFi dongle that is mnaged as a telnet server for rmote operations.

The folder structure of this application is ment to be added to the Waveshare SDK "Pico_code/c" folder structure.

### Environment
- Linux Mint (Debian) platform for development
- Westerbeke 5.0 KW BCD diesel generator

### External dependencies
- ARM Cross delevopment tools for the Pico (apt install gcc-arm-none-eabi)
- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [PIco SDK reference manual](https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf)
- [Waveshare SDK](https://www.waveshare.com/w/upload/2/28/Pico_code.7z)
- [ESP8266 Serial WIFI Module](https://wiki.iteadstudio.com/ESP8266_Serial_WIFI_Module)

### Schematics for external relay box and other control logics
- On request

### Screenshots
- Under development at home ...
<img src="http://hedmanshome.se/wbDev1.png" width=100%>
<img src="http://hedmanshome.se/wbDev2.png" width=100%>
- Installed in the vessel.
<img src="http://hedmanshome.se/wbFinal.png" width=720px>
- Controlbox for the display panel (the pico+display+wifi) that also connects to the vessels power management systems' relay output that alarms for a low battery charge level.
<img src="http://hedmanshome.se/wbCtrlBox.png" width=720px>
- Relaybox in the engine compartmentthat connects to the Westerbeke control panel.
<img src="http://hedmanshome.se/wbRelayBox.png" width=100%>

