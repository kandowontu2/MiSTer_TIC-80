#define _GNU_SOURCE
/* Exercise real frontend workers with frozen upstream cartridges and references. */
#include "tic80.h"
#ifdef TM_TEST_STUDIO
#include "tic80_mister/studio_session.h"
#include "tic80_mister/spawn_private.h"
#include "studio/studio.h"
#else
#include "tic80_mister/vm.h"
#endif
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"runtime production failed line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned spawns;
static void verify_worker(pid_t pid,const char *target)
{
    char link[64],actual[1024];snprintf(link,sizeof link,"/proc/%ld/exe",(long)pid);
    ssize_t length=readlink(link,actual,sizeof actual-1);CHECK(length>0);actual[length]=0;
    CHECK(!strcmp(actual,target));++spawns;
    printf("production worker executable verified: %s PID %ld\n",actual,(long)pid);
}
#ifdef TM_TEST_STUDIO
int __real_tm_spawn_closed(pid_t *,const tm_spawn_request *);
int __wrap_tm_spawn_closed(pid_t *pid,const tm_spawn_request *request)
{
    const char *target=getenv("TM_PRODUCTION_WORKER");
    if(!target || target[0]!='/' || !request || strcmp(request->file,"/proc/self/exe")
        || !request->argv || !request->argv[1] || strcmp(request->argv[1],"--studio-worker"))return EINVAL;
    tm_spawn_request redirected=*request;redirected.file=target;
    int result=__real_tm_spawn_closed(pid,&redirected);
    if(!result)verify_worker(*pid,target);
    return result;
}
#else
int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const [],char *const []);
int __wrap_posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attributes,char *const argv[],char *const environment[])
{
    const char *target=getenv("TM_PRODUCTION_WORKER");
    if(!target || target[0]!='/' || strcmp(path,"/proc/self/exe") || !argv || !argv[1] || strcmp(argv[1],"--vm-worker"))return EINVAL;
    int result=__real_posix_spawn(pid,target,actions,attributes,argv,environment);
    if(!result)verify_worker(*pid,target);
    return result;
}
#endif
static unsigned char *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);
    CHECK(n>0 && n<=4*1024*1024);rewind(f);unsigned char *data=malloc((size_t)n);CHECK(data);
    CHECK(fread(data,1,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));*size=(size_t)n;return data;
}
static void compare_frame(const u32 *actual,const char *reference,const char *output)
{
    size_t size;unsigned char *expected=read_file(reference,&size);CHECK(size==256*144*4);
    FILE *f=fopen(output,"wb");CHECK(f);CHECK(fwrite(actual,1,size,f)==size);CHECK(!fclose(f));
    size_t differences=0;for(size_t n=0;n<size;++n)differences+=expected[n]!=((const unsigned char *)actual)[n];
    if(differences)fprintf(stderr,"Frame %s differs from %s in %zu RGBA channels\n",output,reference,differences);
    free(expected);CHECK(!differences);
}
int main(int argc,char **argv)
{
    CHECK(argc==4);setvbuf(stdout,NULL,_IONBF,0);
    const char *languages[]={"lua","js","moon","yue","fennel","scheme","squirrel","python","wren","janet","wasm","ruby","miniscript","forth"};
    const char *formats[]={"native","png","legacy"},*suffixes[]={".tic",".png","-legacy.png"};
    for(unsigned language=0;language<14;++language)for(unsigned format=0;format<3;++format) {
        char cart[1024],audio_path[1024],config[1024];
        CHECK(snprintf(cart,sizeof cart,"%s/carts/%s%s",argv[1],languages[language],suffixes[format])<(int)sizeof cart);
        CHECK(snprintf(audio_path,sizeof audio_path,"%s/goldens/%s-60.s16le",argv[1],languages[language])<(int)sizeof audio_path);
        size_t cart_size,audio_size;unsigned char *bytes=read_file(cart,&cart_size),*audio=read_file(audio_path,&audio_size);
        CHECK(audio_size==60*1600*sizeof(s16));
#ifdef TM_TEST_STUDIO
        CHECK(snprintf(config,sizeof config,"%s/%s-%s-config",argv[3],languages[language],formats[format])<(int)sizeof config);
        CHECK(!mkdir(config,0700));
        tm_studio_session *session=NULL;CHECK(tm_studio_session_open(&session,config)==TM_STUDIO_OK);
        pid_t original=tm_studio_session_pid(session);CHECK(original>0);
        CHECK(tm_studio_session_load(session,bytes,cart_size,cart)==TM_STUDIO_OK);
        CHECK(tm_studio_session_run(session,5000)==TM_STUDIO_OK);CHECK(tm_studio_session_mode(session)==TIC_RUN_MODE);
#else
        (void)config;tm_vm *vm=NULL;CHECK(tm_vm_open(&vm,bytes,cart_size)==TM_VM_OK);
#endif
        free(bytes);
        for(unsigned tick=0;tick<60;++tick) {
            const u32 *screen;const s16 *samples;tic80_input input={0};
#ifdef TM_TEST_STUDIO
            input.mouse.x=128;input.mouse.y=72;
            if(tick)CHECK(tm_studio_session_tick(session,input,UINT64_MAX,2000)==TM_STUDIO_OK);
            CHECK(tm_studio_session_mode(session)==TIC_RUN_MODE && tm_studio_session_pid(session)==original);
            screen=tm_studio_session_screen(session);samples=tm_studio_session_audio(session);
#else
            CHECK(tm_vm_tick(vm,input,0)==TM_VM_OK);tic80 *product=tm_vm_product(vm);CHECK(product && product->samples.count==1600);
            screen=product->screen;samples=product->samples.buffer;
#endif
            CHECK(screen && samples);
            if(memcmp(samples,audio+(size_t)tick*1600*sizeof(s16),1600*sizeof(s16)))
                fprintf(stderr,"Audio mismatch for %s/%s at tick %u\n",languages[language],formats[format],tick+1);
            CHECK(!memcmp(samples,audio+(size_t)tick*1600*sizeof(s16),1600*sizeof(s16)));
            if(tick==29 || tick==59) {
                char reference[1024],output[1024];
#ifdef TM_TEST_STUDIO
                CHECK(snprintf(reference,sizeof reference,"%s/%s-%u.rgba",argv[2],languages[language],tick+1)<(int)sizeof reference);
#else
                CHECK(snprintf(reference,sizeof reference,"%s/goldens/%s-%u.rgba",argv[1],languages[language],tick+1)<(int)sizeof reference);
#endif
                CHECK(snprintf(output,sizeof output,"%s/%s-%s-%u.rgba",argv[3],languages[language],formats[format],tick+1)<(int)sizeof output);
                compare_frame(screen,reference,output);
            }
        }
#ifdef TM_TEST_STUDIO
        CHECK(tm_studio_session_close(session)==TM_STUDIO_OK);
#else
        tm_vm_close(vm);
#endif
        free(audio);printf("%s/%s: 60 ticks, exact 30/60 RGBA frames and complete PCM passed\n",languages[language],formats[format]);
    }
    CHECK(spawns==42);
    puts("Production runtime matrix: fourteen languages, three formats, forty-two real workers, eighty-four exact frames and 60-tick PCM passed");
    return 0;
}
