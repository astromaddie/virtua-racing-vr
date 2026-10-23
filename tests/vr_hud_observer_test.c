/* Exercise the actual interpreter dispatch and HUD consumer, including state
   across buffers, aliases, transparent writes and list-entry changes. */
#include "../src/hud.c"
#include <assert.h>
#include <stdio.h>

struct Pico32xMem *Pico32xMem;
SH2 sh2s[2];
int vrde_sh2_access_filter;
void (*vrde_sh2_access_hook)(int,unsigned int,unsigned int,unsigned int,int,int);
static struct Pico32xMem memory;
static unsigned calls, expected_slave, expected_pc, expected_address, expected_value;
static int expected_size, expected_write;
static void observe(int slave,unsigned pc,unsigned address,unsigned value,int size,int write)
{
    assert(slave==expected_slave && pc==expected_pc && address==expected_address);
    assert(value==expected_value && size==expected_size && write==expected_write);
    calls++;
    hud_note_sh2_access(slave,address,value,size,write);
}
static uint64_t state_hash(void)
{
    uint64_t h=1469598103934665603ULL;
    const unsigned char *data=(const unsigned char *)written;
    for(size_t i=0;i<sizeof(written);i++)h=(h^data[i])*1099511628211ULL;
    data=(const unsigned char *)written_item;
    for(size_t i=0;i<sizeof(written_item);i++)h=(h^data[i])*1099511628211ULL;
    h=h*33+drawn_types[0];h=h*33+drawn_types[1];h=h*33+current_item;
    return ((h*33+current_type)*33)+(unsigned)target;
}
static unsigned replay(int filter,uint64_t states[32])
{
    memset(written,0,sizeof(written));memset(written_item,0,sizeof(written_item));
    memset(drawn_types,0,sizeof(drawn_types));target=-1;current_item=current_type=0;
    calls=0;vrde_sh2_access_filter=filter;vrde_sh2_access_hook=observe;
    const unsigned address[]={0x06000000,0x0600c7ff,0x0600c800,0x0600c9ff,
        0x0600ca00,0x03ffffff,0x04000000,0x0401ffff,0x04020000,0x0403ffff,0x04040000,0xc0000000};
    for(unsigned i=0;i<8192;i++) {
        msh2.p_dram=memory.dram[(i/1024)&1];
        expected_slave=(i/12)&1;expected_pc=0x06003cb4+i*2;
        expected_address=address[i%12] | ((i/24)&1?0x20000000u:0);
        expected_value=i%5?0x80010200u+i:0;expected_size=1<<(i%3);expected_write=(i/48)&1;
        assert(vrde_sh2_report(expected_slave,expected_pc,expected_address,expected_value,
                              expected_size,expected_write)==expected_value);
        if((i&255)==255)states[i/256]=state_hash();
    }
    assert(drawn_types[0] && drawn_types[1]);
    return calls;
}
int main(void)
{
    Pico32xMem=&memory;
    for(unsigned i=0;i<32;i++)((uint16_t *)memory.sdram)[(0xc800+i*16)/2]=(i%31+1)<<8;
    uint64_t full[32],filtered[32];
    unsigned all=replay(VRDE_SH2_TRACE_ALL,full),needed=replay(VRDE_SH2_TRACE_HUD,filtered);
    assert(all==8192 && needed>0 && needed<all/4);
    assert(!memcmp(full,filtered,sizeof(full)));
    vrde_sh2_access_hook=NULL;
    assert(vrde_sh2_report(0,0,0,123,4,1)==123);
    printf("HUD observer: identical state at32 checkpoints; full trace=%u callbacks, HUD filter=%u\n",all,needed);
}
