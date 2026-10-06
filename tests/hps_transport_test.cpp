#include "Vhps_transport_top.h"
#include "tic80_mister/memory_map.h"
#include "user_io.h"
#include "mra_loader.h"
#include "main_horizontal_wheel.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(c) do { if(!(c)) { std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
void tm_main_metadata(const char*,unsigned char,char,uint32_t);
void tm_main_mgl_complete();
void tm_main_bus_restart();
void user_io_tic80_horizontal_wheel(int32_t);
static bool osd_visible=false;
char user_io_osd_is_visible() { return osd_visible; }
char is_st() { return 0; }
static std::string core="TIC-80", path;
static bool missing_path=false;
char* user_io_get_core_name(int) { return core.data(); }
const char* getFullPath(const char*) { return missing_path?nullptr:path.c_str(); }
void fpga_wait_to_reset() { CHECK(false); }
struct Sim {
    Vhps_transport_top dut;
    std::vector<uint8_t> cart=std::vector<uint8_t>(TM_CART_CAPACITY+16,0xcc);
    std::vector<uint8_t> source=std::vector<uint8_t>(8+TM_CART_SOURCE_CAPACITY+16,0xcc);
    uint32_t gpo=0x80000000, previous_ticket=0;
    unsigned cycle=0,reads=0,writes=0,ready_at=0,ack_at=0,busy_until=0;
    bool frozen=false,legacy=false;
    std::vector<uint8_t> frozen_cart,frozen_source;
    std::vector<uint32_t> publications;
    uint32_t source_ticket() { uint32_t value; std::memcpy(&value,source.data(),4); return value; }
    uint32_t source_length() { uint32_t value; std::memcpy(&value,source.data()+4,4); return value; }
    void step() {
        CHECK(cycle<200000);
        if(ready_at && cycle>=ready_at) { dut.cart_ack_valid=1; ready_at=0; }
        if(ack_at && cycle>=ack_at && (dut.cart_meta&3)>=2) { dut.cart_ack=(uint32_t)dut.cart_meta; ack_at=0; frozen=false; }
        dut.gp_out=gpo; dut.clk_sys=0;
        dut.write_ready=dut.write_request && cycle>=busy_until && cycle%17>=9;
        dut.eval();
        if(frozen) CHECK(cart==frozen_cart && source==frozen_source);
        if(dut.write_ready) {
            uint32_t address=dut.write_address*8-TM_PHYSICAL_BASE;
            bool name=address>=TM_CART_SOURCE_OFFSET && address<TM_CART_SOURCE_OFFSET+8+TM_CART_SOURCE_CAPACITY;
            uint32_t offset=address-(name?TM_CART_SOURCE_OFFSET:TM_CART_DATA_OFFSET);
            auto &target=name?source:cart;
            CHECK(offset<(name?8+TM_CART_SOURCE_CAPACITY:TM_CART_CAPACITY));
            for(unsigned i=0;i<8;++i) if(dut.write_be&(1<<i)) target[offset+i]=(uint8_t)(dut.write_data>>(i*8));
            ++writes;
        }
        dut.clk_sys=1; dut.eval(); ++cycle;
        uint32_t ticket=(uint32_t)dut.cart_meta;
        if((ticket&3)>=2 && ticket!=previous_ticket) {
            if(!legacy) CHECK(source_ticket()==ticket);
            publications.push_back(ticket); previous_ticket=ticket;
        }
        for(unsigned i=TM_CART_CAPACITY;i<cart.size();++i) CHECK(cart[i]==0xcc);
        for(unsigned i=8+TM_CART_SOURCE_CAPACITY;i<source.size();++i) CHECK(source[i]==0xcc);
    }
    void idle(unsigned count=40) { while(count--) step(); }
    void acknowledge() { dut.cart_ack=(uint32_t)dut.cart_meta; frozen=false; idle(); }
    void freeze() { frozen_cart=cart; frozen_source=source; frozen=true; }
    void check(const std::vector<uint8_t> &bytes,const std::string &expected) {
        idle(); CHECK((dut.cart_meta&3)==2 && (dut.cart_meta>>32)==bytes.size());
        CHECK(!std::memcmp(cart.data(),bytes.data(),bytes.size()));
        if(legacy) { for(auto byte:source) CHECK(byte==0xcc); }
        else {
            CHECK(source_length()==(expected.empty()?0:expected.size()+1));
            if(!expected.empty()) CHECK(!std::memcmp(source.data()+8,expected.c_str(),expected.size()+1));
        }
    }
};
static Sim* sim;
struct MainDeparture {};
static unsigned bus_events=0, departure_event=0;
static bool count_bus=false, interrupt_bus=false;
static void bus_boundary() {
    if(!count_bus) return;
    if(interrupt_bus && bus_events==departure_event) throw MainDeparture{};
    ++bus_events;
}
uint32_t tm_gpo_read() { return sim->gpo; }
void tm_gpo_write(uint32_t value) { bus_boundary(); sim->gpo=value; sim->step(); }
int tm_gpi_read() { bus_boundary(); sim->step(); ++sim->reads; return (int)sim->dut.gp_in; }
static void metadata(const std::string &name,unsigned index=0) {
    path=name; missing_path=false; tm_main_metadata("cart.tic",index,0,0);
}
static void transfer(const std::vector<uint8_t> &bytes,unsigned index=0) {
    user_io_set_index(index); user_io_file_info(index?".PNG":".TIC");
    user_io_set_download(1); user_io_file_tx_data(bytes.data(),bytes.size()); user_io_set_download(0);
}
static mgl_struct& parse_mgl(const char *items) {
    FILE *xml=std::fopen("initial.mgl","w"); CHECK(xml);
    std::fputs("<mistergamedescription>",xml); std::fputs(items,xml);
    std::fputs("</mistergamedescription>",xml); CHECK(std::fclose(xml)==0);
    return *mgl_parse("initial.mgl");
}
static void abrupt_restart_cases() {
    // Interrupt before every GPIO register operation, retaining the last real
    // register value. No artificial chip-select drop or FPGA reset occurs at
    // death. Restart uses Main's initialization write and core-ID exchange.
    const std::string old_path="/media/fat/games/TIC-80/interrupted.tic";
    const std::string new_path="/media/usb1/games/TIC-80/retry.PNG";
    std::vector<uint8_t> old_bytes(21),new_bytes(9);
    for(unsigned i=0;i<old_bytes.size();++i) old_bytes[i]=(uint8_t)(37*i+19);
    for(unsigned i=0;i<new_bytes.size();++i) new_bytes[i]=(uint8_t)(53*i+7);
    unsigned total=0, asserted_cs=0, asserted_strobe=0, receiving=0, completed=0;
    auto initialize=[](Sim &s) {
        sim=&s; s.dut.cart_ack_valid=1; s.dut.cart_ack=0;
        s.dut.reset=1; s.idle(); s.dut.reset=0; s.idle();
    };
    {
        Sim baseline; initialize(baseline);
        bus_events=0; count_bus=true; interrupt_bus=false;
        metadata(old_path); transfer(old_bytes);
        count_bus=false; total=bus_events;
        baseline.check(old_bytes,old_path);
    }
    CHECK(total>200);
    for(unsigned cut=0;cut<=total;++cut) {
        Sim s; initialize(s);
        bus_events=0; departure_event=cut; count_bus=true; interrupt_bus=true;
        bool departed=false;
        try { metadata(old_path); transfer(old_bytes); }
        catch(const MainDeparture&) { departed=true; }
        count_bus=false; interrupt_bus=false;
        CHECK(departed==(cut<total));
        CHECK(bus_events==cut);
        asserted_cs+=(s.gpo&(1u<<18))!=0;
        asserted_strobe+=(s.gpo&(1u<<17))!=0;
        // FPGA/DDR continue with the GPIO register held exactly as left.
        uint32_t held=s.gpo; s.idle(80); CHECK(s.gpo==held);
        const uint32_t before=(uint32_t)s.dut.cart_meta;
        if((before&3)==2) {
            ++completed; s.check(old_bytes,old_path);
        } else {
            CHECK((before&3)<2); receiving+=(before&3)==1;
        }
        // A bounded modeled consumer acknowledges completed/error tickets.
        // This does not write bytes or repair the transport on Main's behalf.
        s.ack_at=s.cycle+400;
        tm_main_bus_restart(); CHECK(s.dut.reset==0);
        metadata(new_path,0x40); transfer(new_bytes,0x40);
        s.check(new_bytes,new_path);
        CHECK(!s.publications.empty() && (s.publications.back()&3)==2);
        unsigned ready=0;
        for(uint32_t ticket:s.publications) {
            if((ticket&3)==2) ++ready;
            else CHECK((ticket&3)==3);
        }
        // A transfer that already completed may publish once; an unfinished
        // prefix must never become a successful old cartridge during retry.
        CHECK(ready==1+((before&3)==2));
        s.acknowledge();
        std::printf("abrupt-cut %u/%u gpio=%08x before=%u publications=%zu ready=%u retry_bytes=%zu\n",
                    cut,total,held,before,s.publications.size(),ready,new_bytes.size());
    }
    CHECK(asserted_cs && asserted_strobe && receiving && completed);
    std::printf("Abrupt Main GPIO departure: %u cuts, CS asserted %u, strobe asserted %u, receiving %u, completed %u; no reset at death, no successful prefixes, exact retry/source ownership passed\n",
                total+1,asserted_cs,asserted_strobe,receiving,completed);
}
int main(int argc,char**argv) {
    CHECK(argc==3); Sim state; sim=&state; state.legacy=!std::strcmp(argv[2],"legacy");
    state.dut.cart_ack_valid=1; state.dut.cart_ack=0; state.dut.reset=1; state.idle(); state.dut.reset=0; state.idle();
    std::string scenario=argv[1];
    std::vector<uint8_t> bytes(21); for(unsigned i=0;i<bytes.size();++i) bytes[i]=(uint8_t)(i*37+19);
    const std::string first="/media/fat/games/TIC-80/a.tic", second="/media/usb1/games/TIC-80/next.PNG";
    if(scenario=="stock") {
        CHECK(!state.legacy);
        user_io_status_set("[0]",0);
        CHECK(!(state.dut.status[0]&1));
        unsigned cases=0;
        for(unsigned index:{0u,0x40u}) for(unsigned length:{21u,129u,256u,513u}) {
            std::vector<uint8_t> plain(length);
            for(unsigned i=0;i<length;++i) plain[i]=(uint8_t)(i*53+length);
            unsigned before=state.reads;
            if(cases) {
                state.freeze();
                state.ack_at=state.cycle+400;
            }
            // The unmodified Main helpers send only the standard file index,
            // extension and download bytes. No reserved metadata transfer.
            transfer(plain,index);
            if(cases) CHECK(state.reads>before+300);
            state.check(plain,"");
            CHECK(state.source_ticket()==(uint32_t)state.dut.cart_meta);
            for(unsigned i=8;i<state.source.size();++i) CHECK(state.source[i]==0xcc);
            ++cases;
        }
        CHECK(cases==8 && state.publications.size()==8);
        state.acknowledge();
        std::puts("Stock Main: eight native/PNG-index transfers, exact bytes, delayed ACK ownership and empty source metadata passed; no Main extensions invoked");
    } else if(scenario=="abrupt-restart") {
        CHECK(!state.legacy);
        abrupt_restart_cases();
    } else if(scenario=="horizontal") {
        tm_main_horizontal_wheel coarse={}, fine={}; fine.high_resolution=1;
        auto event=[&](tm_main_horizontal_wheel &s,unsigned type,unsigned code,int32_t value) {
            int32_t delta=tm_main_horizontal_event(&s,type,code,value,1);
            if(delta) user_io_tic80_horizontal_wheel(delta);
            state.idle();
        };
        event(coarse,EV_REL,REL_HWHEEL,3); CHECK(state.dut.horizontal_wheel==0);
        event(coarse,EV_SYN,SYN_REPORT,0); CHECK(state.dut.horizontal_wheel==3);
        event(coarse,EV_REL,REL_HWHEEL,-5); event(coarse,EV_SYN,SYN_REPORT,0);
        CHECK(state.dut.horizontal_wheel==0xfffffffe);
        for(unsigned i=0;i<4;++i) {
            event(fine,EV_REL,REL_HWHEEL_HI_RES,30); event(fine,EV_SYN,SYN_REPORT,0);
            event(fine,EV_REL,REL_HWHEEL,1); event(fine,EV_SYN,SYN_REPORT,0);
            CHECK(state.dut.horizontal_wheel==(i==3?0xffffffff:0xfffffffe));
        }
        user_io_tic80_horizontal_wheel(INT32_MIN); state.idle();
        CHECK(state.dut.horizontal_wheel==0x7fffffff);
        user_io_tic80_horizontal_wheel(INT32_MIN); state.idle();
        CHECK(state.dut.horizontal_wheel==0xffffffff);
        unsigned reads=state.reads;
        core="OTHER"; user_io_tic80_horizontal_wheel(7); CHECK(state.reads==reads);
        core="TIC-80"; osd_visible=true; user_io_tic80_horizontal_wheel(9); CHECK(state.reads==reads);
        osd_visible=false; state.dut.osd_open=1; state.idle();
        user_io_tic80_horizontal_wheel(11); state.idle(); CHECK(state.dut.horizontal_wheel==0xffffffff);
        state.dut.osd_open=0; state.idle();
        user_io_tic80_horizontal_wheel(1); state.idle(); CHECK(state.dut.horizontal_wheel==0);
        // An ordinary cart transaction after the extension proves that hps_io
        // and the loader still own their normal command/data transport.
        metadata(first); transfer(bytes); state.check(bytes,first);
    } else if(scenario=="normal") {
        state.dut.cart_ack_valid=0; state.ready_at=state.cycle+300;
        metadata(first); transfer(bytes); state.check(bytes,first); state.acknowledge();
        metadata(second,0x40); bytes.resize(9); transfer(bytes,0x40); state.check(bytes,second); state.acknowledge();
        metadata("/media/fat/games/TIC-80/\xcf\x80.tic"); transfer(bytes); state.check(bytes,path); state.acknowledge();
        metadata("/"+std::string(TM_CART_SOURCE_CAPACITY-2,'x')); transfer(bytes); state.check(bytes,path); state.acknowledge();
        metadata(first); missing_path=true; tm_main_metadata("cart.tic",0,0,0); transfer(bytes); state.check(bytes,""); state.acknowledge();
        core="OTHER"; unsigned writes=state.writes; tm_main_metadata("cart.tic",0,0,0); CHECK(state.writes==writes);
        core="TIC-80"; tm_main_metadata("cart.tic",1,0,0); CHECK(state.writes==writes);
    } else if(scenario=="finish") {
        mgl_struct &mgl=parse_mgl("<file delay=\"3\" type=\"f\" index=\"0\" path=\"/media/fat/a.tic\"/>");
        CHECK(mgl.count==1 && mgl.item[0].type=='F' && mgl.item[0].delay==3);
        for(unsigned bit=0;bit<128;++bit) {
            const std::string option="["+std::to_string(bit)+"]";
            user_io_status_set(option.c_str(),bit%3==1);
        }
        user_io_status_set("[0]",0);
        CHECK(user_io_status_get("[0]")==0 && (state.dut.status[0]&1)==1);
        const uint32_t held_status[4]={state.dut.status[0],state.dut.status[1],state.dut.status[2],state.dut.status[3]};
        metadata(first);
        user_io_set_index(0); user_io_file_info(".TIC"); user_io_set_download(1);
        user_io_file_tx_data(bytes.data(),bytes.size());
        state.busy_until=state.cycle+500;
        // Main must not return from DN=0 and release its temporary MGL reset
        // until the final data/source DDR writes and ready ticket have drained.
        user_io_set_download(0);
        CHECK(state.cycle>=state.busy_until);
        CHECK((state.dut.cart_meta&3)==2 && state.source_ticket()==(uint32_t)state.dut.cart_meta);
        CHECK(state.dut.status[0]&1);
        mgl.state=3; tm_main_mgl_complete();
        for(unsigned word=0;word<4;++word) CHECK(state.dut.status[word]==(held_status[word] & (word==0?~1u:~0u)));
        // Failed initial-file completion drops the temporary overlay, while an
        // explicit held reset survives. Other cores and later files bypass it.
        mgl.state=0; user_io_status_set("[0]",1);
        mgl.state=3; tm_main_mgl_complete(); CHECK(state.dut.status[0]&1);
        mgl.count=2; mgl.current=1; mgl.state=0;
        user_io_status_set("[0]",0); CHECK(!(state.dut.status[0]&1));
        mgl.current=0; core="OTHER";
        user_io_status_set("[0]",0); CHECK(!(state.dut.status[0]&1));
        core="TIC-80"; mgl.count=0;
        parse_mgl("<file delay=\"3\" type=\"F\" index=\"64\" path=\"/media/fat/a.PNG\"/>");
        CHECK(mgl.count==1 && mgl.item[0].type=='F' && mgl.item[0].index==64);
        user_io_status_set("[0]",0); CHECK(state.dut.status[0]&1);
        mgl.state=3; tm_main_mgl_complete(); CHECK(!(state.dut.status[0]&1));
        parse_mgl("<file type=\"f\" index=\"0\" path=\"/media/fat/a.tic\"/>");
        CHECK(mgl.count==0); user_io_status_set("[0]",0); CHECK(!(state.dut.status[0]&1));
        parse_mgl("<file delay=\"3\" type=\"s\" index=\"0\" path=\"/media/fat/a.tic\"/>");
        CHECK(mgl.count==1 && mgl.item[0].type=='S'); user_io_status_set("[0]",0); CHECK(!(state.dut.status[0]&1));
        parse_mgl("<reset delay=\"0\" hold=\"1\"/>");
        CHECK(mgl.count==1 && mgl.item[0].action==MGL_ACTION_RESET);
        user_io_status_set("[0]",0); CHECK(!(state.dut.status[0]&1));
        state.check(bytes,first);
    } else if(scenario=="source-restart") {
        // Main dies with an unfinished metadata download; HPS download state
        // persists after chip select falls. A fresh Main must retain the new path.
        user_io_set_index(0xfe); user_io_set_download(1);
        const uint8_t partial[]={0x54,0x53,0x4e,0x31,0,0,40,0,'/','o','l','d'};
        user_io_file_tx_data(partial,sizeof partial); state.gpo=0x80000000; state.idle();
        CHECK(state.dut.ioctl_download);
        metadata(first); transfer(bytes); state.check(bytes,first);
    } else if(scenario=="cart-restart") {
        metadata(first); user_io_set_index(0); user_io_set_download(1);
        user_io_file_tx_data(bytes.data(),13); state.gpo=0x80000000; state.idle();
        CHECK((state.dut.cart_meta&3)==1); state.ack_at=state.cycle+400;
        metadata(second,0x40); transfer(bytes,0x40); state.check(bytes,second);
        CHECK(state.publications.size()==2 && (state.publications[0]&3)==3);
    } else if(scenario=="ack") {
        metadata(first); transfer(bytes); state.check(bytes,first); state.freeze();
        state.ack_at=state.cycle+400; unsigned before=state.reads;
        metadata(second,0x40); CHECK(state.reads>before+300);
        transfer(bytes,0x40); state.check(bytes,second);
    } else CHECK(false);
    std::printf("Main/HPS cart transport %s %s: real SPI handshake, pinned hps_io, stalled DDR, source/cart bytes and ticket publication passed\n",argv[1],argv[2]);
}
