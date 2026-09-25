#include "ChatKanji.hpp"
#include "ChatKanjiText.hpp"
#include "ChatKanjiAnchors.h"
#include "GameFontView.hpp"
#include "Engine.hpp"
#include "NativeFsApi.hpp"
#include <CTRPluginFramework/System/Process.hpp>
#include <cstdio>
#include <cstring>

namespace CTRPluginFramework { namespace ChatKanji { namespace {
constexpr uint32_t kMenuManager=0x949D4C, kChatSlot=0x1EC, kChatVtable=0x8E5120;
constexpr uint32_t kInput=0x958108, kKeyboard=0x94A654, kFontManager=0x94C9C8;
constexpr unsigned kChatLimit=32, kRowBytes=SwkbdEngine::TextUnits*3+12;
Thread worker=nullptr;
uint32_t done=0;
bool discard=false, owned=false;
uint16_t input[SwkbdEngine::TextUnits]={};
SwkbdEngine::Report report={};
char rows[SwkbdEngine::MaxCandidates][kRowBytes]={};
const char *rowPointers[SwkbdEngine::MaxCandidates]={};
int rowCount=0;
char title[64]={}, error[96]={};
alignas(4) uint32_t fontObject[6]={}; // private MapCharToGlyph cache; game tables/texture descriptors are borrowed
GameFontView fontView;
uint32_t fontManager=0, fontResource=0;
bool fontReady=false;
// Resident engine (2026-09-26): prepared once (dictionary/code read, relocation, init), then only converted.
// Written by the worker, read by GuiMenu's thread only while no worker exists (after join).
uint8_t *resident=nullptr;
bool loadOnly=false;     // current worker only prepares (Preload)

bool Readable(uint32_t address,size_t bytes) {
    const uint64_t end=uint64_t(address)+bytes;
    if(address<0x100000 || !bytes || end>0x40000000)return false;
    uint64_t cursor=address;
    while(cursor<end) {
        MemInfo info={}; PageInfo page={};
        if(R_FAILED(svcQueryMemory(&info,&page,static_cast<u32>(cursor))))return false;
        const uint64_t next=uint64_t(info.base_addr)+info.size;
        if(info.base_addr>cursor || next<=cursor || !(info.perm & MEMPERM_READ))return false;
        cursor=next;
    }
    return true;
}
bool Read(uint32_t address,void *out,size_t bytes) {
    if(!Readable(address,bytes))return false;
    const volatile uint8_t *p=reinterpret_cast<const volatile uint8_t*>(address);
    uint8_t *q=static_cast<uint8_t*>(out);
    for(size_t i=0;i<bytes;++i)q[i]=p[i];
    return true;
}
bool Word(uint32_t a,uint32_t &v) { return !(a&3) && Read(a,&v,sizeof(v)); }
bool BuildMatches() {
    if(Process::GetTitleID()!=0x0004000000086200ULL)return false;
    for(const ChatKanjiAnchor &a:kChatKanjiAnchors) {
        uint32_t words[4];
        if(!Read(a.address,words,sizeof(words)) || std::memcmp(words,a.words,sizeof(words)))return false;
    }
    return true;
}
bool PrepareFont() {
    uint32_t manager=0,obj[6]={},size=0; uint8_t phase=0;
    if(!Word(kFontManager,manager) || !manager ||
       !Read(manager+0x1A1,&phase,1) || phase!=3 ||
       !Read(manager+0x188,obj,sizeof(obj)) || obj[0]!=0x8FCAAC || !obj[3] ||
       !Word(obj[1]+12,size) || size<32 || size>0x800000 || !Readable(obj[1],size))return false;
    // This permanently loaded Garden_msg_size16 font has native linefeed == height.
    uint8_t info[24];
    if(!Read(obj[2],info,sizeof(info)) || !info[20] || !info[21] || info[1]!=info[20])return false;
    if(!fontView.Init(reinterpret_cast<void*>(obj[1]),obj[1],size,obj[2]))return false;
    std::memcpy(fontObject,obj,sizeof(obj));fontObject[5]=0xFFFFFFFFu;
    fontManager=manager;fontResource=obj[1];fontReady=true;
    return true;
}
void Work(void*);
RequestResult Snapshot() {
    uint32_t manager=0,chat=0,vt=0,ptr=0,cap=0,active=0,child=0,global=0;
    if(!Word(kMenuManager,manager) || !manager || !Word(manager+kChatSlot,chat) || !chat ||
       !Word(chat,vt) || vt!=kChatVtable || !Word(chat+0x50,ptr) || ptr!=chat+0x58 ||
       !Word(chat+0x54,cap) || cap!=65 || !Word(chat+0xDC,child) || !child ||
       !Word(kKeyboard,active) || !active || !Word(kInput,global) || global!=ptr)return REQUEST_NO_CHAT;
    uint16_t first[kChatLimit+1]={},second[kChatLimit+1]={};
    if(!Read(ptr,first,sizeof(first)))return REQUEST_NO_CHAT;
    size_t length=0;
    if(!ChatKanjiText::Valid(first,kChatLimit+1,kChatLimit,length))return REQUEST_BAD_INPUT;
    uint32_t checkManager=0,checkChat=0,checkPtr=0,checkGlobal=0,checkKeyboard=0,checkVt=0;
    if(!Read(ptr,second,sizeof(second)) || std::memcmp(first,second,sizeof(first)) ||
       !Word(kMenuManager,checkManager) || checkManager!=manager ||
       !Word(manager+kChatSlot,checkChat) || checkChat!=chat || !Word(chat,checkVt) || checkVt!=vt ||
       !Word(chat+0x50,checkPtr) || checkPtr!=ptr || !Word(kInput,checkGlobal) || checkGlobal!=ptr ||
       !Word(kKeyboard,checkKeyboard) || checkKeyboard!=active)return REQUEST_NO_CHAT;
    if(!length)return REQUEST_EMPTY;
    std::memset(input,0,sizeof(input));std::memcpy(input,first,(length+1)*sizeof(uint16_t));
    return REQUEST_OK;
}
// Normal chat open and bound to the shared input buffer. Same checks as Snapshot, without reading the text.
bool ChatBound() {
    uint32_t manager=0,chat=0,vt=0,ptr=0,cap=0,active=0,child=0,global=0;
    return Word(kMenuManager,manager) && manager && Word(manager+kChatSlot,chat) && chat &&
           Word(chat,vt) && vt==kChatVtable && Word(chat+0x50,ptr) && ptr==chat+0x58 &&
           Word(chat+0x54,cap) && cap==65 && Word(chat+0xDC,child) && child &&
           Word(kKeyboard,active) && active && Word(kInput,global) && global==ptr;
}
RequestResult Start() {
    if(!PrepareFont())return REQUEST_NO_FONT;
    __atomic_store_n(&done,0u,__ATOMIC_RELEASE);
    // Engine uses its own verified 64 KiB stack. This stack is for FS/loader/libctru.
    worker=threadCreate(Work,nullptr,0x8000,0x31,-2,false);
    return worker ? REQUEST_OK : REQUEST_NO_THREAD;
}
void Work(void*) {
    NativeFsApi api;
    if(!resident) {SwkbdEngine::Reset(report);resident=SwkbdEngine::Load(api,report);}
    if(resident && !loadOnly)SwkbdEngine::Convert(resident,input,report);
    __atomic_store_n(&done,1u,__ATOMIC_RELEASE);
}
} // namespace

bool Busy() { return worker!=nullptr; }
RequestResult Request() {
    if(worker || owned)return REQUEST_BUSY;
    error[0]=0;rowCount=0;discard=false;
    if(!BuildMatches())return REQUEST_UNSUPPORTED;
    const RequestResult snapshot=Snapshot();if(snapshot!=REQUEST_OK)return snapshot;
    return Start();
}
RequestResult RequestText(const uint16_t *text,size_t length) {
    if(worker || owned)return REQUEST_BUSY;
    error[0]=0;rowCount=0;discard=false;
    if(!BuildMatches())return REQUEST_UNSUPPORTED;
    if(!ChatBound())return REQUEST_NO_CHAT;
    if(!text || !length)return REQUEST_EMPTY;
    if(length>kChatLimit)return REQUEST_BAD_INPUT;
    uint16_t copy[kChatLimit+1]={};
    std::memcpy(copy,text,length*sizeof(uint16_t));
    size_t checked=0;
    if(!ChatKanjiText::Valid(copy,kChatLimit+1,kChatLimit,checked) || checked!=length)return REQUEST_BAD_INPUT;
    std::memset(input,0,sizeof(input));std::memcpy(input,copy,(length+1)*sizeof(uint16_t));
    return Start();
}
bool Poll() {
    // Result timeouts can be positive informational codes: only exact success permits freeing.
    if(!worker || !__atomic_load_n(&done,__ATOMIC_ACQUIRE) || threadJoin(worker,0)!=0)return false;
    threadFree(worker);worker=nullptr;
    if(loadOnly) {loadOnly=false;return false;}   // Preload: nothing to publish (a failed load retries on the next request)
    if(discard)return false;
    if(report.state!=2 || !report.executed || !resident || !report.stackGuard) {
        std::snprintf(error,sizeof(error),"ERROR %lu / STAGE %lu",static_cast<unsigned long>(report.failure),static_cast<unsigned long>(report.stage));
        return true;
    }
    if(!FontReady()) {std::snprintf(error,sizeof(error),"FONT UNAVAILABLE");return true;}
    for(unsigned i=0;i<report.count && i<SwkbdEngine::MaxCandidates;++i) {
        const unsigned length=report.lengths[i];size_t checked=0;
        if(!length || length>=SwkbdEngine::TextUnits ||
           !ChatKanjiText::Valid(report.candidates[i],SwkbdEngine::TextUnits,SwkbdEngine::TextUnits-1,checked) || checked!=length) {
            rowCount=0;std::snprintf(error,sizeof(error),"INVALID CANDIDATE");return true;
        }
        const int prefix=std::snprintf(rows[i],sizeof(rows[i]),"%u. ",i+1);
        if(prefix<0 || !ChatKanjiText::Utf8(report.candidates[i],length,rows[i]+prefix,sizeof(rows[i])-prefix)) {
            rowCount=0;std::snprintf(error,sizeof(error),"INVALID TEXT");return true;
        }
        rowPointers[i]=rows[i];++rowCount;
    }
    if(!rowCount) {std::snprintf(error,sizeof(error),"NO CANDIDATES");return true;}
    std::snprintf(title,sizeof(title),"KANJI %d%s",rowCount,report.exhausted?"":"+ (LIMIT)");
    owned=true;return true;
}
void Dismiss() {discard=true;owned=false;}
bool Preload() {
    if(worker || resident || owned || !BuildMatches())return false;
    loadOnly=true;discard=false;
    __atomic_store_n(&done,0u,__ATOMIC_RELEASE);
    worker=threadCreate(Work,nullptr,0x8000,0x31,-2,false);
    if(!worker)loadOnly=false;
    return worker!=nullptr;
}
bool EngineResident() {return worker==nullptr && resident!=nullptr;}
bool ReleaseEngine() {
    if(worker) {
        if(!__atomic_load_n(&done,__ATOMIC_ACQUIRE) || threadJoin(worker,0)!=0)return false;   // never interrupt the engine
        threadFree(worker);worker=nullptr;loadOnly=false;
    }
    discard=true;owned=false;
    if(resident) {NativeFsApi api;SwkbdEngine::Unload(api,resident);resident=nullptr;}
    return true;
}
void Shutdown() {
    discard=true;owned=false;
    if(worker) {threadJoin(worker,U64_MAX);threadFree(worker);worker=nullptr;}
    if(resident) {NativeFsApi api;SwkbdEngine::Unload(api,resident);resident=nullptr;}
}
const char *const *Rows() {return rowPointers;}
int CandidateCount() {return owned?rowCount:0;}
const uint16_t *Candidate(int index,int &length) {
    length=0;
    if(!owned || index<0 || index>=rowCount)return nullptr;
    length=report.lengths[index];
    return report.candidates[index];
}
bool NormalChatOpen() {return ChatBound();}
int RowCount() {return rowCount;}
const char *Title() {return title;}
const char *Error() {return error;}
bool FontReady() {
    uint32_t manager=0,resource=0;uint8_t phase=0;
    return fontReady && Word(kFontManager,manager) && manager==fontManager &&
           Read(manager+0x1A1,&phase,1) && phase==3 && Word(manager+0x188+4,resource) && resource==fontResource;
}
uint32_t FontAddress() {return fontReady?reinterpret_cast<uintptr_t>(fontObject):0;}
int Glyph(uint32_t cp) {return fontReady?fontView.Glyph(cp):-1;}
int GlyphAdvance(int glyph) {return fontReady?fontView.Advance(glyph):0;}
int GlyphRawAdvance(int glyph) {return fontReady?fontView.RawAdvance(glyph):0;}
int FontCellWidth() {return fontReady?fontView.CellWidth():0;}
int FontCellHeight() {return fontReady?fontView.CellHeight():0;}
} }
