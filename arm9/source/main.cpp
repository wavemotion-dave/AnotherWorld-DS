/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <nds.h>
#include <fat.h>

#include "engine.h"
#include "systemstub.h"
#include "util.h"

#include "backscreen.h"
#include "topscreen.h"
#include "backcredit.h"

#define RAW_MENUINIT 0x01
#define RAW_MENUSHOW 0x02
#define RAW_PLAYGAME 0x03
#define RAW_PLAYSGAM 0x04 
#define RAW_MENUCRED 0x05
#define RAW_QUITSTDS 0x06


int bg0, bg1, bg0b, bg1b;

unsigned int dsWaitOnMenu(void) {
  // Palette index 179, 180, 181
  unsigned int uState=RAW_MENUINIT;
  unsigned int keys_pressed;
  bool bDone=false;
  int iUp=0, iDown=0, iA=0, iFlp=0, iIncPal = 1, iSensPal=0;
  static int iPal=179;
  
  while (!bDone) {
    // wait for stylus
    keys_pressed = keysCurrent();
    if (keys_pressed & KEY_UP) {
      if (!iUp) {
        iUp = 1;
        BG_PALETTE_SUB[iPal] = RGB15(31,31,31);
        iPal = iPal == 179 ? 181 : iPal-1;
      }
    }
    else iUp = 0;
    if (keys_pressed & KEY_DOWN) {
      if (!iDown) {
        iDown = 1;
        BG_PALETTE_SUB[iPal] = RGB15(31,31,31);
        iPal = iPal == 181 ? 179 : iPal+1;
      }
    }
    else iDown = 0;
    if (keys_pressed & (KEY_A | KEY_START)) {
      if (!iA) {
        iA = 1; bDone = true;
        if (iPal == 179) uState=RAW_PLAYGAME;
        if (iPal == 180) uState=RAW_PLAYSGAM;
        if (iPal == 181) uState=RAW_MENUCRED;
      }
    }
    else iA = 0;
    iFlp++;
    if (iFlp >= 10) { 
      iFlp=0; BG_PALETTE_SUB[iPal] = RGB15(iIncPal,iIncPal,iIncPal); 
      iIncPal += (iSensPal ? 2 : -2); if ( (iIncPal >= 31) || (iIncPal <= 1)) { iSensPal = 1 - iSensPal; }
    }
    swiWaitForVBlank();
  }
  
  return uState;
}

//*****************************************************************************
// Displays a message on the screen
//*****************************************************************************
void DSPrint(int iX, int iY, int iScr, char *szMessage)
{
    u16 *pusScreen, *pusMap;
    u16 usCharac;
    char *pTrTxt = szMessage;

    pusScreen = (u16 *)(iScr != 1 ? bgGetMapPtr(bg1b) : bgGetMapPtr(bg1)) + iX + (iY << 5);
    pusMap = (u16 *)(iScr != 1 ? (iScr == 6 ? bgGetMapPtr(bg0b) + 24 * 32 : (iScr == 0 ? bgGetMapPtr(bg0b) + 24 * 32 : (iScr == 4) ? bgGetMapPtr(bg0b) + 28 * 32 : bgGetMapPtr(bg0b) + 26 * 32)) : bgGetMapPtr(bg0) + 51 * 32);

    while ((*pTrTxt) != '\0')
    {
        char ch = *pTrTxt++;
        if (ch >= 'a' && ch <= 'z')
        {
            ch -= 32; // Faster than strcpy/strtoupper
        }

        if (((ch) < ' ') || ((ch) > '_'))
        {
            usCharac = *(pusMap); // Will render as a vertical bar
        }
        else if ((ch) < '@')
        {
            usCharac = *(pusMap + (ch) - ' '); // Number from 0-9 or punctuation
        }
        else
        {
            usCharac = *(pusMap + 32 + (ch) - '@'); // Character from A-Z
        }
        *pusScreen++ = usCharac;
    }
}


#undef main
int main(int argc, char *argv[])
{
    const char *dataPath = "/raw";
    const char *savePath = "/raw";
    unsigned int etatEmu = RAW_MENUINIT;

    // Init sound
    consoleDemoInit();
    soundEnable();
    lcdMainOnTop();

  // Init Fat
    if (!fatInitDefault()) {
        iprintf("Unable to initialize libfat!\n");
        while(1) 
        {
            swiWaitForVBlank();
        }
    }
    
    char tmpStr[128];
    u8 bWarnMissingFiles = 0;
    
    sprintf(tmpStr, "%s/MEMLIST.BIN", dataPath);
    FILE *fp = fopen(tmpStr, "rb");
    if (!fp) bWarnMissingFiles = 1;
    else fclose(fp);

    sprintf(tmpStr, "%s/BANK01", dataPath);
    fp = fopen(tmpStr, "rb");
    if (!fp) bWarnMissingFiles = 1;
    else fclose(fp);
    
    sprintf(tmpStr, "%s/BANK0D", dataPath);
    fp = fopen(tmpStr, "rb");
    if (!fp) bWarnMissingFiles = 1;
    else fclose(fp);

    if (bWarnMissingFiles)
    {
        iprintf("ONE OR MORE GAME FILES\nARE MISSING...\n\n");
        iprintf("THE FOLLOWING FILES FROM\n");
        iprintf("AN ORIGINAL DOS DISK ARE\n");
        iprintf("REQUIRED in the '/RAW' DIR:\n\n");
        iprintf("MEMLIST.BIN\n");
        iprintf("BANK01\n");
        iprintf("BANK02\n");
        iprintf("BANK03\n");
        iprintf("etc\n");
        iprintf("BANK0C\n");
        iprintf("BANK0D\n");
        while(1) 
        {
            swiWaitForVBlank();
        }
    }

  while(etatEmu != RAW_QUITSTDS) 
  {
    switch (etatEmu) 
    {
      case RAW_MENUINIT:
        // Init BG mode for 8 bits colors , tiles mode
        extern s16 sound_buffer[2048];
        memset(sound_buffer, 0x00, sizeof(sound_buffer));
#if 1 // Set to 0 to use iprintf()
        videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE );
        videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D_LAYOUT | DISPLAY_SPR_ACTIVE);
        vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
        bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
        bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31,0);
        bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29, 0);
        bgSetPriority(bg0b, 1);
        bgSetPriority(bg1b, 0);        

        decompress(backscreenTiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(backscreenMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *) backscreenPal,(u16*) BG_PALETTE_SUB,256*2);

        decompress(topscreenTiles, bgGetGfxPtr(bg0), LZ77Vram);
        decompress(topscreenMap, (void*) bgGetMapPtr(bg0), LZ77Vram);
        dmaCopy((void *) topscreenPal,(u16*) BG_PALETTE,256*2);
#endif              
        etatEmu = RAW_MENUSHOW;
        break;
        
      case RAW_MENUSHOW:
        etatEmu =  dsWaitOnMenu();
        break;
      
      case RAW_MENUCRED:
        decompress(backcreditTiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(backcreditMap, (void*) bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *) backcreditPal,(u16*) BG_PALETTE_SUB,256*2);
        while (keysCurrent())
           ;
        while (!keysCurrent())
           ;
        while (keysCurrent())
           ;

        etatEmu = RAW_MENUINIT;
        break;
       
      case RAW_PLAYGAME:
        {
            memset((u8 *)0x06000000, 0x00, 0x20000); // Ensure screen is clear...
            g_debugMask = 0; //| DBG_INFO; // DBG_LOGIC | DBG_BANK | DBG_VIDEO | DBG_SER | DBG_SND
            SystemStub *stub = SystemStub_SDL_create();
            Engine *e = new Engine(stub, dataPath, savePath);
            e->run();
            delete e;
            delete stub;
            etatEmu = RAW_MENUINIT;
        }
        break;
        
      case RAW_PLAYSGAM:
        {
            memset((u8 *)0x06000000, 0x00, 0x20000); // Ensure screen is clear...
            g_debugMask = 0; //| DBG_INFO; // DBG_LOGIC | DBG_BANK | DBG_VIDEO | DBG_SER | DBG_SND
            SystemStub *stub = SystemStub_SDL_create();
            Engine *e = new Engine(stub, dataPath, savePath);
            e->forceLoadState();
            e->run();
            delete e;
            delete stub;
            etatEmu = RAW_MENUINIT;
        }
        break;
    }
    }

    return 0;
}
