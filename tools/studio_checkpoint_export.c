/* Read-only recovery utility for a frozen IPC snapshot from the exact same
 * Studio build. Including the session source binds layout to its C ABI. */
#include "../src/studio_session.c"
#include "cart.h"
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 1;
    shared *m=malloc(sizeof *m); if(!m) {fclose(f);return 1;}
    int ok=fread(m,1,sizeof *m,f)==sizeof *m && fgetc(f)==EOF;
    fclose(f);
    if(!ok || m->magic!=MAGIC || m->sequence!=m->acknowledged) {free(m);return 1;}
    u8 *bytes=malloc(3*sizeof(tic_cartridge)); tic_cartridge *roundtrip=calloc(1,sizeof *roundtrip);
    if(!bytes || !roundtrip) {free(bytes);free(roundtrip);free(m);return 1;}
    int size=tic_cart_save(&m->cart,bytes);
    if(size<=0 || (size_t)size>3*sizeof(tic_cartridge)) return 1;
    tic_cart_load(roundtrip,bytes,size);
    if(memcmp(roundtrip,&m->cart,sizeof *roundtrip)) {
        fprintf(stderr,"Checkpoint cartridge did not round-trip exactly\n");return 1;
    }
    f=fopen(argv[2],"wb");if(!f) return 1;
    ok=fwrite(bytes,1,size,f)==(size_t)size && !fclose(f);
    printf("checkpoint: ipc=%zu cart_offset=%zu cart_bytes=%zu encoded=%d sequence=%u mode=%d modified=%d\n",
           sizeof *m,offsetof(shared,cart),sizeof(tic_cartridge),size,m->sequence,m->data.mode,m->data.modified);
    free(roundtrip);free(bytes);free(m);return ok?0:1;
}
