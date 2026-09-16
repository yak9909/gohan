#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ChatKanjiText {
inline bool Valid(const uint16_t *text, size_t capacity, size_t limit, size_t &length) {
    for(length=0;length<capacity;++length) {
        const uint16_t c=text[length];
        if(!c)return length<=limit;
        if(c<0x20 || c==0x7F || (c>=0xD800 && c<=0xDFFF))return false;
    }
    return false;
}
inline bool Utf8(const uint16_t *text, size_t length, char *out, size_t capacity) {
    if(!capacity)return false;
    size_t n=0;
    for(size_t i=0;i<length;++i) {
        const uint32_t c=text[i];
        if(!c || c<0x20 || c==0x7F || (c>=0xD800 && c<=0xDFFF))return false;
        const size_t bytes=c<0x80?1:c<0x800?2:3;
        if(n+bytes>=capacity)return false;
        if(bytes==1)out[n++]=char(c);
        else if(bytes==2) {out[n++]=char(0xC0|(c>>6));out[n++]=char(0x80|(c&63));}
        else {out[n++]=char(0xE0|(c>>12));out[n++]=char(0x80|((c>>6)&63));out[n++]=char(0x80|(c&63));}
    }
    out[n]=0;return true;
}
}
