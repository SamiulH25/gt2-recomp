#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// OVL: 0x30 header + gzip stream at 0x30 -> decompresses to 1,133,632 bytes MIPS
// Header: 30 00 00 00 45 35 02 00 78 35 02 00 2d ad 00 00 ... (unknown fields)
// Observed: gzip at offset 0x30 (48), decompressed size 0x114C40

uint8_t* overlay_decompress(const char *path, size_t *out_size) {
    FILE *f=fopen(path,"rb");
    if(!f) { perror(path); return NULL; }
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *raw=malloc(sz);
    fread(raw,1,sz,f); fclose(f);
    long off=0x30; // gzip start
    // brute find 1f 8b if not at 0x30
    if(!(raw[off]==0x1f && raw[off+1]==0x8b)) {
        for(long i=0;i<sz-2;i++) if(raw[i]==0x1f && raw[i+1]==0x8b && raw[i+2]==0x08) { off=i; break; }
    }
    printf("[overlay] gzip at %lx raw %ld\n", off, sz);
    // decompress via zlib - handle concatenated gzip members (GT2 OVL has 6 members)
    size_t cap=2*1024*1024;
    uint8_t *out=malloc(cap);
    size_t total_out=0;
    size_t in_off=off;
    while(in_off < (size_t)sz) {
        if(raw[in_off]!=0x1f || raw[in_off+1]!=0x8b) { in_off++; continue; }
        z_stream strm={0};
        inflateInit2(&strm, 16+15);
        strm.next_in=raw+in_off; strm.avail_in=sz-in_off;
        strm.next_out=out+total_out; strm.avail_out=cap-total_out;
        int ret=inflate(&strm, Z_SYNC_FLUSH);
        if(ret==Z_STREAM_END || ret==Z_OK) {
            size_t produced=strm.total_out;
            total_out+=produced;
            size_t consumed=strm.total_in;
            inflateEnd(&strm);
            if(consumed==0) break;
            in_off+=consumed;
            // if we consumed all and there is more gzip data, continue
            if(in_off < (size_t)sz && total_out < cap) continue;
            else break;
        } else {
            fprintf(stderr,"inflate %d %s at %zx\n",ret,strm.msg?strm.msg:"",in_off);
            inflateEnd(&strm); break;
        }
    }
    *out_size=total_out;
    free(raw);
    printf("[overlay] decompressed %zu (0x%zx) first %02x %02x %02x %02x (MIPS addiu sp)\n", *out_size, *out_size, out[0],out[1],out[2],out[3]);
    return out;
}

void overlay_load(const char *path, uint32_t va) {
    size_t sz; uint8_t *data=overlay_decompress(path, &sz);
    if(!data) return;
    printf("[overlay] would map %zu bytes at VA %08x (stub - copy to emulated RAM in full runtime)\n", sz, va);
    free(data);
}
