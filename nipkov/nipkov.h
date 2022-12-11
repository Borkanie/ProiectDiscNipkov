/*
Nipkow Disk Project
===================

A Nipkow disk is a mechanical, rotating image scanning device, invented in 1885 by Paul Nipkow.
It was a fundamental component in mechanical television through the 1920s and 1930s.
This project uses a 3D-printed version of a Nipkow disk, an Arduino Mega and a bright (RGB)LEDs
to create images.

Hardware :
Arduino Mega 2560
Nipkow-Disk : V4 32x32 (1024) Pixel
RGB LED , 3x 6bit-R2R DACs
SD Card Module
Buttons

Note : 
library "SDfat.h" is required (add via Arduino library manager) for fast SDcard read access 

Pin Connections on Mega :
 
 Port(pin) -> DAC channel
  A2(24) -> R0    C2(35) -> G0     L2(47) -> B0
  A3(25) -> R1    C3(34) -> G1     L3(46) -> B1
  A4(26) -> R2    C4(33) -> G2     L4(45) -> B2
  A5(27) -> R3    C5(32) -> G3     L5(44) -> B3
  A6(28) -> R4    C6(31) -> G4     L6(43) -> B4
  A7(29) -> R5    C7(30) -> G5     L7(42) -> B5
  
  SD-card Module Pins  :
  PG0(41) -> CS 
  PB3(50) -> MISO
  PB2(51) -> MOSI
  PB1(52) -> CLK

  IR sync input :
  PE4(2) -> Sensor / INT4

  Keys for front panel (a debounce-cap 100nF is strongly recommended for buttons !) : 
  Mode select (pic or video) -> PB7 (13)
  play/stop                  -> PB6 (12)
  next track                 -> PB5 (11)

 V14 13.03.2022 final version

 mac70 March 2022
 https://www.hackster.io/mac70/projects

*/

const String Version = "14";     // Version of this module
  
// Includes
#include <SPI.h>                  // SPI I/F
#include "SdFat.h"                // fast SDcard library 
 #include "bitmaps.h"              // Testpics to be stored in flash memory
const byte numberpics = 5;        // --> specify number of pictures contained in "bitmaps.h" include file here !

// Pin Assigments 
const int pinPulse = 2;           // IR Detector input for frame sync = IO-Pin 2(INT0)
const int SDchipSelect = 41;      // SD-card Module Pins: MISO - pin 50, MOSI - pin 51 , CLK - pin 52, CS - pin 41 
const int MODE_BUTTONPIN = 13;    //  Mode-Switch Input (Low for Video, High for Pictures)
const int PLAY_BUTTONPIN = 12;    // "Play/Stop" button (don't forget to add a debounce-cap!)
const int NEXT_BUTTONPIN = 11;    // "Next Track" button (don't forget to add a debounce-cap!)

// Nikow Disk Paramter
#define FRAMESIZE 3072            // size of framebuffer ( 32*32 Pixels = 1024  * 3 Bytes/pixel = 3072 Bytes)
const int pixelsframe = 1024;     // Number of Pixels in a frame  32*32=1024
unsigned long adjust_cycles = 94; // adjustment value for frame position, depends on disk sync loc -> CHANGE this value for your disk !!

// Buffersizes
#define BUFFERSIZE FRAMESIZE*2    // Double-Buffer
#define HALFBUF FRAMESIZE         // Half of Buffer

// Framebuffer definition
byte framebuffer[BUFFERSIZE+100]; // Framebuffer, with a few pixels added in case overruns occur
bool loadbuffer_part0=false;      // flag to show pre-load lower buffer (video mode only)
bool loadbuffer_part1=false;      // flag to show pre-load upper buffer (video mode only)

// sync and timing variables
unsigned long PeriodBetweenPulses = 1000;     // Time in us between IR sync pulses
unsigned long LastMeasured;                   // last measurement
float fps;                                    // derived frames per sec rate
signed int pixerror;                          // indicates over/underrun pixels
float per;                                    // holds current duration of one pixel in us
unsigned long cycles;                         // timer value for pixel-timer 

// pixelposition
int pixel=0;                    // index if current pixel in framebuffer
int startpix=0;                 // offset for double buffer (video mode only)

// source media controls
byte mode=1;                    // what to display : 0 = video ; 1 = pics
unsigned int pic=1;             // index of picture to show
unsigned int frames=0;          // frame counter for video 
boolean play=true;              // video playing or stopped

// command interface
boolean cmdreceived = false;    // true if new command received
String inputString = "";        // contains the received commandline
boolean logfps = false;         // if true, it will output some fps, period etc values to serial

// SD card
SdFat32 SD;                     // File system object
FatFile file, dir;              // File and directory handles

// prototypes
void Pulse_Event();
void loadpicture();
void OpenNextVideoFile();
