# Digital Photo Frame

Authors: Emily Rodrigues, Lazaro Ramos

For ECE-692 @ NJIT Spring 2023
Professor: Jean Walker-Soler

Description: This program will enable the viewing of JPEG photos on
a ST7735 display using an ESP32 as the microcontroller from two
different sources, either a MicroSD card, or a Server serving the
photos as HTTP requests. The program enables the modification of
settings through a companion BLE enabled app. With the Android app
the user can turn on/off the display, adjust the interval in between
photo changes, as well as changing the Wifi credentials, and server
details.

References:
Server and Wifi mode: https://www.instructables.com/Face-Aware-OSD-Photo-Frame/
SD card mode: https://github.com/bitbank2/JPEGDEC/
App: https://community.appinventor.mit.edu/t/ble-esp32-bluetooth-send-receive-arduino-ide-multitouch/1980/49
