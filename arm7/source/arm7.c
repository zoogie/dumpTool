/*---------------------------------------------------------------------------------

	derived from the default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <dswifi7.h>
#include <maxmod7.h>
#include <nds/bios.h>
#include <nds/arm7/aes.h>

//---------------------------------------------------------------------------------
void VblankHandler(void) {
//---------------------------------------------------------------------------------
	Wifi_Update();
}


//---------------------------------------------------------------------------------
void VcountHandler() {
//---------------------------------------------------------------------------------
	inputGetAndSend();
}

volatile bool exitflag = false;

//---------------------------------------------------------------------------------
void powerButtonCB() {
//---------------------------------------------------------------------------------
	exitflag = true;
}

/*
REG_AESCNT 	0x04004400 	4
REG_AESBLKCNT 	0x04004404 	4
REG_AESWRFIFO 	0x04004408 	4
REG_AESRDFIFO 	0x0400440C 	4
REG_AESCTR 	0x04004420 	16
REG_AESMAC 	0x04004430 	16
REG_AESKEY0 	0x04004440 	48
REG_AESKEY1 	0x04004470 	48
REG_AESKEY2 	0x040044A0 	48
REG_AESKEY3 	0x040044D0 	48
*/

#define AES_CTR_DECRYPT (2 << 27)

void set_ctr(u32* ctr){
	for (int i = 0; i < 4; i++) REG_AES_IV[i] = ctr[3-i];
}

void wait(int ticks){
	
	while(ticks--)swiWaitForVBlank();
}

// 10 11  22 23 24 25
void aes(void* in, void* out, void* iv, u32 method){ //this is sort of a bodged together dsi aes function adapted from this 3ds function
	REG_AES_CNT = ( AES_CNT_MODE(method) |           //https://github.com/TiniVi/AHPCFW/blob/master/source/aes.c#L42
					AES_WRFIFO_FLUSH |				 //as long as the output changes when keyslot values change, it's good enough.
					AES_RDFIFO_FLUSH | 
					AES_CNT_KEY_APPLY | 
					AES_CNT_KEYSLOT(3) |
					AES_CNT_DMA_WRITE_SIZE(2) |
					AES_CNT_DMA_READ_SIZE(1)
					);
					
    if (iv != NULL) set_ctr((u32*)iv);
	REG_AES_BLKCNT = (1 << 16);
	REG_AES_CNT |= 0x80000000;
	
	for (int j = 0; j < 0x10; j+=4) REG_AES_WRFIFO = *((u32*)(in+j));
	while(((REG_AES_CNT >> 0x5) & 0x1F) < 0x4); //wait for every word to get processed
	for (int j = 0; j < 0x10; j+=4) *((u32*)(out+j)) = REG_AES_RDFIFO;
	//REG_AES_CNT &= ~0x80000000;
	//if (method & (AES_CTR_DECRYPT | AES_CTR_ENCRYPT)) add_ctr((u8*)iv);
}

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------

	//readUserSettings();
	irqInit();  
	fifoInit();
	//mmInstall(FIFO_MAXMOD);
	//initClockIRQ(); // Start the RTC tracking IRQ
	//SetYtrigger(80);
	//installWifiFIFO();
	//installSoundFIFO();
	installSystemFIFO();
	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);
	irqEnable( IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);   
	//setPowerButtonCB(powerButtonCB);   
 
	u8 base[16]={0};
	u8 in[16]={0};
	u8 iv[16]={0};
	u8 *scratch=(u8*)0x02300200; 
	u8 *out=(u8*)0x02300000;
	u8 *key3=(u8*)0x40044D0;
	
	aes(in, base, iv, 2);

	//write consecutive 0-255 values to any byte in key3 until we get the same aes output as "base" above - this reveals the hidden byte. this way we can uncover all 16 bytes of the key3 normalkey pretty easily.
	//greets to Martin Korth for this trick https://problemkaputt.de/gbatek.htm#dsiaesioports (Reading Write-Only Values)
	for(int i=0;i<16;i++){  
		for(int j=0;j<256;j++){
			*(key3+i)=j & 0xFF;
			aes(in, scratch, iv, 2);
			if(!memcmp(scratch, base, 16)){
				out[i]=j;
				//hit++;
				break;
			}
		}
	}
	
	fifoSendValue32(FIFO_USER_01, 42);//just signal to the arm9 that everything's ready. the value doesn't matter
	
	// Keep the ARM7 mostly eric idle
	while (!exitflag) {
		if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_X | KEY_Y))) {
			exitflag = true;
		}
		swiWaitForVBlank();
	}
	
	return 0;
}