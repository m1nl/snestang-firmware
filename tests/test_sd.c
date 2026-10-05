#include "sd_test.h"
static uint8_t response[32];
static int head, tail, command_bytes, command, data_bytes;
static uint32_t arg, now, written_arg[4];
static int write_commands, status_commands, blocklen_commands;
static bool await_data, busy_forever, write_busy_timeout, command_error, status_error, reject_data, missing_token;
static int card_version, ocr_error;
static void push(uint8_t x) { assert(tail<32); response[tail++]=x; }
uint8_t spi_send(uint8_t x) {
    if(data_bytes) {
        if(--data_bytes==0) {
            head=tail=0;
            push(0xff); push(0xff);
            if(!missing_token) {
                push(reject_data ? 0x0b : 0x05);
                if(write_busy_timeout) busy_forever=true;
                else { push(0); push(0); push(0); push(0xff); }
            }
        }
        return 0xff;
    }
    if(command_bytes) {
        if(command_bytes>1) arg=(arg<<8)|x;
        if(--command_bytes==0) {
            head=tail=0;
            switch(command) {
            case 0: push(1); break;
            case 8:
                if(card_version==1) push(5);
                else { push(1); push(0); push(0); push(1); push(card_version==3 ? 0 : 0xaa); }
                break;
            case 55: push(1); break;
            case 41: push(0); break;
            case 58:
                push(ocr_error ? 4 : 0); push(card_version==2 ? 0xc0 : 0x80);
                push(0); push(0); push(0); break;
            case 16: blocklen_commands++; assert(arg==512); push(0); break;
            case 24:
                assert(write_commands<4); written_arg[write_commands++]=arg;
                push(command_error ? 4 : 0); await_data=!command_error; break;
            case 13: status_commands++; push(0); push(status_error ? 0x20 : 0); break;
            default: assert(0);
            }
        }
        return 0xff;
    }
    if(head<tail) return response[head++];
    if(busy_forever) return 0;
    if(await_data && x==0xfe) { await_data=false; data_bytes=514; return 0xff; }
    if((x&0xc0)==0x40) { command=x&0x3f; command_bytes=5; arg=0; }
    return 0xff;
}
uint8_t spi_receive(void) { return spi_send(0xff); }
uint32_t spi_receive_word(void) {
    uint32_t v=spi_receive(); v|=(uint32_t)spi_receive()<<8;
    v|=(uint32_t)spi_receive()<<16; return v|((uint32_t)spi_receive()<<24);
}
void spi_send_word(uint32_t v) { for(int i=0;i<4;i++) spi_send(v>>(8*i)); }
uint8_t spi_sendrecv(uint8_t x) { spi_send(x); return spi_receive(); }
int time_millis(void) { return (int)now++; }
void delay(int ms) { now+=(uint32_t)ms; }
#include "sd_under_test.h"
static void setup(void) {
    head=tail=command_bytes=data_bytes=0; now=0;
    write_commands=status_commands=blocklen_commands=0;
    await_data=busy_forever=write_busy_timeout=command_error=status_error=reject_data=missing_token=false;
    card_version=2; ocr_error=0; sdhc_card=1;
}
int main(void) {
    uint8_t data[1024]; memset(data,0xa5,sizeof data);
    setup(); assert(sd_writesector(3,data,2));
    assert(write_commands==2 && status_commands==2 && written_arg[0]==3 && written_arg[1]==4);
    setup(); sdhc_card=0; assert(sd_writesector(3,data,1)); assert(written_arg[0]==1536);
    setup(); assert(!sd_writesector(0,data,0) && !sd_writesector(0,NULL,1));
    assert(!sd_writesector(UINT32_MAX,data,2) && write_commands==0);
    sdhc_card=0; assert(!sd_writesector(UINT32_MAX/512,data,2));
    setup(); command_error=true; assert(!sd_writesector(0,data,1) && status_commands==0);
    setup(); reject_data=true; assert(!sd_writesector(0,data,1) && status_commands==0);
    setup(); missing_token=true; assert(!sd_writesector(0,data,1));
    setup(); write_busy_timeout=true; assert(!sd_writesector(0,data,1)); assert(now>=1000);
    setup(); busy_forever=true; assert(!sd_writesector(0,data,1) && write_commands==0);
    setup(); status_error=true; assert(!sd_writesector(0,data,1) && status_commands==1);
    setup(); now=UINT32_MAX-10; write_busy_timeout=true; assert(!sd_writesector(0,data,1));
    setup(); assert(sd_init()==0 && sdhc_card==1 && blocklen_commands==0);
    setup(); card_version=1; assert(sd_init()==0 && sdhc_card==0 && blocklen_commands==1);
    setup(); ocr_error=1; assert(sd_init()!=0 && sdhc_card==0);
    setup(); card_version=3; assert(sd_init()!=0); // invalid CMD8 voltage/check-pattern echo
    puts("PASS: SD write tokens, busy timeout/wrap, CMD13 errors, addressing and initialization");
}
