#include "Engine.hpp"
#include "Relocations.h"
#include <string.h>

namespace SwkbdEngine {
static uint32_t U32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint32_t BE32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static void W32(uint8_t *p, uint32_t v) { memcpy(p, &v, sizeof(v)); }
static uint32_t VA(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
static bool Range(uint32_t off, uint32_t size, uint32_t capacity) {
    return off <= capacity && size <= capacity - off;
}

bool Decompress(const uint8_t *src, uint32_t size, uint8_t *dst, uint32_t capacity) {
    if (size < 8 || size > capacity) return false;
    const uint32_t footer = U32(src + size - 8), extra = U32(src + size - 4);
    const uint32_t packed = footer & 0xFFFFFF, skip = footer >> 24;
    if (skip < 8 || skip > packed || packed > size || extra > capacity - size) return false;
    const uint32_t end = size + extra, bottom = size - packed;
    if (end != CodeSize) return false; // this loader supports only the identified build
    memcpy(dst, src, size);
    uint32_t in = size - skip, out = end;
    while (in > bottom) {
        const uint8_t flags = src[--in];
        for (uint32_t mask = 0x80; mask && in > bottom; mask >>= 1) {
            if (flags & mask) {
                if (in - bottom < 2) return false;
                in -= 2;
                const uint32_t token = src[in] | (uint32_t(src[in + 1]) << 8);
                const uint32_t count = (token >> 12) + 3, distance = (token & 0xFFF) + 2;
                if (count > out - bottom || out >= end || distance >= end - out) return false;
                for (uint32_t i = 0; i < count; ++i) { dst[out - 1] = dst[out + distance]; --out; }
            } else {
                if (out <= bottom) return false;
                dst[--out] = src[--in];
            }
        }
    }
    return in == bottom && out == bottom;
}

struct File { uint32_t offset, size, target, handle, kind; };
// Names/path resolution follows libctru romfs_header/romfs_dir/romfs_file.
class RomFs {
    const uint8_t *b;
    uint32_t n, dt, ds, ft, fs, data;
    bool Name(const uint8_t *p, uint32_t bytes, const char *name) const {
        const size_t length = strlen(name);
        if (length * 2 != bytes) return false;
        for (uint32_t i = 0; i < length; ++i) if (p[i*2] != name[i] || p[i*2+1]) return false;
        return true;
    }
public:
    RomFs(const uint8_t *ptr, uint32_t len) : b(ptr), n(len), dt(0), ds(0), ft(0), fs(0), data(0) {}
    bool Init() {
        if (n < 40 || U32(b) != 40) return false;
        dt=U32(b+12); ds=U32(b+16); ft=U32(b+28); fs=U32(b+32); data=U32(b+36);
        return dt >= 40 && Range(dt,ds,n) && Range(ft,fs,n) && dt+ds <= ft && ft+fs <= data && data <= n;
    }
    bool Directory(uint32_t parent, const char *name, uint32_t &found) const {
        if (!Range(parent,24,ds)) return false;
        uint32_t child=U32(b+dt+parent+8), steps=0;
        while (child != 0xFFFFFFFFu) {
            if (++steps > ds/24 || !Range(child,24,ds)) return false;
            const uint8_t *p=b+dt+child; const uint32_t len=U32(p+20);
            if (U32(p) != parent || (len & 1) || !Range(child+24,len,ds)) return false;
            if (Name(p+24,len,name)) { found=child; return true; }
            child=U32(p+4);
        }
        return false;
    }
    bool Lookup(uint32_t parent, const char *name, File &f) const {
        if (!Range(parent,24,ds)) return false;
        uint32_t child=U32(b+dt+parent+12), steps=0;
        while (child != 0xFFFFFFFFu) {
            if (++steps > fs/32 || !Range(child,32,fs)) return false;
            const uint8_t *p=b+ft+child; const uint32_t len=U32(p+28);
            if (U32(p) != parent || (len & 1) || !Range(child+32,len,fs)) return false;
            if (Name(p+32,len,name)) {
                if (U32(p+12) || U32(p+20)) return false;
                const uint32_t off=U32(p+8), size=U32(p+16);
                if (!Range(off,size,n-data)) return false;
                f={data+off,size,0,0,0}; return true;
            }
            child=U32(p+4);
        }
        return false;
    }
};

static bool PackDictionaries(uint8_t *arena, File (&files)[6], uint32_t &used) {
    RomFs fs(arena,SwkbdProbe::DictionarySize);
    uint32_t ja=0, sub=0;
    const char *names[]={"njcon.a","njexyomi.a","njfzk.a","njtan.a","njubase1.a","njubase2.a"};
    if (!fs.Init() || !fs.Directory(0,"JA_small",ja) || !fs.Directory(ja,"32",sub)) return false;
    for (uint32_t i=0;i<6;++i) if (!fs.Lookup(i ? sub : ja,names[i],files[i])) return false;
    // All source metadata and headers are checked BEFORE modifying RomFS bytes.
    uint32_t order[6]={0,1,2,3,4,5};
    for (uint32_t i=0;i<6;++i) for (uint32_t j=i+1;j<6;++j)
        if (files[order[j]].offset < files[order[i]].offset) {
            const uint32_t t=order[i]; order[i]=order[j]; order[j]=t;
        }
    used=0;
    for (uint32_t k=0;k<6;++k) {
        File &f=files[order[k]];
        if (f.size < 0x68 || memcmp(arena+f.offset,"NJDC",4) || memcmp(arena+f.offset+f.size-4,"NJDC",4)) return false;
        if (k && files[order[k-1]].offset + files[order[k-1]].size > f.offset) return false;
        f.target=(used+3u)&~3u;
        if (f.target+4 > f.offset || !Range(f.target,4+f.size,ImageOffset)) return false;
        used=f.target+4+f.size;
        f.kind=BE32(arena+f.offset+8)==0x50000 ? 3 : 0;
        const uint32_t offsets[]={0x30,0x34,0x44,0x64,0x3C,0x2C,0x28};
        if (f.kind==3) for (uint32_t o : offsets) if (BE32(arena+f.offset+o) >= f.size) return false;
    }
    for (uint32_t k=0;k<6;++k) {
        File &f=files[order[k]];
        memmove(arena+f.target+4,arena+f.offset,f.size);
        memset(arena+f.target,0,4);
    }
    used=(used+3u)&~3u;
    if (!Range(used,6*64+3*6236,ImageOffset)) return false;
    for (File &f : files) {
        uint8_t *base=arena+f.target+4;
        if (f.kind==0) { f.handle=VA(base); continue; }
        uint8_t *obj=arena+used; used+=64; memset(obj,0,64); f.handle=VA(obj);
        W32(obj,BE32(base+8)); W32(obj+4,f.size); W32(obj+8,7); W32(obj+16,VA(base));
        const uint32_t dst[]={20,24,32,40,44,48,56}, src[]={0x30,0x34,0x44,0x64,0x3C,0x2C,0x28};
        for (uint32_t i=0;i<7;++i) W32(obj+dst[i],VA(base)+BE32(base+src[i]));
        W32(obj+28,BE32(base+0x38) ? VA(base)-4 : 0);
        W32(obj+36,VA(base)-4); W32(obj+52,VA(base)-4);
    }
    return true;
}

static void Bind(uint8_t *arena, File (&files)[6], uint32_t &used) {
    uint8_t *ctx=arena+ContextOffset, *ds=ctx+236;
    const uint32_t index[]={4,5,2,3,1};
    const uint16_t freq[8][6]={
        {400,550,100,560,400,500},{400,550,100,560,400,500},
        {400,500,10,0,400,500},{0,10,10,0,0,10},{10,0,100,244,10,0},
        {0,0,10,0,10,0},{10,0,950,1000,10,0},{0,0,10,0,10,0}
    };
    for (uint32_t i=0;i<8;++i) {
        uint8_t *entry=ds+(i+2)*36;
        memset(entry,0,36); memcpy(entry+20,freq[i],12);
        if (i<5) {
            File &f=files[index[i]]; entry[0]=f.kind; W32(entry+4,f.handle);
            if (i==0 || i==1 || i==4) { memset(arena+used,0,6236); W32(entry+32,VA(arena+used)); used+=6236; }
        } else {
            const uint32_t callbacks[]={0x19BECC,0x19C334,0x16B890};
            entry[0]=1; W32(entry+4,VA(arena+ImageOffset)+callbacks[i-5]-0x100000);
        }
    }
    for (uint32_t i=0;i<3;++i) W32(ds+720+i*4,files[0].handle);
    ctx[2476]=1;
}

void Reset(Report &r) {
    memset(&r,0,sizeof(r)); r.magic=0x53575037; r.version=1;
    memset(&r.dictionary,0xFF,sizeof(r.dictionary)); memset(&r.code,0xFF,sizeof(r.code));
}
void Run(SwkbdProbe::Api &api, bool execute, const uint16_t *input, Report &r) {
    r.state=1; r.stage=1;
    bool valid=false;
    for (uint32_t i=0;i<TextUnits;++i) {
        r.input[i]=input[i];
        if (!input[i]) { valid=i>0; break; }
    }
    if (!valid) { r.failure=6; r.state=3; return; }
    uint8_t *arena=static_cast<uint8_t*>(api.Allocate(SwkbdProbe::ArenaSize));
    r.arena=VA(arena);
    if (!arena) { r.failure=2; r.state=3; return; }
    if (!api.Validate(arena,SwkbdProbe::ArenaSize)) r.failure=3;
    else {
        r.stage=2;
        if (!SwkbdProbe::Acquire(api,false,arena,r.dictionary)) r.failure=4;
        else {
            r.stage=3;
            if (!SwkbdProbe::Acquire(api,true,arena+SourceOffset,r.code)) r.failure=5;
            else SwkbdEngine_Process(arena,execute,&r);
        }
    }
    api.Free(arena); r.freed=1; r.state=r.failure ? 3 : 2;
}
}

extern "C" void SwkbdEngine_Process(uint8_t *arena, uint32_t execute, SwkbdEngine::Report *report) {
    using namespace SwkbdEngine;
    Report &r=*report; r.stage=4;
    uint8_t *image=arena+ImageOffset, *ctx=arena+ContextOffset, *stack=arena+StackOffset;
    memset(image,0,ImageSize);
    if (!Decompress(arena+SourceOffset,SwkbdProbe::CompressedSize,image,ImageSize)) { r.failure=7; return; }
    r.codeCrc=SwkbdProbe::Crc32(image,CodeSize);
    if (r.codeCrc!=0x423DC1C2) { r.failure=8; return; }
    File files[6]={}; uint32_t used=0;
    if (!PackDictionaries(arena,files,used)) { r.failure=9; return; }
    // Validate ALL relocation sites before writing any; only this known image is supported.
    const uint32_t delta=VA(image)-0x100000;
    for (const Relocation &f : Relocations) {
        if (!Range(f.site-0x100000,4,CodeSize) || U32(image+f.site-0x100000)!=f.value ||
            f.value<0x100000 || f.value>=0x100000+ImageSize) { r.failure=11; return; }
    }
    for (const Relocation &f : Relocations) { W32(image+f.site-0x100000,f.value+delta); ++r.relocationCount; }
    r.dictionaryUsed=used; r.prepared=1;
    if (!execute) { r.stage=10; return; }
    memset(ctx,0,ContextSize); memset(stack,0xA7,StackSize);
    SwkbdEngine_SyncCode();
    uint32_t args[6]={VA(ctx),0,0,0,0,0};
    void *stackTop=stack+StackSize;
    r.stage=5; SwkbdEngine_Call(0x190408+delta,stackTop,args);
    r.stage=6; Bind(arena,files,used); r.dictionaryUsed=used;
    r.stage=7; args[1]=1;
    r.initResult=SwkbdEngine_Call(0x18C628+delta,stackTop,args);
    if (r.initResult<0) { r.failure=12; return; }
    r.stage=8; args[1]=0; args[2]=VA(r.input); args[3]=0; args[4]=0; args[5]=55;
    r.conversionResult=SwkbdEngine_Call(0x111FD4+delta,stackTop,args);
    if (r.conversionResult<0) { r.failure=13; return; }
    r.stage=9;
    for (uint32_t i=0;i<MaxCandidates;++i) {
        args[1]=VA(r.candidates[i]); args[2]=0; args[3]=i; args[4]=112; args[5]=0;
        const int32_t n=SwkbdEngine_Call(0x1181D0+delta,stackTop,args);
        if (n<0 || n>55) { r.failure=13; return; }
        if (n==0) { r.exhausted=1; break; }
        r.lengths[i]=static_cast<uint16_t>(n); r.candidates[i][n]=0; ++r.count;
    }
    r.stackGuard=1;
    for (uint32_t i=0;i<0x1000;++i) if (stack[i]!=0xA7) r.stackGuard=0;
    if (!r.stackGuard) { r.failure=14; return; }
    r.executed=1; r.stage=10;
}
