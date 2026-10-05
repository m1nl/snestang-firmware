#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CORE_SNES 2
#define CORE_GBA 3
#define CRC16 0x8005
#define BACKUP_MAX_SIZE (128*1024)
#define BACKUP_PATH_SIZE 272
#define GSU_SAVE_OFFSET 0x7c00
#define GSU_SAVE_SIZE 0x400
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
typedef enum { FR_OK, FR_DISK_ERR, FR_NO_FILE, FR_NO_PATH } FRESULT;
typedef struct { int id; unsigned pos, size; } FIL;
typedef struct { unsigned fsize; } FILINFO;
#define f_size(f) ((f)->size)
static uint8_t ram[BACKUP_MAX_SIZE], backup_buffer[BACKUP_MAX_SIZE];
static volatile uint8_t *SNES_BSRAM=ram;
static int CORE_ID=CORE_SNES;
static bool option_backup_bsram=true, core_backup_valid, core_backup_gsu, backup_retry;
static uint16_t snes_bsram_crc16;
static uint32_t core_backup_time, dirty;
static uint32_t backup_success_time;
static bool backup_has_saved;
#define reg_cartram_dirty dirty
static int time_millis(void) { return 100; }
static void status(char *s) { (void)s; }
#define uart_printf(...) ((void)0)
uint16_t gen_crc16(const volatile uint8_t *, int);
static struct { char name[272]; uint8_t data[BACKUP_MAX_SIZE]; unsigned size; bool exists; } files[4];
static const char *fault, *fault_path;
static int faults;
static bool short_write, mutate_on_write;
static int calls_write;
static bool fail(const char *op, const char *path) {
    if(faults && !strcmp(op,fault) && (!fault_path || !strcmp(path,fault_path))) { faults--; return true; }
    return false;
}
static int lookup(const char *name) { for(int i=0;i<4;i++) if(files[i].exists && !strcmp(files[i].name,name)) return i; return -1; }
static int install(const char *name, const void *data, unsigned size) {
    int i=lookup(name); if(i<0) for(i=0;i<4 && files[i].exists;i++);
    assert(i<4); strcpy(files[i].name,name); files[i].exists=true; files[i].size=size;
    if(size) memcpy(files[i].data,data,size);
    return i;
}
static FRESULT f_stat(const char *p, FILINFO *info) {
    if(fail("stat",p)) return FR_DISK_ERR;
    if(!strcmp(p,"/saves")) return FR_OK;
    int i=lookup(p); if(i<0) return FR_NO_FILE; info->fsize=files[i].size; return FR_OK;
}
static FRESULT f_mkdir(const char *p) { (void)p; return FR_OK; }
static FRESULT f_open(FIL *f, const char *p, int mode) {
    if(fail("open",p)) return FR_DISK_ERR;
    int i=lookup(p);
    if(mode & FA_CREATE_ALWAYS) i=install(p,NULL,0);
    if(i<0) return FR_NO_FILE;
    f->id=i; f->pos=0; f->size=files[i].size; return FR_OK;
}
static FRESULT f_read(FIL *f, void *data, unsigned count, unsigned *br) {
    if(fail("read",files[f->id].name)) return FR_DISK_ERR;
    *br=count; if(*br>f->size-f->pos) *br=f->size-f->pos;
    memcpy(data,files[f->id].data+f->pos,*br); f->pos+=*br; return FR_OK;
}
static FRESULT f_write(FIL *f, const void *data, unsigned count, unsigned *bw) {
    calls_write++;
    if(fail("write",files[f->id].name)) { *bw=0; return FR_DISK_ERR; }
    *bw=short_write ? count/2 : count;
    memcpy(files[f->id].data,data,*bw); files[f->id].size=*bw;
    if(mutate_on_write) ram[0] ^= 0xff;
    return FR_OK;
}
static FRESULT f_close(FIL *f) { return fail("close",files[f->id].name) ? FR_DISK_ERR : FR_OK; }
static FRESULT f_unlink(const char *p) {
    if(fail("unlink",p)) return FR_DISK_ERR;
    int i=lookup(p); if(i<0) return FR_NO_FILE; files[i].exists=false; return FR_OK;
}
static FRESULT f_rename(const char *old, const char *new) {
    if(fail("rename",old)) return FR_DISK_ERR;
    int i=lookup(old); if(i<0) return FR_NO_FILE;
    if(lookup(new)>=0) return FR_DISK_ERR;
    strcpy(files[i].name,new); return FR_OK;
}
#include "backup_under_test.h"
static void setup(void) {
    memset(files,0,sizeof files); memset(ram,0,sizeof ram);
    CORE_ID=CORE_SNES; core_backup_gsu=false; option_backup_bsram=true;
    fault=NULL; fault_path=NULL; faults=0; short_write=false; mutate_on_write=false; calls_write=0;
    backup_load("game.srm",4096); assert(core_backup_valid);
}
static void inject(const char *op, const char *path) { fault=op; fault_path=path; faults=1; }
int main(void) {
    setup(); assert(backup_save("game.srm",4096)==1);
    ram[0]=0x69; assert(backup_save("chosen.srm",4096)==0);
    assert(lookup("/saves/chosen.srm")>=0 && lookup("/saves/game.srm")<0);
    const char *ops[]={"open","write","close","stat","unlink","rename","rename"};
    const char *paths[]={"/saves/game.srm.tmp","/saves/game.srm.tmp","/saves/game.srm.tmp","/saves/game.srm","/saves/game.srm.bak","/saves/game.srm","/saves/game.srm.tmp"};
    for(unsigned k=0;k<sizeof ops/sizeof *ops;k++) {
        setup(); ram[0]=1; assert(backup_save("game.srm",4096)==0); uint16_t crc=snes_bsram_crc16;
        ram[0]=2; inject(ops[k],paths[k]); assert(backup_save("game.srm",4096)==2);
        assert(snes_bsram_crc16==crc);
        int i=lookup("/saves/game.srm"); assert(i>=0 && files[i].data[0]==1);
        assert(backup_save("game.srm",4096)==0 && files[lookup("/saves/game.srm")].data[0]==2);
    }
    setup(); ram[0]=3; short_write=true; assert(backup_save("game.srm",4096)==2);
    short_write=false; assert(backup_save("game.srm",4096)==0);
    setup(); ram[0]=4; mutate_on_write=true; assert(backup_save("game.srm",4096)==0);
    assert(files[lookup("/saves/game.srm")].data[0]==4);
    assert(snes_bsram_crc16==gen_crc16(files[lookup("/saves/game.srm")].data,4096));
    assert(backup_save("game.srm",4096)==0); // mutation during previous write is still detected
    setup(); uint8_t contents[7]={1,2,3,4,5,6,7}; install("/saves/game.srm",contents,7);
    backup_load("game.srm",13); assert(core_backup_valid && ram[6]==7 && ram[7]==0 && ram[12]==0);
    inject("read","/saves/game.srm"); backup_load("game.srm",13); assert(!core_backup_valid && ram[0]==0);
    inject("open","/saves/game.srm"); backup_load("game.srm",13); assert(!core_backup_valid);
    inject("close","/saves/game.srm"); backup_load("game.srm",13); assert(!core_backup_valid);
    backup_load("game.srm",6); assert(!core_backup_valid); // oversized file
    files[lookup("/saves/game.srm")].exists=false; install("/saves/game.srm.bak",contents,7);
    backup_load("game.srm",13); assert(core_backup_valid && ram[6]==7); // interrupted replacement
    ram[0]=9; assert(backup_save("game.srm",13)==0);
    setup(); core_backup_gsu=true; backup_load("gsu.srm",32768);
    ram[0]=9; assert(backup_save("gsu.srm",32768)==1); // cached work RAM ignored
    ram[0x7c00]=8; assert(backup_save("gsu.srm",32768)==0);
    assert(files[lookup("/saves/gsu.srm")].size==32768);
    setup(); CORE_ID=CORE_GBA; dirty=1; ram[0]=7;
    inject("open","/saves/game.srm.tmp"); assert(backup_save("game.srm",4096)==2 && dirty==0);
    assert(backup_save("game.srm",4096)==0); // software retry survives clear-only dirty register
    assert(backup_save("game.srm",4096)==1);
    backup_load("missing-gba.srm",13);
    assert(core_backup_valid && ram[0]==0xff && ram[12]==0xff);
    option_backup_bsram=false; CORE_ID=CORE_SNES; ram[0]=1;
    backup_load("disabled.srm",13); assert(!core_backup_valid && ram[0]==0);
    backup_load("game.srm",BACKUP_MAX_SIZE+1); assert(!core_backup_valid);
    puts("PASS: backup load, snapshot/CRC, GSU range, failed-write retries, replacement and recovery");
}
