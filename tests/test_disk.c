#include <assert.h>
#include <stdio.h>
#include "../fatfs/diskio.c"
static int ready=1, writes_ok=1;
int sd_sync(void) { return ready; }
int sd_init(void) { return 0; }
int print(const char *s) { (void)s; return 0; }
int sd_readsector_multi(uint32_t s, uint8_t *b, uint32_t n) { (void)s;(void)b;(void)n;return 1; }
int sd_writesector(uint32_t s, const uint8_t *b, uint32_t n) { (void)s;(void)b;(void)n;return writes_ok; }
int main(void) {
    uint8_t data[512]={0};
    assert(disk_ioctl(0,CTRL_SYNC,NULL)==RES_NOTRDY);
    assert(disk_initialize(0)==0);
    assert(disk_write(0,data,0,1)==RES_OK);
    writes_ok=0; assert(disk_write(0,data,0,1)==RES_ERROR);
    assert(disk_write(0,data,0,0)==RES_PARERR);
    assert(disk_read(0,NULL,0,1)==RES_PARERR);
    assert(disk_ioctl(0,CTRL_SYNC,NULL)==RES_OK);
    ready=0; assert(disk_ioctl(0,CTRL_SYNC,NULL)==RES_ERROR);
    assert(disk_ioctl(1,CTRL_SYNC,NULL)==RES_PARERR);
    puts("PASS: FatFs disk writes and CTRL_SYNC propagate card errors");
}
