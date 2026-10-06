#include "Vtic80_cart_loader.h"
#include "../tools/main_source_packet.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
struct Sim {
    Vtic80_cart_loader dut;
    unsigned cycle=0,writes=0;
    std::vector<uint8_t> memory=std::vector<uint8_t>(64,0xcc);
    void step() {
        dut.clk=0; dut.write_ready=dut.write_request && cycle%11>=3; dut.eval();
        if(dut.write_ready) {
            unsigned offset=dut.write_address*8-TM_PHYSICAL_BASE-TM_CART_DATA_OFFSET;
            CHECK(offset+8<=memory.size());
            for(unsigned i=0;i<8;++i) if(dut.write_be&(1<<i)) memory[offset+i]=(uint8_t)(dut.write_data>>(8*i));
            ++writes;
        }
        dut.clk=1; dut.eval(); ++cycle;
    }
    void metadata(unsigned index) {
        uint8_t bytes[8+TM_CART_SOURCE_CAPACITY]; size_t size=tm_main_source_packet("/media/fat/games/TIC-80/cart.tic",index,bytes);
        unsigned before=writes; uint64_t meta=dut.cart_meta; auto frozen=memory;
        dut.ioctl_index=0xfe; dut.ioctl_download=1; step();
        for(unsigned i=0;i<size;++i) {
            CHECK(!dut.ioctl_wait); dut.ioctl_addr=i; dut.ioctl_dout=bytes[i]; dut.ioctl_wr=1; step(); dut.ioctl_wr=0; step();
        }
        dut.ioctl_download=0; for(unsigned i=0;i<20;++i) step();
        CHECK(writes==before && dut.cart_meta==meta && memory==frozen);
    }
};
int main() {
    Sim s; s.dut.reset=1; s.dut.ioctl_download=0; s.dut.ioctl_wr=0; s.dut.ioctl_index=0;
    s.dut.cart_ack_valid=1; s.dut.cart_ack=0; s.step(); s.dut.reset=0;
    for(unsigned index:{0u,0x40u}) {
        s.metadata(index); s.dut.ioctl_index=index; s.dut.ioctl_download=1; s.step();
        for(unsigned i=0;i<13;++i) {
            while(s.dut.ioctl_wait) s.step(); s.dut.ioctl_addr=i; s.dut.ioctl_dout=i+1;
            s.dut.ioctl_wr=1; s.step(); s.dut.ioctl_wr=0; s.step();
        }
        s.dut.ioctl_download=0; unsigned limit=s.cycle+100;
        while(((s.dut.cart_meta&3)==1 || s.dut.write_request) && s.cycle<limit) s.step();
        CHECK((s.dut.cart_meta&3)==2 && (s.dut.cart_meta>>32)==13);
        for(unsigned i=0;i<13;++i) CHECK(s.memory[i]==i+1);
        uint32_t ticket=(uint32_t)s.dut.cart_meta;
        // Patched Main may send the next metadata packet before the old cart
        // has been ACKed. The old loader ignores it without disturbing staging.
        s.metadata(index); s.dut.cart_ack=ticket; s.step();
    }
    std::puts("Legacy cart loader: patched Main source packets ignored before/after ACK; native/PNG carts and owned staging remain unchanged");
}
