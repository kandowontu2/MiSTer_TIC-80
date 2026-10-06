#include "Vtic80_cart_loader.h"
#include "tic80_mister/memory_map.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <string>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
struct Sim {
    Vtic80_cart_loader dut;
    std::vector<uint8_t> memory = std::vector<uint8_t>(TM_CART_CAPACITY + 16, 0xCC);
    std::vector<uint8_t> source = std::vector<uint8_t>(8 + TM_CART_SOURCE_CAPACITY + 16, 0xCC);
    unsigned cycle = 0, writes = 0;
    bool held = false;
    uint32_t held_address = 0;
    uint64_t held_data = 0;
    unsigned held_be = 0;
    void step() {
        dut.clk = 0;
        dut.write_ready = dut.write_request && cycle % 11 >= 3;
        dut.eval();
        if (held) CHECK(dut.write_request && dut.write_address == held_address &&
                        dut.write_data == held_data && dut.write_be == held_be);
        held = dut.write_request && !dut.write_ready;
        if (held) { held_address = dut.write_address; held_data = dut.write_data; held_be = dut.write_be; }
        if (dut.write_ready) {
            uint32_t byte_address=dut.write_address*8-TM_PHYSICAL_BASE;
            bool name=byte_address>=TM_CART_SOURCE_OFFSET && byte_address<TM_CART_SOURCE_OFFSET+8+TM_CART_SOURCE_CAPACITY;
            uint32_t offset=byte_address-(name?TM_CART_SOURCE_OFFSET:TM_CART_DATA_OFFSET);
            CHECK(offset < (name?8+TM_CART_SOURCE_CAPACITY:TM_CART_CAPACITY));
            auto &target=name?source:memory;
            for (unsigned i = 0; i < 8; ++i) if (dut.write_be & (1 << i)) target[offset+i] = (uint8_t)(dut.write_data >> (8*i));
            ++writes;
        }
        dut.clk = 1; dut.eval(); ++cycle;
    }
    void wait_ready() {
        unsigned limit = cycle + 100;
        while (((dut.cart_meta & 3) == 1 || dut.write_request) && cycle < limit) step();
        CHECK((dut.cart_meta & 3) == 2 || (dut.cart_meta & 3) == 3);
    }
    void download(unsigned size, bool bad_address = false) {
        std::fill(memory.begin(), memory.end(), 0xCC);
        dut.ioctl_download = 1;
        dut.ioctl_wr = 0;
        unsigned limit = cycle + 100;
        while ((dut.cart_meta & 3) != 1 && cycle < limit) step();
        CHECK((dut.cart_meta & 3) == 1);
        for (unsigned i = 0; i < size; ++i) {
            while (dut.ioctl_wait) step();
            dut.ioctl_addr = bad_address && i == 13 ? 14 : i;
            dut.ioctl_dout = (uint8_t)(i * 37 + 19);
            dut.ioctl_wr = 1; step(); dut.ioctl_wr = 0; step();
        }
        dut.ioctl_download = 0;
        step(); wait_ready();
        unsigned actual = (unsigned)(dut.cart_meta >> 32);
        CHECK(actual <= TM_CART_CAPACITY);
        if (size < 4 || size > TM_CART_CAPACITY || bad_address) CHECK((dut.cart_meta & 3) == 3);
        else {
            CHECK((dut.cart_meta & 3) == 2 && actual == size);
            for (unsigned i = 0; i < size; ++i) CHECK(memory[i] == (uint8_t)(i * 37 + 19));
            for (unsigned i = size; i < memory.size(); ++i) CHECK(memory[i] == 0xCC);
        }
        for (unsigned i = TM_CART_CAPACITY; i < memory.size(); ++i) CHECK(memory[i] == 0xCC);
        for (unsigned i=8+TM_CART_SOURCE_CAPACITY;i<source.size();++i) CHECK(source[i]==0xCC);
        CHECK(source_ticket()==(uint32_t)dut.cart_meta);
    }
    uint32_t source_ticket() { uint32_t value; std::memcpy(&value,source.data(),4); return value; }
    uint32_t source_length() { uint32_t value; std::memcpy(&value,source.data()+4,4); return value; }
    void name_packet(const std::vector<uint8_t> &bytes,bool bad_address=false) {
        dut.ioctl_index=0xfe; dut.ioctl_download=1; dut.ioctl_wr=0; step();
        for(unsigned i=0;i<bytes.size();++i) {
            unsigned limit=cycle+100; while(dut.ioctl_wait && cycle<limit) step(); CHECK(!dut.ioctl_wait);
            dut.ioctl_addr=bad_address && i==10?11:i; dut.ioctl_dout=bytes[i]; dut.ioctl_wr=1; step();
            dut.ioctl_wr=0; step();
        }
        dut.ioctl_download=0; for(unsigned i=0;i<50;++i) step();
    }
};
static std::vector<uint8_t> packet(const char *path,unsigned index) {
    unsigned size=std::strlen(path)+1;
    std::vector<uint8_t> bytes{(uint8_t)TM_CART_SOURCE_MAGIC,(uint8_t)(TM_CART_SOURCE_MAGIC>>8),
        (uint8_t)(TM_CART_SOURCE_MAGIC>>16),(uint8_t)(TM_CART_SOURCE_MAGIC>>24),(uint8_t)index,0,(uint8_t)size,(uint8_t)(size>>8)};
    bytes.insert(bytes.end(),path,path+size); return bytes;
}
int main() {
    Sim s;
    s.dut.reset = 1; s.dut.ioctl_download = 0; s.dut.ioctl_wr = 0;
    s.dut.ioctl_index = 0; s.dut.cart_ack_valid = 0; s.dut.cart_ack = 0x10203002;
    s.step(); s.dut.reset = 0;
    s.dut.ioctl_download = 1;
    for (unsigned i = 0; i < 10; ++i) s.step();
    CHECK(s.dut.ioctl_wait && s.dut.cart_meta == 0);
    s.dut.ioctl_download = 0; s.dut.cart_ack_valid = 1;
    for (unsigned index : {0u, 0x40u}) {
        s.dut.ioctl_index = index;
        for (unsigned size : {0u, 3u, 4u, 7u, 8u, 9u, 13u, 4095u, TM_CART_CAPACITY, TM_CART_CAPACITY+1}) {
            s.download(size);
            uint32_t ticket = (uint32_t)s.dut.cart_meta;
            CHECK(ticket != s.dut.cart_ack);
            // A second transfer must wait until the complete first ticket is ACKed.
            unsigned writes = s.writes;
            s.dut.ioctl_download = 1;
            for (unsigned i = 0; i < 15; ++i) s.step();
            CHECK(s.dut.ioctl_wait && s.writes == writes && (uint32_t)s.dut.cart_meta == ticket);
            s.dut.ioctl_download = 0;
            s.dut.cart_ack = ticket;
            s.step();
        }
    }
    s.download(23, true);
    CHECK((s.dut.cart_meta & 3) == 3);
    s.dut.cart_ack = (uint32_t)s.dut.cart_meta; s.step();
    auto source=packet("/media/fat/games/TIC-80/folder/cart.tic",0);
    s.name_packet(source); s.dut.ioctl_index=0; s.download(13);
    CHECK(s.source_length()==source.size()-8 && !std::memcmp(s.source.data()+8,source.data()+8,source.size()-8));
    // The next filename itself cannot replace an unacknowledged source/cart.
    auto frozen=s.source; unsigned writes=s.writes;
    s.dut.ioctl_index=0xfe; s.dut.ioctl_download=1;
    for(unsigned i=0;i<20;++i) s.step();
    CHECK(s.dut.ioctl_wait && s.source==frozen && s.writes==writes);
    s.dut.ioctl_download=0; s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    s.dut.ioctl_index=0; s.download(9); CHECK(!s.source_length()); // consumed exactly once
    s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    for(unsigned kind=0;kind<10;++kind) {
        auto bad=source;
        if(kind==0) bad[0]^=1;
        if(kind==1) bad[4]=1;
        if(kind==2) bad[5]=1;
        if(kind==3) { bad[6]=1; bad[7]=1; }
        if(kind==4) bad[8]='x';
        if(kind==5) bad.back()='x';
        if(kind==6) bad[10]=0;
        if(kind==7) bad.pop_back();
        if(kind==8) bad.push_back(0);
        s.name_packet(bad,kind==9); s.dut.ioctl_index=0; s.download(8); CHECK(!s.source_length());
        s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    }
    s.name_packet(source); s.dut.ioctl_index=0x40; s.download(9); CHECK(!s.source_length()); // index binding
    s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    std::string longest="/"+std::string(TM_CART_SOURCE_CAPACITY-2,'x');
    auto maximum=packet(longest.c_str(),0x40); s.name_packet(maximum); s.dut.ioctl_index=0x40; s.download(8);
    CHECK(s.source_length()==TM_CART_SOURCE_CAPACITY && !std::memcmp(s.source.data()+8,maximum.data()+8,TM_CART_SOURCE_CAPACITY));
    s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    s.name_packet(source); s.dut.reset=1; s.step(); s.dut.reset=0; s.dut.ioctl_index=0; s.download(8); CHECK(!s.source_length());
    s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    // An index change ends the ownership of a receiving cart even if Main
    // sends no further data before stopping it. A selected PNG index must not
    // silently turn the already received TIC prefix into a complete file.
    for(unsigned changed_index : {0x40u,0xfeu,1u}) {
        s.dut.ioctl_index=0; s.dut.ioctl_download=1; s.dut.ioctl_wr=0; s.step();
        CHECK((s.dut.cart_meta&3)==1);
        for(unsigned i=0;i<13;++i) {
            while(s.dut.ioctl_wait) s.step();
            s.dut.ioctl_addr=i; s.dut.ioctl_dout=i; s.dut.ioctl_wr=1; s.step();
            s.dut.ioctl_wr=0; s.step();
        }
        s.dut.ioctl_index=changed_index;
        for(unsigned i=0;i<20;++i) s.step();
        CHECK((s.dut.cart_meta&3)==1);
        s.dut.ioctl_download=0; s.step(); s.wait_ready();
        CHECK((s.dut.cart_meta&3)==3 && !s.source_length());
        s.dut.cart_ack=(uint32_t)s.dut.cart_meta; s.step();
    }
    for (unsigned index : {1u, 0x41u, 0x80u, 0x100u, 0xffffu}) {
        unsigned writes=s.writes; uint64_t meta=s.dut.cart_meta;
        s.dut.ioctl_index=index; s.dut.ioctl_download=1; s.dut.ioctl_wr=1;
        for (unsigned i=0;i<20;++i) { s.dut.ioctl_addr=i; s.step(); CHECK(!s.dut.ioctl_wait); }
        s.dut.ioctl_download=0; s.dut.ioctl_wr=0; s.step();
        CHECK(s.writes==writes && s.dut.cart_meta==meta);
    }
    std::puts("Cart staging: TIC/PNG indices, other-slot rejection, ACK backpressure, stable writes, partial words, 4 MiB bound, transfer-bound source tags, malformed metadata, source capacity/index/reset and source ACK ownership verified; receiving-index changes reject incomplete prefixes");
}
