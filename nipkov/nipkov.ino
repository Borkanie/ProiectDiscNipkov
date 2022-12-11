#include "nipkov.h"

/* --------------------------------------------------------------------------------------
 Setup after Reset
-------------------------------------------------------------------------------------- */
void setup()
{ 
  
  // init IOs

  // DAC Output Ports
  DDRA=0xFF; // set port A to output for DAC channel R
  DDRC=0xFF; // set port C to output for DAC channel G
  DDRL=0xFF; // set port L to output for DAC channel B
  PORTA = 0; // output R=0 
  PORTC = 0; // output G=0
  PORTL = 0; // output B=0
  // buttons on front panel are diginal inputs with internal pullup (+need a de-bounce cap)
  pinMode(NEXT_BUTTONPIN, INPUT_PULLUP);      // Push-Button "next track" 
  pinMode(PLAY_BUTTONPIN, INPUT_PULLUP);      // Push-Button "play/stop"
  pinMode(MODE_BUTTONPIN, INPUT_PULLUP);      // Mode Switch "pic/video"
   
  // Serial interface init
  Serial.begin(9600);
  int t = 10; //wait for serial port to open, max 5 seconds
  while (!Serial) {
      delay(500);
      if ( (t--) == 0 ) break;
  }
  Serial.print("\nNipkow Display Project\n"); 
  Serial.println("======================="); 
  Serial.print("Version =");
  Serial.println(Version);

  //  init framebuffer including margin pixels
  for (int n=0; n<FRAMESIZE+100; n++) framebuffer[n]=0;
  
  // SD-card reader init:
  // Initialize at the highest speed supported by the board/SDcard module
  // We need approx. 100MB/s for fluent videos (Try a lower speed if SPI errors occur.)
  if (!SD.begin(SDchipSelect, SD_SCK_MHZ(50))) 
  {
    SD.initErrorHalt();
    Serial.println(F("ERROR - SD card initialization failed !!"));
  } 
  else 
  {
    Serial.println(F("SD card initialization OK... List of files found : "));
    // List directory
    SD.ls(LS_R);
  }

 // Make sure current directory is root
 if (!dir.open("/")) Serial.println("error open root ...!");
 if (!SD.chdir())    Serial.println("error open root ...!");

  // Open first video file on root and pre-load first frame  
  // order if files on root is older to newer files
  OpenNextVideoFile();

   // read mode switch
  if (digitalRead(MODE_BUTTONPIN)==LOW) mode=0;  // in video mode
  else 
  {
    mode=1;           // in picture mode
    pic=1;
    loadpicture();   //  preload first test-picture to framebuffer 
  }

    
  // list available commands
  inputString="help";
  getCommand();
  inputString="";
  
  Serial.println(" ");
  
  // Setup Timer1 for the pixel-timing 
  TCCR1A = 0; // Normal Mode : Timer1 counts up to 0xFFFF, then overflow interrupt will call TIMER1_OVF_vect
  TIMSK1 = (1<<TOIE1); // set interrupt to TCNT1 overflow
  TCNT1 = 1;  // Startvalue (will be correctly set later) 
  TCCR1B = 0;  // stop Timer (will be started later)

  // wait for disk to spin...
  Serial.println(F("waiting for first sync..."));
  while(digitalRead(pinPulse));  

  // Puls-input for IR : Enable interrupt when going from H->L
  attachInterrupt(digitalPinToInterrupt(pinPulse), Pulse_Event, FALLING);  

 Serial.println(F("End of Init"));
 
} 


/* --------------------------------------------------------------------------------------
 MAIN LOOP (do some foreground stuff, but main action is handled via interrupts)
-------------------------------------------------------------------------------------- */
void loop() 
{ 

  // process buttons and serial commands
  user_input();

  // in case logging is enabled, output some log info 
  if ((logfps) && (pixel & 0x300) ) log_output(); // check only once per frame (when at 768 pixel)

  // if disk spins too slow, switch off outputs
  if (PeriodBetweenPulses > 300000)       
  {
         TCCR1B = 0;  // stop timer
         PORTA = 0;   // output R=0 
         PORTC = 0;   // output G=0
         PORTL = 0;   // output B=0
         if (logfps)  // if logging is enabled, output message
         {
           Serial.print(F(" period: "));
           Serial.print(PeriodBetweenPulses);        
           Serial.println(F(" -- too slow, Switched off... -- "));
         }
  }

  // if Input-Selector is in Video-mode and video is not paused, pre-load next video frame in double-buffer  
  if ((mode==0) && (play))      
    {
      if (loadbuffer_part0)                 // pre-load lower buffer ?
      {
          if (file.available())
          { 
            file.read(framebuffer, HALFBUF);     // read next frame to lower buffer     
            startpix=0;                          // set start-position for next frame to lower buffer
            frames++;
          }
          else
          {
            
           file.close();                       // close current video file
           OpenNextVideoFile();               // and load next one
           frames=0;           
          }   
          loadbuffer_part0=false;     
      }
      else if (loadbuffer_part1)            // pre-load upper buffer ?
      {
          if (file.available())
          { 
            file.read(framebuffer+HALFBUF, HALFBUF);  // read next frame to upper buffer
            startpix=HALFBUF;                         // set start-position for next frame to upper buffer
            frames++;
          }
          else
          {
           file.close();                    // close current video file
           OpenNextVideoFile();             // and load next one
           frames=0;
          }   
          loadbuffer_part1=false;     
      }
    }
} 



/* --------------------------------------------------------------------------------------
 Sync Puls Interrupt - new frame starts, measure current RPM of disk and toggle framebuffer
-------------------------------------------------------------------------------------- */
void Pulse_Event()  
{
unsigned long mnow;
    
   TCCR1B = 0;                                           // stop Pixel-Timer    

   mnow=micros();
   PeriodBetweenPulses = mnow - LastMeasured;           // get current frame-period (time between 2 sync pulses) 
   LastMeasured = mnow;  
 
    pixerror = FRAMESIZE-pixel;                         // indicates the over/underruns of last frame  
  
    if ((PeriodBetweenPulses > 20000) && (PeriodBetweenPulses < 300000) )  // if in valid range between 300000us (3fps) and 20000us (50fps) 
    { 
      per=((float)PeriodBetweenPulses/pixelsframe);         // calculate time for each pixel in us 
      cycles = (unsigned long)(65536 - (16 * per));         // set new timer value (1 cycle= 0.0625us)
      cycles+=adjust_cycles;                                // add adjust value   

       pixel=startpix;                                      // set pixel counter to start of current buffer
       
       if (startpix==0)        loadbuffer_part1=true;     // flag to preload upper part of buffer
       if (startpix==HALFBUF)  loadbuffer_part0=true;     // flag to preload lower part of buffer
 
    }

   TCNT1 = cycles;                                       // set new Timer value
   TCCR1B = (1<<CS10);                                   // restart timer
}

/* --------------------------------------------------------------------------------------
 16 bit Timer Interrupt - display next pixel
-------------------------------------------------------------------------------------- */
ISR(TIMER1_OVF_vect)
{
  // read next R,G,B Pixelvalues from Framebuffer and output to DAC ports
  PORTA = (framebuffer[pixel++] );  // R
  PORTC = (framebuffer[pixel++] );  // G
  PORTL = (framebuffer[pixel++] );  // B

  TCNT1 = cycles;    // re-load Timer 
}

/* --------------------------------------------------------------------------------------
 Loads a picture (indexed by variable "pic") from flash to framebuffer
-------------------------------------------------------------------------------------- */
void loadpicture()
{
  unsigned int n;
  for (n=0; n<FRAMESIZE; n++) // copy all pixels from pic stored in flash to framebuffer
  {
    framebuffer[n]=pgm_read_byte_near(bitmaps[pic-1]+n);
  }
}

/* --------------------------------------------------------------------------------------
 open next video file on SD card 
-------------------------------------------------------------------------------------- */
void OpenNextVideoFile()
{
  int c=0;
  boolean opensucc=false;

 do  // find a next file to open
 {
   if (file.openNext(&dir, O_RDONLY))           // try open next file in current (root) directory
   {
     if (!file.isDir())                         // if file exists, check if it's not a directory
       opensucc=true; 
     else 
     {
       file.close();                            // if it's a directory, go to next file
       opensucc=false;   
     }
   }   
   else
   {
       dir.rewind();                            // rewind directory to begin when reached the end
       opensucc=false;   
       c++;
   }
 } while ((!opensucc) && (c<2));   // repeat until next file could be opend and is not a directory or entire dir was searched
  

  if (opensucc)  // found next file
  {
      Serial.println(F("Opening next video file on card : "));
      file.printName(&Serial);
      Serial.write(' ');
      file.printFileSize(&Serial);
      Serial.write(' ');
      file.printModifyDateTime(&Serial);
      Serial.write(' ');
      Serial.println(F("ok"));
  }
  else      // unsuccessful
  {
      Serial.println(F("\nERROR - could not opening next video file on card !!"));
      mode=1; // switch to testpic 
    
  }

}


/* --------------------------------------------------------------------------------------
 outputs some logging data to serial 
-------------------------------------------------------------------------------------- */
void log_output()
{
    if ((PeriodBetweenPulses > 20000) && (PeriodBetweenPulses < 300000) )  // if in valid range between 300000us (3fps) and 20000us (50fps) 
    {
      fps=1000000*(1/(float)PeriodBetweenPulses);  // calculate current frame per second value
 
      // output logging data:
      Serial.print(F(" period: "));
      Serial.print(PeriodBetweenPulses);
      Serial.print(F(" us  "));
      Serial.print(F("\tfps: "));
      Serial.print(fps);    
      Serial.print(F("\terror: "));
      Serial.print(pixerror);
      Serial.print(F("\ttimer: "));
      Serial.print(per,2);
      Serial.print(F("\tframes: "));
      Serial.print(frames);
      Serial.print(F("\tinput: "));
      Serial.print(mode);
      Serial.print(F("\tpic: "));
      Serial.println(pic);
    }

}
/* --------------------------------------------------------------------------------------
 process user inputs -> check for button changes and serial commands
-------------------------------------------------------------------------------------- */
void user_input()
{

  // check if mode switch has changed
 if ((digitalRead(MODE_BUTTONPIN)==LOW) && (mode==1)) // mode has changed to video
 {
  Serial.println(F("Mode changed to video"));
  mode=0;
  file.rewind(); 
  frames=0;
  startpix=0;  
 }
 if ((digitalRead(MODE_BUTTONPIN)==HIGH) && (mode==0))  // mode has changed to pic
 {
  Serial.println(F("Mode changed to pic"));
  mode=1;     
  loadpicture();
  startpix=0;   
 }

 // check push buttons
 if ((digitalRead(NEXT_BUTTONPIN)==LOW) && (mode==0))  // next track pressed in video mode 
 {
   Serial.println(F("show next video : "));
   file.close();
   OpenNextVideoFile();
   while (digitalRead(NEXT_BUTTONPIN)==LOW);  // wait for button relase...
 }
 if ((digitalRead(NEXT_BUTTONPIN)==LOW) && (mode==1))  // next track pressed in pic mode 
 {
   pic++;
   if (pic>numberpics) pic=1;
   loadpicture();
   Serial.print(F("showing picture number "));
   Serial.println(pic);    
   while (digitalRead(NEXT_BUTTONPIN)==LOW);  // wait for button relase...
 }

 if ((digitalRead(PLAY_BUTTONPIN)==LOW) && (mode==0))  // play/stop pressed (only valid in video mode)
 {
   if (play) 
   {
     Serial.println(F("video STOP ..."));
     play=false;
   }else
   {
     Serial.println(F("video PLAY ..."));
    play=true;
   }
   while (digitalRead(PLAY_BUTTONPIN)==LOW);  // wait for button relase...
 }

  // process serial commands
  if (Serial.available() > 0) processInput(); // if any characters received, process commands
   
}



/* --------------------------------------------------------------------------------------
Serial Command Interface 
-------------------------------------------------------------------------------------- */
void processInput() 
{
  while (Serial.available())   // process all characters in input buffer received 
  {
    char inChar = (char)(Serial.read());  // read next character
    if (inChar == '\n')                   // Line-Feed received ?
       getCommand();                      // yes, commandline complete -> execute command
    else
    {
       inputString += inChar;             // no, add chat to inputString 
       Serial.print(inChar);              // Echo 
    }
  }
}

void getCommand()  // process commands
{
   String paramString = "";         // Parameters
   int len = inputString.length();  // length of commandline

   logfps=false;  // stop logfps output if enabled
   
    // Process Commands
 
         
    // --------------------------------------
    // helpscreen
    if (inputString.startsWith("help"))
    {
        Serial.println("");
        Serial.println(F("available commands :"));
        Serial.println(F("help       ...lists all commands"));
        Serial.println(F("logfps     ...prints fps and other values each frame (until next CR)"));
        Serial.println(F("dir        ...prints SD-card directory"));
        Serial.println(F("next       ...skip to next video file on SDcard"));
        Serial.println(F("rew        ...rewind video file to beginning"));
        Serial.println(F("input      ...set new input mode (or show current value with '?') ...does not work with poti connected"));
        Serial.println(F("adjust     ...set new frame adjust value (or show current value with '?')"));
     
    }     


    // --------------------------------------
    // print SD card directory
    if (inputString.startsWith("dir"))
    {
      Serial.println(F("\n--- SD Directory :"));
      SD.ls(LS_R);
      Serial.println(F("\n--- End of SD Directory "));
    }

    // --------------------------------------
    // print logfps information
    if (inputString.startsWith("logfps"))
    {
      Serial.println(F("\n--- start logging fps, period info :"));
      logfps = true;
    }

    // --------------------------------------
    // set or print input 
    if (inputString.startsWith("input"))
    {                           
       paramString = inputString.substring(6,len);
       if (paramString.startsWith("?"))
       {
         Serial.print(F("input = "));
         Serial.println(mode);
       }
       else
       {
        mode = paramString.toInt();
        Serial.println("");
        Serial.print(F("new input ="));
        Serial.print(mode);
        loadpicture();
        Serial.println(F(" ...ok"));
       }
    }    

    // --------------------------------------
    // offset 
    if (inputString.startsWith("adjust"))
    {                           
       paramString = inputString.substring(7,len);
       if (paramString.startsWith("?"))
       {
         Serial.print(F("adjust = "));
         Serial.println(adjust_cycles);
       }
       else
       {
        adjust_cycles = paramString.toInt();
        Serial.println("");
        Serial.print(F("new adjust ="));
        Serial.print(adjust_cycles);       
        Serial.println(F(" ...ok"));
       }
    }   

    // --------------------------------------
    // Open next video 
    if (inputString.startsWith("next"))
    {                           
        Serial.println(F(" ...skipping to next file"));
        file.close();
        OpenNextVideoFile();
    }   
    // --------------------------------------
    // rewind video 
    if (inputString.startsWith("rew"))
    {                           
        Serial.println(F(" ...rewind current video"));
        file.rewind();                      
    }   

    // --------------------------------------
   
    // ready for next command
    Serial.println(F("ready."));
    inputString = "";     
 
}
