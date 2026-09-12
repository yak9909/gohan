#pragma once
#include <stdint.h>
#include <stddef.h>

// Read-only relocated CFNU tables. All pointer arithmetic is bounded by the resource.
// CMAP/CWDH layout verified against ACNL 0x743768/0x7438B4 and a live font snapshot.
class GameFontView {
    const uint8_t *data_ = nullptr;
    uint32_t base_ = 0, size_ = 0, info_ = 0;
    const uint8_t *At(uint32_t address, uint32_t n) const {
        if (!data_ || address < base_ || address-base_ > size_ || n > size_-(address-base_)) return nullptr;
        return data_+(address-base_);
    }
    static uint16_t U16(const uint8_t *p) { return p[0] | (uint16_t(p[1])<<8); }
    static uint32_t U32(const uint8_t *p) { return U16(p) | (uint32_t(U16(p+2))<<16); }
public:
    bool Init(const void *data, uint32_t address, uint32_t size, uint32_t info) {
        data_=static_cast<const uint8_t*>(data); base_=address; size_=size; info_=info;
        const uint8_t *f=At(info_,24);
        return f && size_>=32 && f[20] && f[21] && f[1] && U32(data_)==0x554E4643 &&
               U32(data_+12)==size_ && At(U32(f+8),24);
    }
    int Glyph(uint32_t cp, bool fallback=true) const {
        const uint8_t *f=At(info_,24); if (!f || cp>0xFFFF) return -1;
        uint32_t address=U32(f+16);
        for (unsigned steps=0;address && steps<128;++steps) {
            const uint8_t *p=At(address,12); if (!p) return -1;
            const uint32_t begin=U16(p), end=U16(p+2), method=U16(p+4);
            if (begin>end) return -1;
            if (cp>=begin && cp<=end) {
                uint32_t glyph=0xFFFF;
                if (method==0) { p=At(address,14); if (!p) return -1; glyph=(U16(p+12)+cp-begin)&0xFFFF; }
                else if (method==1) { p=At(address,12+2*(end-begin+1)); if (!p) return -1; glyph=U16(p+12+2*(cp-begin)); }
                else if (method==2) {
                    p=At(address,14); if (!p) return -1;
                    const uint32_t count=U16(p+12); p=At(address,14+4*count); if (!p) return -1;
                    uint32_t lo=0,hi=count;
                    while(lo<hi) { const uint32_t mid=lo+(hi-lo)/2,code=U16(p+14+4*mid);
                        if(code<cp)lo=mid+1;else hi=mid; }
                    if(lo<count && U16(p+14+4*lo)==cp)glyph=U16(p+16+4*lo);
                } else return -1;
                return glyph!=0xFFFF ? int(glyph) : fallback ? U16(f+2) : -1;
            }
            address=U32(p+8);
        }
        if(address)return -1;
        return fallback ? U16(f+2) : -1;
    }
    int Advance(int glyph) const {
        const uint8_t *f=At(info_,24); if(!f || glyph<0)return 0;
        uint32_t address=U32(f+12); unsigned advance=f[6];
        for(unsigned steps=0;address && steps<128;++steps) {
            const uint8_t *p=At(address,8); if(!p)return 0;
            const unsigned begin=U16(p),end=U16(p+2);
            if(begin>end)return 0;
            if(unsigned(glyph)>=begin && unsigned(glyph)<=end) {
                p=At(address,8+3*(end-begin+1)); if(!p)return 0;
                advance=p[8+3*(unsigned(glyph)-begin)+2]; break;
            }
            address=U32(p+4);
            if(steps==127 && address)return 0;
        }
        return (advance*8+f[21]-1)/f[21]; // conservative pixel width at requested width 8
    }
};
