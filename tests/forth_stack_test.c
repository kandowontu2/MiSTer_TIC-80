#define _POSIX_C_SOURCE 200809L
#include "tic80_mister/vm.h"
#include "cart.h"
#include "script.h"
#include "api.h"
#include "pf_all.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "forth_stack_cases.h"
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"Forth stack failed line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
/* Independent operand/output counts, not extracted from the staged wrappers. */
static const unsigned required[]={1,8,2,3,5,5,5,9,1,3,7,8,2,3,2,3,1,2,1,2,1,2,3,3,3,1,2,0,0,0,10,0,4,4,5,5,4,7,7,18,4,0,7,3,1,0,1,3,2,3,2,2,2,2,1,1,1,1,1,1,1,1};
static const unsigned outputs[]={0,1,1,0,0,0,0,0,1,1,0,0,1,0,1,0,1,0,1,0,1,0,0,0,0,1,0,1,1,0,1,7,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1};
static tic_mem* current;
ThrowCode __real_pfInitialize(const char*,cell_t,ExecToken*);
ThrowCode __wrap_pfInitialize(const char* dictionary,cell_t init,ExecToken* entry)
{
    ThrowCode result=__real_pfInitialize(dictionary,init,entry);
    if(result) return result;
    /* Prove the bootstrap aliases really exist, rather than accepting a
     * compiler's undefined-word error as evidence of the CALL_C guard. */
    ExecToken token;
    CHECK(ffFindC("CTEST0",&token)&&ffFindC("CTEST1",&token));
    static const char* names[]={"TMGLUEUNMARKED","TMGLUETYPED","TMGLUEINDEX"};
    const ucell_t packed[]={0,((ucell_t)2<<16)|((ucell_t)1<<24),((ucell_t)1<<16)|62};
    for(unsigned i=0;i<3;++i) {
        char name[PF_NAME_SIZE_SAFE]; CStringToForth(name,names[i],sizeof name);
        ffCreateSecondaryHeader(name); CODE_COMMA(ID_CALL_C); CODE_COMMA(packed[i]); ffFinishSecondary();
    }
    return 0;
}
static unsigned errors;
static char diagnostic[1024];
static void error(const char* text) { ++errors; size_t used=strlen(diagnostic); snprintf(diagnostic+used,sizeof diagnostic-used,"%s\n",text); fprintf(stderr,"FORTH_STACK_ERROR: %s\n",text); }
void __real_tic80_tick(tic80*,tic80_input,u64 (*)(void*),u64 (*)(void*));
void __wrap_tic80_tick(tic80* tic,tic80_input input,u64 (*count)(void*),u64 (*freq)(void*))
{ current=(tic_mem*)tic; __real_tic80_tick(tic,input,count,freq); }
cell_t __real_CallUserFunction(cell_t,int32_t,int32_t);
cell_t __wrap_CallUserFunction(cell_t index,int32_t mode,int32_t parameters)
{
    CHECK(index>=0&&index<62&&parameters==0);
    CHECK(mode==(outputs[index]==1));
    cell_t depth=pfGetStackDepth();
    if(depth<(cell_t)required[index]) fprintf(stderr,"FORTH_STACK_BAD_DISPATCH index=%ld required=%u depth=%ld\n",(long)index,required[index],(long)depth);
    CHECK(depth>=(cell_t)required[index]);
    if((index==25||index==26)&&gCurrentTask->td_StackPtr[0]>=244)
        return __real_CallUserFunction(index,mode,parameters);
    CHECK(current&&current->ram->persistent.data[249]==(u32)index);
    u32* pmem=current->ram->persistent.data;
    ++pmem[250]; pmem[251]=(u32)index; pmem[252]=0x43414c00u+(u32)index;
    if(index==25||index==26) return __real_CallUserFunction(index,mode,parameters);
    /* Double the C native boundary; real Forth glue/stack/CATCH and workers run. */
    for(unsigned i=0;i<required[index];++i) pfPopFromStack();
    for(unsigned i=0;i<outputs[index];++i) pfPushToStack(0);
    return 0;
}
static u64 counter(void* ignored) { (void)ignored; return 1; }
static u64 frequency(void* ignored) { (void)ignored; return 60; }
static u8* cartridge(const char* code,s32* size)
{
    tic_cartridge* cart=calloc(1,sizeof *cart); CHECK(cart);
    FOREACH_LANG(s) if(!strcmp(s->name,"forth")) cart->lang=s->id;
    CHECK(strlen(code)<sizeof cart->code.data); strcpy(cart->code.data,code);
    u8* bytes=malloc(sizeof *cart*2); CHECK(bytes); *size=tic_cart_save(cart,bytes); CHECK(*size>0); free(cart); return bytes;
}
static void product(tic80* tic,unsigned index,unsigned kind,unsigned frames)
{
    const u32* pmem=((tic_mem*)tic)->ram->persistent.data;
    CHECK(pmem[249]==index);
    CHECK(pmem[250]==(kind<2?frames:0));
    CHECK(pmem[251]==(kind<2?index:0)&&pmem[252]==(kind<2?0x43414c00u+index:0));
    CHECK(pmem[244]==(kind==2?0:7));
    CHECK(pmem[245]==(kind==1?91:0)&&pmem[246]==(kind==1?92:0));
    CHECK(pmem[248]==(kind==3?(u32)-4:0));
    CHECK(tic->samples.count==1600);
    for(unsigned i=0;i<1600;++i) CHECK(!tic->samples.buffer[i]);
}
static void recovery(void)
{
    s32 size; u8* bytes=cartridge(": TIC 25 249 PMEM! 0 PMEM DROP 7 244 PMEM! ;\n",&size);
    tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes);
    CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK); product(tm_vm_product(vm),25,0,1);
    tm_vm_close(vm); CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
}
static void rejected_glue(void)
{
    static const char* words[]={"CTEST0","CTEST1","TMGLUEUNMARKED","TMGLUETYPED","TMGLUEINDEX"};
    for(unsigned i=0;i<5;++i) {
        char code[256];
        snprintf(code,sizeof code,": BAD %s ;\n: TIC 0 249 PMEM! 91 92 ['] BAD CATCH 248 PMEM! 246 PMEM! 245 PMEM! 7 244 PMEM! ;\n",words[i]);
        s32 size; u8* bytes=cartridge(code,&size);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct);
        direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes); errors=0; diagnostic[0]=0;
        for(unsigned frame=0;frame<3;++frame) {
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct); CHECK(!errors);
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==TM_VM_OK);
            tic80* products[]={direct,tm_vm_product(vm)};
            for(unsigned p=0;p<2;++p) {
                const u32* pmem=((tic_mem*)products[p])->ram->persistent.data;
                CHECK(pmem[248]==(u32)-13&&pmem[244]==7&&pmem[245]==91&&pmem[246]==92);
                CHECK(!pmem[250]&&!pmem[251]&&!pmem[252]);
            }
        }
        tm_vm_close(vm); tic80_delete(direct); current=NULL;
        CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD);
        printf("FORTH_GLUE_PASS word=%s direct_worker=1 no_dispatch=1 throw=-13 caller_preserved=1\n",words[i]);
    }
}
int main(int argc,char** argv)
{
    if(argc>1&&!strcmp(argv[1],"--vm-worker")) return tm_vm_worker(argc,argv);
    if(argc==2&&!strcmp(argv[1],"--glue")) { rejected_glue(); return 0; }
    CHECK(argc==1||(argc==3&&!strcmp(argv[1],"--case"))); CHECK(!setvbuf(stdout,NULL,_IOLBF,0));
    unsigned counts[4]={0},seen[62][4][18]={0};
    CHECK(sizeof required/sizeof *required==62&&sizeof outputs/sizeof *outputs==62);
    for(unsigned c=0;c<sizeof forth_stack_cases/sizeof *forth_stack_cases;++c) {
        const unsigned index=forth_stack_cases[c].word,kind=forth_stack_cases[c].kind,provided=forth_stack_cases[c].provided;
        const char* name=forth_stack_cases[c].name;
        if(argc==3&&strcmp(name,argv[2])) continue;
        CHECK(index<62&&kind<4&&provided<=required[index]);
        unsigned position=kind<2?0:provided; CHECK(!seen[index][kind][position]++);
        CHECK(kind<2?provided==required[index]:provided<required[index]);
        printf("FORTH_STACK_START case=%s\n",name);
        s32 size; u8* bytes=cartridge(forth_stack_cases[c].source,&size);
        tic80* direct=tic80_create(48000,TIC80_PIXEL_COLOR_RGBA8888); CHECK(direct); direct->callback.error=error; tic80_load(direct,bytes,size);
        tm_vm* vm=NULL; CHECK(tm_vm_open(&vm,bytes,size)==TM_VM_OK); free(bytes); errors=0; diagnostic[0]=0;
        unsigned frames=kind==2?1:3;
        for(unsigned frame=0;frame<frames;++frame) {
            tic80_tick(direct,(tic80_input){0},counter,frequency); tic80_sound(direct);
            if(kind==2) CHECK(errors>0&&strstr(diagnostic,"THROW -4")); else CHECK(!errors);
            product(direct,index,kind,frame+1);
            CHECK(tm_vm_tick(vm,(tic80_input){0},0)==(kind==2?TM_VM_ERROR:TM_VM_OK)); product(tm_vm_product(vm),index,kind,frame+1);
        }
        tm_vm_close(vm); tic80_delete(direct); current=NULL;
        if(kind==2) recovery();
        CHECK(waitpid(-1,NULL,WNOHANG)==-1&&errno==ECHILD); ++counts[kind];
        printf("FORTH_STACK_PASS case=%s direct_worker=1 before_native=1 catch_semantics=1\n",name);
    }
    if(argc==1) for(unsigned index=0;index<62;++index) for(unsigned kind=0;kind<4;++kind) for(unsigned i=0;i<18;++i)
        CHECK(seen[index][kind][i]==(kind<2?i==0:i<required[index]));
    else CHECK(counts[0]+counts[1]+counts[2]+counts[3]==1);
    if(argc==1) rejected_glue();
    printf("Forth native stack contract: words=62 exact=%u surplus=%u unhandled=%u caught=%u; no dispatch on underflow, THROW -4, CATCH, preserved caller cells and exec workers passed\n",counts[0],counts[1],counts[2],counts[3]);
    return 0;
}
