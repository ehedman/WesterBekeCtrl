# WesterBekeCtrl
Control application for Westerbeke Marine Generators (in progress Aug 2021)

This application is based on the Rasperry Pi Pico platform and an 1.4 inch graphical display with four control buttons (GPIO) and a custom made relay control box.

The purpose of this application is to automatimize the pre-heat, start and stop functions of the generator as well as monitor the generators run state in terms of RPM and its line frequency drift.

The user functions is to either start the generator with one single buttom press or set the application in stand-by mode to be stimulated to a start by external means, typically a signal from a battery monitor gadget that alerts a low charge level event.

The folder structure of this application is ment to be added to the Waveshare SDK "Pico_code/c" folder structure.

### Environment
- Linux Mint (Debian) platform for development
- Westerbeke 5.0 KW BCD diesel generator

### External dependencies
- ARM Cross delevopment tools for the Pico (apt install gcc-arm-none-eabi)
- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [PIco SDK reference manual](https://datasheets.raspberrypi.org/pico/raspberry-pi-pico-c-sdk.pdf)
- [Waveshare SDK](https://www.waveshare.com/w/upload/2/28/Pico_code.7z)

### Schematics for external relay box and other control logics
- On request

### Screenshots
-TBD

