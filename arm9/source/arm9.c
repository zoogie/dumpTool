#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <nds/arm9/nand.h>
#include "f_xy.h"
#include "dsi.h"

#define CHUNKSIZE 0x80000
u8 *workbuffer;
u32 done=0;

typedef struct nocash_footer {
	char footerID[16];
	u8 nand_cid[16];
	u8 consoleid[8];
	u8 pad[0x18];
} nocash_footer_t;

void wait(int ticks){
	while(ticks--)swiWaitForVBlank();
}

u32 getMBremaining(){
	struct statvfs stats;
	statvfs("/", &stats);
	u64 total_remaining = ((u64)stats.f_bsize * (u64)stats.f_bavail) / (u64)0x100000;
	return (u32)total_remaining;
}

void death(char *message){
	iprintf("%s\n", message);
	iprintf("Hold Power to exit\n");
	free(workbuffer);
	while(1)swiWaitForVBlank();
}

void getCID(u8 *CID){
	memcpy(CID,(u8*)0x02FFD7BC,16); //arm9 location
}

void getConsoleID(u8 *consoleID){
	u8 *fifo=(u8*)0x02300000; //shared mem address that has our computed key3 stuff
	u8 key[16]; //key3 normalkey - keyslot 3 is used for DSi/twln NAND crypto
	u8 key_xy[16]; //key3_y ^ key3_x
	u8 key_x[16];////key3_x - contains a DSi console id (which just happens to be the LFCS on 3ds)
	u8 key_y[16] = {0x76, 0xDC, 0xB9, 0x0A, 0xD3, 0xC4, 0x4D, 0xBD, 0x1D, 0xDD, 0x2D, 0x20, 0x05, 0x00, 0xA0, 0xE1}; //key3_y NAND constant
	
	while(1){
		if(fifoCheckValue32(FIFO_USER_01)){  //checking to see when that plucky little arm7 has finished its consoleID magic
			break;
		}
		swiWaitForVBlank();
	}
	
	memcpy(key, fifo, 16);  //receive the goods from arm7

	F_XY_reverse((uint32_t*)key, (uint32_t*)key_xy); //work backwards from the normalkey to get key_x that has the consoleID
	
	for(int i=0;i<16;i++){
		key_x[i] = key_xy[i] ^ key_y[i];             //''
	}
	
	memcpy(&consoleID[0], &key_x[0], 4);             
	memcpy(&consoleID[4], &key_x[0xC], 4);
}

int dumpNAND(nocash_footer_t *footer){
	consoleClear();

	u32 rwTotal=nand_GetSize()*0x200; //240MB or 245.5MB
	printf("NAND size: %.2fMB\n", (float)rwTotal/(float)0x100000);
	
	if(rwTotal != 0xF000000 && rwTotal != 0xF580000) death("Unknown NAND size."); //there's no documented nand chip with sizes different from these two.
	
	bool batteryMsgShown=false;
	while(getBatteryLevel() < 0x4){
		if(!batteryMsgShown) {
			iprintf("Battery low: plug in to proceed\r"); //user can charge to 2 bars or more OR just plug in the charger. charging state adds +0x80 to battery level. low 4 bits are the battery charge level.
			batteryMsgShown=true;
		}
	}
	
	char *filename="nand.bin";
	int fail=0;
	swiSHA1context_t ctx;
	ctx.sha_block=0; //this is weird but it has to be done
	u8 sha1[20]={0};
	char sha1file[0x33]={0};
	
	FILE *f = fopen("nand.bin", "wb");
	if(!f) death("Could not open nand file");
	
	iprintf("Dumping...\n");
	iprintf("Hold A & B to cancel\n");
	iprintf("\x1b[16;0H");
	iprintf("Progress: 0%%\n");
	swiSHA1Init(&ctx);

	for(int i=0;i<rwTotal;i+=CHUNKSIZE){           //read from nand, dump to sd
		if(nand_ReadSectors(i / 0x200, CHUNKSIZE / 0x200, workbuffer) == false){
			iprintf("Nand read error\nOffset: %08X\nAborting...", (int)i);
			fclose(f);
			unlink(filename);
			fail=1;
			break;
		}
		swiSHA1Update(&ctx, workbuffer, CHUNKSIZE);
		if(fwrite(workbuffer, 1, CHUNKSIZE, f) < CHUNKSIZE){
			iprintf("Sdmc write error\nOffset: %08X\nAborting...", (int)i);
			fclose(f);
			unlink(filename);
			fail=1;
			break;
		}
		iprintf("\x1b[16;0H");
		iprintf("Progress: %lu%%\n", (i+CHUNKSIZE)/(rwTotal/100));
		scanKeys();
		int keys = keysHeld();
		if(keys & KEY_A && keys & KEY_B){
			iprintf("\nCanceling...");
			fclose(f);
			unlink(filename);
			fail=1;
			break;
		}
	}
	
	if(!fail) 
	{
		fwrite(footer, 1, sizeof(nocash_footer_t), f);
		fclose(f);
		swiSHA1Final(sha1, &ctx);
		char temp[3];
		for(int i=0;i<20;i++){
			snprintf(temp, 3, "%02X", sha1[i]);
			memcpy(&sha1file[i*2], temp, 2);
		}
		memcpy(&sha1file[20*2], " *nand.bin\n", 11);
		FILE *g=fopen("nand.bin.sha1","wb");
		fwrite(sha1file, 1, 0x33, g);
		fclose(g);
	}

	iprintf("\nDone.\nPress START to exit");
	done=1;
	
	return fail;
}

int restoreNAND(nocash_footer_t *footer){
	consoleClear();

	printf("\x1B[41mWARNING!\x1B[47m Even with the safety\n");
	printf("measures taken here, writing to\n");
	printf("NAND is very dangerous and most\n");
	printf("issues are not helped by\n");
	printf("restoring a NAND backup.\n\n");
	printf("Only continue if you're certain\n");
	printf("this will fix your problem.\n\n");
	printf("Press X + Y to begin restore\n");
	printf("Press B to cancel\n");

	u16 held = 0;
	while(1) {
		do {
			swiWaitForVBlank();
			scanKeys();
			held = keysHeld();
		} while(!(held & (KEY_X | KEY_Y | KEY_B)));

		if((held & (KEY_X | KEY_Y)) == (KEY_X | KEY_Y)) {
			consoleClear();
			break;
		} else if(held & KEY_B) {
			consoleClear();
			iprintf("Press Y to begin NAND restore\n");
			iprintf("Press A to begin NAND dump\nPress START to exit\n\n");
			return -1;
		}
	}

	u32 rwTotal=nand_GetSize()*0x200; //240MB or 245.5MB
	printf("NAND size: %.2fMB\n", (float)rwTotal/(float)0x100000);

	if(rwTotal != 0xF000000 && rwTotal != 0xF580000) death("Unknown NAND size."); //there's no documented nand chip with sizes different from these two.

	bool batteryMsgShown=false;
	while(getBatteryLevel() < 0x4){
		if(!batteryMsgShown) {
			iprintf("Battery low: plug in to proceed\r"); //user can charge to 2 bars or more OR just plug in the charger. charging state adds +0x80 to battery level. low 4 bits are the battery charge level.
			batteryMsgShown=true;
		}
	}

	int fail=0;
	int sectorsWritten=0;

	FILE *f = fopen("nand.bin", "rb");
	if(!f) death("Could not open nand file");

	fseek(f, (rwTotal==0xF580000 ? 0xF580000 : 0xF000000)+0x10, SEEK_SET);

	u8 CID[16];
	u8 consoleID[8];
	fread(&CID, 1, 16, f);
	fread(&consoleID, 1, 8, f);

	if((memcmp(footer->nand_cid, &CID, 16) != 0) || (memcmp(footer->consoleid, &consoleID, 8) != 0)) death("Footer does not match");

	fseek(f, 0, SEEK_SET);

	iprintf("Restoring...\n");
	iprintf("Do not turn off the power,\n");
	iprintf("or remove the SD card.\n");
	iprintf("\x1b[16;0H");
	iprintf("Progress: 0%%\nSectors written: 0\n");

	int i2=0;
	for(int i=0;i<rwTotal;i+=0x200){           //read nand dump from sd, compare sectors, and write to nand
		if(nand_ReadSectors(i2, 1, workbuffer) == false){
			iprintf("Nand read error\nOffset: %08X\nAborting...", (int)i);
			fclose(f);
			fail=1;
			break;
		}
		if(fread(workbuffer+0x200, 1, 0x200, f) < 0x200){
			iprintf("Sdmc read error\nOffset: %08X\nAborting...", (int)i);
			fclose(f);
			fail=1;
			break;
		}
		if(memcmp(workbuffer, workbuffer+0x200, 0x200) != 0){
			if(nand_WriteSectors(i2, 1, workbuffer+0x200) == false){
				iprintf("Nand write error\nOffset: %08X\nAborting...", (int)i);
				fclose(f);
				fail=1;
				break;
			}
			sectorsWritten++;
		}
		iprintf("\x1b[16;0H");
		iprintf("Progress: %lu%%\nSectors written: %i\n", (i+0x200)/(rwTotal/100), sectorsWritten);
		i2++;
	}

	if(!fail) 
	{
		fclose(f);
	}

	iprintf("\nDone.\nPress START to exit");
	done=1;

	return fail;
}

int verifyNocashFooter(nocash_footer_t *footer){
	u8 out[0x200]={0};
	u8 sha1[20]={0};
	u32 key_x[4]={0};
	u32 key_y[4] = {0x0AB9DC76,0xBD4DC4D3,0x202DDD1D,0xE1A00005}; //key3_y NAND constant
	u8 key[16]={0}; 
	u8 iv[16]={0};
	u32 cpuid[2]={0};
	
	nand_ReadSectors(0, 1, workbuffer); 
	dsi_context ctx;
	
	swiSHA1Calc(sha1, &footer->nand_cid, 16);
	memcpy(iv, sha1, 16);
	memcpy(cpuid, &footer->consoleid[0], 8);
	
	key_x[0]=cpuid[0];
	key_x[1]=cpuid[0] ^ 0x24EE6906;
	key_x[2]=cpuid[1] ^ 0xE65B601D;
	key_x[3]=cpuid[1];
	
	F_XY((u32*)key, (u32*)key_x, (u32*)key_y);

	dsi_init_ctr(&ctx, key, (u8*)iv);
	dsi_crypt_ctr(&ctx, workbuffer, out, 0x200);
	
	if(out[510]==0x55 && out[511]==0xAA) return 1;

	return 0;
}

int main(void) {
	extern void dsiOnly(void);
	dsiOnly();

	consoleDemoInit();
	iprintf("SafeNANDManager v1.0 by\n");
	iprintf("Rocket Robz (dumpTool by zoogie)\n");

	workbuffer=(u8*)malloc(CHUNKSIZE);
	if(!workbuffer) death("Could not allocate workbuffer");  
	nocash_footer_t nocash_footer;
	u8 CID[16];
	u8 consoleID[8];
	getCID(CID);
	getConsoleID(consoleID); //request bruteforce with write only arm7 aes registers to get consoleID. not as easy as CID!
	char dirname[128]={0};

	memset(nocash_footer.footerID, 0, sizeof(nocash_footer_t));
	memcpy(nocash_footer.footerID, "DSi eMMC CID/CPU", 16);
	memcpy(nocash_footer.nand_cid, CID, 16);
	memcpy(nocash_footer.consoleid, consoleID, 8);

	if(!fatInitDefault() || !nand_Startup()) death("MMC init problem - dying...");
	wait(1); //was having a race condition issue with nand_startup and nand_readsectors, so this might help

	if(getMBremaining() < 250) death("SD space remaining < 250MBs");
	if(nand_GetSize()*0x200 > 600*0x100000) death("This isn't a DSi!"); //I'll give you a kidney if there's unmodified DSi out there with a 600MB NAND.

	snprintf(dirname, 32, "DT%016llX", *(u64*)CID); //that 'certain other tool' uses MAC for console-unique ID, while this one uses part of the nand CID. either is fine but I don't want to overwrite the other app's dump.
	mkdir(dirname, 0777);
	chdir(dirname);

	bool nandFound = (access("nand.bin", F_OK) == 0);

	iprintf("Verifying nocash_footer: ");
	iprintf("%s\n", verifyNocashFooter(&nocash_footer) ? "GOOD":"BAD\nThis dump can't be decrypted\nwith this footer!");
	iprintf("\n");
	if (nandFound) {
		iprintf("Press Y to begin NAND restore\n");
	}
	iprintf("Press A to begin NAND dump\nPress START to exit\n\n");

	while(1) {

		swiWaitForVBlank();
		scanKeys();
		if      ((keysDown() & KEY_Y) && nandFound && !done) restoreNAND(&nocash_footer);
		if      ((keysDown() & KEY_A) && !done) dumpNAND(&nocash_footer);
		else if (keysDown() & KEY_START) break;
	}

	free(workbuffer);

	return 0;
}