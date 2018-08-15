/*
display.ino
Display navigation demo, for 4D Systems GEN4-IOD-24T
Copyright (c) 2018, Peter Brier
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the 4D Display project.
 */
 
#include "ESP8266WiFi.h"
#include "GFX4d.h"
#include "prototype\assets.h"

#define SCREENS (sizeof(assets) / sizeof(assets[0]) )

#define RGB565(r,g,b)  ( ((unsigned short)b >> 3) | ((unsigned short)g & 0xFC) << 3 | ((unsigned short)r & 0xF8) << 8 )
#define BAUDRATE 500000
#define FILENAME "assets.bin"

#define DISPLAY_CS_PIN 15
#define DISPLAY_DC_PIN 4
#define SD_CS_PIN 5


GFX4d gfx = GFX4d();


int screen_index = 0;

// The external (SD Card) assets
File binfile;
typedef struct asset { int ofs; int len; } asset;
typedef struct navigation { int left; int right; int up; int down; } navigation;
asset sdassets[128];
navigation navi[128];
int numassets = 0;


inline void setDataBits(uint16_t bits) {
    const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
    bits--;
    SPI1U1 = ((SPI1U1 & mask) | ((bits << SPILMOSI) | (bits << SPILMISO)));
}

static inline uint8 ICACHE_FLASH_ATTR read_rom_uint8(const uint8* addr)
{
    uint32 bytes;
    bytes = *(uint32*)((uint32)addr & ~3);
    return ((uint8*)&bytes)[(uint32)addr & 3];
}


/**
*** display()
*** Dipsplay RLE image on location x,y
*** Note: RLE data might be in flash, and then only 32bit alligned reads are allowed we need
*** some magic to read individual bytes.
**/
#define RD(x) read_rom_uint8(x)
void  ICACHE_FLASH_ATTR display_flash(int x, int y, const unsigned char *img)
{
   static const unsigned char *__rd;
   static unsigned short c, i;
   static int __l;
   static long int len; 
   static int w, h;

   __rd = img + 2*sizeof(unsigned long int);
   w = *(const unsigned long*)img;
   h = *(const unsigned int*)(img+4);
   len = w*h;
   gfx.setGRAM(x, y, x+w-1, y+h-1);
   
   SPISettings spiSettings = SPISettings(80000000, MSBFIRST, SPI_MODE0);
   digitalWrite(DISPLAY_DC_PIN, HIGH);
   digitalWrite(DISPLAY_CS_PIN, LOW);
 
   SPI.beginTransaction(spiSettings);
   setDataBits(16); 
   while ( len > 0 ) 
   { 
      __l = RD(__rd++);
      if ( __l & 128 ) // repeat pixel n times 
      {  
        __l = __l - 128;
        c = ((unsigned short)RD(__rd+0) << 8) | ((unsigned short)RD(__rd+1));
        __rd += 2; 
        
        // SPI.writePattern((unsigned char*)&c, 2, __l);
        for(i=__l; i--;)
        {
           while(SPI1CMD & SPIBUSY) {}
           SPI1W0 = c;
           SPI1CMD |= SPIBUSY; 
        } 
      } 
      else // literal copy
      { 
        for(i=__l; i--;)
        {
           // SPI.write16( ((unsigned short) RD(__rd+1)<<8) | ((unsigned short)RD(__rd+0)) ); 
           c = ((unsigned short) RD(__rd+0)<<8) | ((unsigned short)RD(__rd+1));
           while(SPI1CMD & SPIBUSY) {}
           SPI1W0 = c;
           SPI1CMD |= SPIBUSY;
           __rd += 2;
        } 
      }      
      len -= __l;
    } 
    while(SPI1CMD & SPIBUSY) {}
    SPI.endTransaction();  
    digitalWrite(DISPLAY_CS_PIN, HIGH);
}

static int readptr = -1;
unsigned char read_sd_data()
{
  unsigned char c;
  static unsigned char buf[512];   
  if ( readptr < 0 )
  {
    readptr = 0;
      
    while(SPI1CMD & SPIBUSY);
    digitalWrite(DISPLAY_CS_PIN, HIGH);
    binfile.read(&buf, sizeof(buf));
    
    SPISettings spiSettings = SPISettings(80000000, MSBFIRST, SPI_MODE0);
    digitalWrite(DISPLAY_DC_PIN, HIGH);
    digitalWrite(DISPLAY_CS_PIN, LOW);
    SPI.beginTransaction(spiSettings);
    setDataBits(16);
    c = buf[readptr++];
  } 
  else
  {
    c = buf[readptr++];
    if ( readptr > sizeof(buf)-1 ) readptr = -1;
  }
  return c;
}

/**
*** display()
*** Dipsplay RLE image on location x,y
*** Data read from SD card, in 512 byte chunks and user per byte
**/
#define SDRD() read_sd_data() 
void  ICACHE_FLASH_ATTR display_sd(int x, int y, int num)
{
   static unsigned short c, i;
   static unsigned char __l;
   static long int len; 
   static int w, h;
   binfile.seek(sdassets[num].ofs + 8*(numassets+1));
   binfile.read(&w, sizeof(w));
   binfile.read(&h, sizeof(h));
   len = w*h;
   readptr = -1;
  
   SPISettings spiSettings = SPISettings(80000000, MSBFIRST, SPI_MODE0);
   digitalWrite(DISPLAY_DC_PIN, HIGH);
   digitalWrite(DISPLAY_CS_PIN, LOW);
   
   gfx.setGRAM(x, y, x+w-1, y+h-1);
   SPI.beginTransaction(spiSettings);
   setDataBits(16); 
   while ( len > 0 ) 
   { 
      __l = SDRD();
      if ( __l & 128 ) // repeat pixel n times 
      {  
        __l = __l - 128;
        c = ((unsigned short)SDRD()) << 8;
        c |= (unsigned short)SDRD();
        
        for(i=__l; i--;)
        {
           while(SPI1CMD & SPIBUSY) {}
           SPI1W0 = c;
           SPI1CMD |= SPIBUSY; 
        } 
        while(SPI1CMD & SPIBUSY) {}
      } 
      else // literal copy
      { 
        for(i=__l; i--;)
        {
           // SPI.write16( ((unsigned short) RD(__rd+1)<<8) | ((unsigned short)RD(__rd+0)) ); 
           c = ((unsigned short) SDRD())<<8; 
           c |= (unsigned short) SDRD();
           while(SPI1CMD & SPIBUSY) {}
           SPI1W0 = c;
           SPI1CMD |= SPIBUSY;
        } 
      }      
      len -= __l;
    } 
    while(SPI1CMD & SPIBUSY) {}
    SPI.endTransaction();  
    digitalWrite(DISPLAY_CS_PIN, HIGH);
}

void screentest2(void)
{
  char str[32];
  for(int j=0; j<2; j++)
    for(int i=0; i<16; i++)
    {
      gfx.FillScreen(1<<i);
      sprintf(str, "%d", (int)i);
      gfx.MoveTo(0,0);   
      gfx.println(str);
      delay(400);
    }
}

void screentest(void)
{
  char str[32];
  gfx.setGRAM(0, 0, 320, 240);
  for(int j=0; j<320; j++)
    for(int i=0; i<240; i++)
    {
      gfx.WrGRAM16( RGB565(0xFF, 0, 0) );  
    }
    delay(400);
}


void display_test_sd()
{
  long a = micros();

  for(int i=0; i<numassets; i++)
    display_sd(0,0,i);
  a = micros() - a;
  
  gfx.Cls();
  gfx.print("SD Total time: ");
  gfx.println( a );
  delay(1000);
}

void display_test_rom2()
{
  long a = micros();
  
  for(int i=0; i<100; i++)
    display_flash(i,i,assets[0]);
  a = micros() - a;
  
  gfx.Cls();
  gfx.print("SD 100 frames total time: ");
  gfx.println( a );
  delay(1000);
}

void display_test_rom()
{
  long a = micros();
  
  for(int i=0; i<SCREENS; i++)
    main_menu(i);
  
  a = micros() - a;
  gfx.Cls();
  gfx.print("ROM Total time: ");
  gfx.println( a );
  delay(1000);
}


void setup() 
{
  long a;
  
  Serial.begin(BAUDRATE);
  int entry[2];
  
  // put your setup code here, to run once:
  gfx.begin();  
  gfx.Cls();
  gfx.ScrollEnable(false);
  gfx.BacklightOn(true);  
  gfx.Orientation(LANDSCAPE);
  gfx.SmoothScrollSpeed(5);
  gfx.TextColor(WHITE, BLACK); gfx.Font(2);  gfx.TextSize(1);
  gfx.touch_Set(TOUCH_ENABLE);
  
  gfx.Cls(0);
  gfx.println("Display navigation demo");
  gfx.println("for 4D Systems GEN4-IOD-24T");
  gfx.println("Copyright (c) 2018, Peter Brier"); 
  gfx.println("All rights reserved."); 
  gfx.println("V1.0 " __DATE__ " " __TIME__);
  gfx.print("#ROM SCR: ");
  gfx.println( SCREENS );
  gfx.println("");
  gfx.print("Baudrate: ");
  gfx.println(BAUDRATE);
  gfx.print("Filename: ");
  gfx.println(FILENAME);

  // show directory of entries
  SD.begin(5, 80000000);
  if ( 0 ) 
    gfx.println("ERROR: Cannot find SD card!");
  else
  {
    binfile = SD.open("navigation.bin", FILE_READ);
    if (!binfile ) 
       gfx.println("ERROR: cannot navigation file! navigation.bin");
    while (binfile.available()) {
      binfile.read(&navi[numassets], sizeof(navigation));
      numassets++;
    }
    binfile.close();
    gfx.print("# Assets: ");
    gfx.println(numassets);
    
    numassets = 0;
    binfile = SD.open(FILENAME, FILE_READ);
    if (!binfile ) 
       gfx.println("ERROR: cannot open file! " FILENAME);
    while (binfile.available()) {
      binfile.read(entry, sizeof(entry));
      //gfx.print(entry[0]);
      //gfx.print(" ");
      //gfx.print(entry[1]); 
      if (entry[1] == 0 ) 
        break;
      memcpy((void*)&sdassets[numassets], (void*)&entry, sizeof(assets[0]));
      numassets++;
      //gfx.print(", ");
    }   
     ///gfx.println();
     gfx.print("# Assets: ");
     gfx.println(numassets);
  }
  delay(1000);

  display_test_sd();
  display_test_rom();
  display_test_rom2();
}


void main_menu_rom(int screen)
{
  if ( screen < 0 ) screen = 0;
  if ( screen > SCREENS-1) screen = SCREENS-1;
  display_flash( 0, 0, assets[screen] );
}


void main_menu(int screen)
{
  if ( screen < 0 ) screen = 0;
  if ( screen > numassets) screen = numassets;
  display_sd( 0, 0, screen );
}

#define TNONE 0
#define TACTIVE 1
#define TPRESSED 2
#define TRELEASED 4

#define TRIGHT 8
#define TLEFT 16
#define TUP 32
#define TDOWN 64


int touch_event()
{
  int x,y,t,e=TNONE;
  static int pt = -1;
  for(int i=0; i<10; i++)
    gfx.touch_Update();  
  x = gfx.touch_GetX();
  y = gfx.touch_GetY();
  t = gfx.touch_GetPen();
  /* if ( Serial.available() > 0 )
  {
    switch ( Serial.read() )
    {
      case '-' : e |= TLEFT; break;
      case '=' : e |= TRIGHT; break;
      case '[' : e |= TUP; break;
      case ']' : e |= TDOWN; break; 
      default: e = TNONE; break;
    }
    if ( e ) e |= TPRESSED;
  } */
  if ( t == TOUCH_PRESSED  )
  {
    e |= TACTIVE;
    if ( t != pt)
      e |= (t == TOUCH_PRESSED ? TPRESSED : TRELEASED);
    if ( y  < 60 ) 
      e |= TUP;
    else if ( x  < 100 ) 
      e |= TLEFT;
    else if ( x  > (320-100) ) 
      e |= TRIGHT;
    else e |= TDOWN;
  }
  pt = t;
  return e;
}


void loop2() 
{    
  int t;  
  int dir = 0;
  static int tstart=-1;
  static int prev_index = -1;
  static int interval;

  t = touch_event();
   // single press
  if ( t & TPRESSED )   // If the screen is touched
  {
    /* gfx.Cls(0);
    sprintf(str, "%d,%d: %d", (int)x, (int)y, (int)screen_index);
    gfx.MoveTo(0,0);   
    gfx.println(str); */

    if ( t & TRIGHT ) dir = 1; 
    if ( t & TLEFT ) dir = -1; 
    if ( t & TUP ) screen_index = 0;
    if ( t & TDOWN ) screen_index = numassets-1;
  }

  // increasing speed on long press
  if ( t & TACTIVE  )
  {
    if ( t & TPRESSED)
      tstart = millis();
  }
  else
  {
    tstart = -1;
    interval = 800;
  }
  if ( tstart > 0 )
  {
    int duration = (millis() - tstart);
    //gfx.MoveTo(0,0);   
    //gfx.print(duration);
    //gfx.print("    ");
    
    if ( duration > interval )
    {
      if ( interval == 800 )  // first time, make jump in interval
        interval -= 500;
      else
        interval -= 50; 
        
      tstart = millis();
     
      if ( interval < 10 ) interval = 10;
      if ( t & TRIGHT ) dir = 1; 
      if ( t & TLEFT ) dir = -1;
     // if ( dir > 50 ) dir = 50;
     // if ( dir < -50 ) dir = -50;
     // gfx.print(dir);
     // gfx.print("    ");    
    
    } 
  }

  screen_index += dir;
  if ( screen_index < 0 ) screen_index = 0;
  if ( screen_index > numassets-1) screen_index = numassets-1;
  if ( screen_index != prev_index )
  {
    main_menu(screen_index);
    prev_index = screen_index;
  }
 // delay(1);
  yield();
}




void loop() 
{    
  int t;  
  int dir = 0;
  static int tstart=-1;
  static int prev_index = -1;
  static int interval;

  t = touch_event();
   // single press
  if ( t & TPRESSED )   // If the screen is touched
  {
    /* gfx.Cls(0);
    sprintf(str, "%d,%d: %d", (int)x, (int)y, (int)screen_index);
    gfx.MoveTo(0,0);   
    gfx.println(str); */

    if ( t & TRIGHT ) dir = 1; 
    if ( t & TLEFT ) dir = -1; 
    if ( t & TUP ) screen_index = 0;
    if ( t & TDOWN ) screen_index = numassets-1;
  }

  // increasing speed on long press
  if ( t & TACTIVE  )
  {
    if ( t & TPRESSED)
      tstart = millis();
  }
  else
  {
    tstart = -1;
    interval = 800;
  }
  if ( tstart > 0 )
  {
    int duration = (millis() - tstart);
    //gfx.MoveTo(0,0);   
    //gfx.print(duration);
    //gfx.print("    ");
    
    if ( duration > interval )
    {
      if ( interval == 800 )  // first time, make jump in interval
        interval -= 500;
      else
        interval -= 50; 
        
      tstart = millis();
     
      if ( interval < 10 ) interval = 10;
      if ( t & TRIGHT ) dir = 1; 
      if ( t & TLEFT ) dir = -1;
     // if ( dir > 50 ) dir = 50;
     // if ( dir < -50 ) dir = -50;
     // gfx.print(dir);
     // gfx.print("    ");    
    
    } 
  }

  screen_index += dir;
  if ( screen_index < 0 ) screen_index = 0;
  if ( screen_index > numassets-1) screen_index = numassets-1;
  if ( screen_index != prev_index )
  {
    main_menu(screen_index);
    prev_index = screen_index;
  }
 // delay(1);
  yield();
}
