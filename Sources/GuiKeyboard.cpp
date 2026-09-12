// Simulator ui-model.js / app.js keyboard port. No game-memory access.
#include "GuiKeyboard.hpp"
#include "GuiKeyboardLayout.h"
#include "GuiV2.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace CTRPluginFramework { namespace GuiKeyboard {
namespace {
    State s = {};
    Result result = {};
    bool pending = false, touched = false;
    bool padFocus = false;
    const char *touchFocus = nullptr;
    char saved[33] = "PLAYER", text[33];
    const GuiV2::Screen bot = GuiV2::SCREEN_BOTTOM;
    bool Eq(const char *a, const char *b) { return std::strcmp(a,b) == 0; }
    uint32_t Decode(const char *&p) {
        uint32_t c = (unsigned char)*p++;
        if (c < 0x80) return c;
        const int n = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        c &= (1u << (6-n))-1;
        for (int i=0; i<n && *p; ++i) c = (c<<6) | ((unsigned char)*p++ & 63);
        return c;
    }
    void Encode(uint32_t c, char *&p) {
        if (c < 0x80) *p++ = (char)c;
        else if (c < 0x800) { *p++=(char)(0xC0|(c>>6)); *p++=(char)(0x80|(c&63)); }
        else if (c < 0x10000) { *p++=(char)(0xE0|(c>>12)); *p++=(char)(0x80|((c>>6)&63)); *p++=(char)(0x80|(c&63)); }
        else { *p++=(char)(0xF0|(c>>18)); *p++=(char)(0x80|((c>>12)&63)); *p++=(char)(0x80|((c>>6)&63)); *p++=(char)(0x80|(c&63)); }
    }
    void Utf8(char *out, int count) {
        char *p=out;
        for(int i=0;i<count;++i) Encode(s.chars[i],p);
        *p=0;
    }
    uint32_t Katakana(uint32_t c) { return c>=0x3041 && c<=0x3096 ? c+0x60 : c; }
    const char *const transforms[] = {
        u8"かがきぎくぐけげこごさざしじすずせぜそぞただちぢつづてでとどはばひびふぶへべほぼカガキギクグケゲコゴサザシジスズセゼソゾタダチヂツヅテデトドハバヒビフブヘベホボウヴ",
        u8"はぱひぴふぷへぺほぽハパヒピフプヘペホポ",
        u8"あぁいぃうぅえぇおぉつっやゃゆゅよょわゎアァイィウゥエェオォツッヤャユュヨョワヮ"
    };
    int TransformIndex(const char *key) {
        return Eq(key,"DAKUTEN") ? 0 : Eq(key,"HANDAKUTEN") ? 1 : Eq(key,"SMALL") ? 2 : -1;
    }
    uint32_t Variant(int which) {
        if (!s.cursor) return 0;
        uint32_t current=s.chars[s.cursor-1], base=current;
        for (int i=0;i<3;++i) {
            const char *p=transforms[i];
            while (*p) { uint32_t a=Decode(p), b=Decode(p); if(current==b) base=a; }
        }
        const char *p=transforms[which];
        while (*p) { uint32_t a=Decode(p), b=Decode(p); if(a==base) return current==b ? base : b; }
        return 0;
    }
    void Insert(uint32_t c) {
        if (s.length>=8) return;
        for(int i=s.length;i>s.cursor;--i) s.chars[i]=s.chars[i-1];
        s.chars[s.cursor++]=c;
        ++s.length;
    }
    const KeyRect *Layout(int &count) {
        count=s.abc ? (int)(sizeof(kFullAbc)/sizeof(KeyRect)) : (int)(sizeof(kFullKana)/sizeof(KeyRect));
        return s.compact ? (s.abc?kCompactAbc:kCompactKana) : (s.abc?kFullAbc:kFullKana);
    }
    const char *NumberKey(int row, int col) {
        static const char *const keys[6][4] = {
            {"7","8","9","A"},{"4","5","6","B"},{"1","2","3","C"},
            {"0","D","E","F"},{"SIGN",".","HEX","BS"},{"CANCEL","OK",nullptr,nullptr}
        };
        return row==4 && col==2 && s.hex ? "DEC" : keys[row][col];
    }
    KeyRect NumberRect(int row, int col) {
        const int n=row==5?2:4, w=(296-3*(n-1))/n;
        return {NumberKey(row,col),row,col,12+(296-(w*n+3*(n-1)))/2+col*(w+3),74+row*24,w,21};
    }
    void Move(int dx,int dy) {
        const int rows=s.kind==NUMBER ? 6 : s.abc?5:6;
        s.row=(s.row+dy+rows)%rows;
        int columns=0;
        if(s.kind==NUMBER) columns=s.row==5?2:4;
        else { int n; const KeyRect *layout=Layout(n); for(int i=0;i<n;++i) if(layout[i].row==s.row) ++columns; }
        if(dx) s.column=(s.column+dx+columns)%columns;
        s.column=std::min(s.column,columns-1);
    }
    float Amount(uint32_t now) {
        if(s.kind!=TEXT) return 1;
        float p=s.duration<=0?1:std::min(1.0f,(now-s.started)/s.duration);
        p=1-(1-p)*(1-p)*(1-p);
        return s.from+((s.closing?0.0f:1.0f)-s.from)*p;
    }
    uint32_t Fade(uint32_t c,float a) { return (c&0xFFFFFF)|((uint32_t)(((c>>24)*a)+0.5f)<<24); }
    void Rect(int x,int y,int w,int h,uint32_t c,float a) { GuiV2::FillRect(bot,x,y,w,h,Fade(c,a)); }
    void Label(int x,int y,const char *t,uint32_t c,float a,bool numeric=false) {
        GuiV2::DrawText(bot,x,y,t,Fade(c,a),1,numeric?GuiV2::FONT_NUM:GuiV2::FONT_MAIN);
    }
    // Disjoint border and interior preserve translucency during fades (F-323).
    void Frame(int x,int y,int w,int h,uint32_t line,uint32_t fill,float a) {
        Rect(x,y,w,1,line,a); Rect(x,y+h-1,w,1,line,a);
        Rect(x,y+1,1,h-2,line,a); Rect(x+w-1,y+1,1,h-2,line,a);
        Rect(x+1,y+1,w-2,h-2,fill,a);
    }
    const char *Display(const char *key,char *buf) {
        static const char *const labels[][2] = {
            {"DAKUTEN",u8"濁点"},{"HANDAKUTEN",u8"半濁点"},{"SMALL",u8"小字"},
            {"HIRAGANA",u8"かな"},{"KATAKANA",u8"カナ"},{"BS",u8"消去"},{"ENTER",u8"改行"},
            {"LONG",u8"ー"},{"SPACE",u8"空白"},{"CONFIRM",u8"けってい"},{"BLANK",""},
            {"CAPS","Caps"},{"SHIFT","Shift"},{"AIU",u8"あいう"},{"SYMBOL",u8"記号"},
            {"PHONE",u8"ケータイ"},{"CANCEL",u8"とじる"}
        };
        if(s.kind==NUMBER) {
            if(Eq(key,"BS")) return u8"消";
            if(Eq(key,"SIGN")) return u8"符号";
            if(Eq(key,"CANCEL")) return u8"取消";
            if(Eq(key,"OK")) return u8"決定";
            return key;
        }
        for(unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);++i) if(Eq(key,labels[i][0])) return labels[i][1];
        const char *p=key; uint32_t c=Decode(p);
        if(!*p) {
            if(c>='a' && c<='z' && (s.caps||s.shift)) c-= 'a'-'A';
            if(!s.abc && s.katakana) c=Katakana(c);
            char *out=buf; Encode(c,out); *out=0; return buf;
        }
        return key;
    }
    void DrawKey(const KeyRect &r,float a) {
        const bool disabled=!Enabled(r.key);
        const bool selected=(s.kind==NUMBER || padFocus)
            ? r.row==s.row && r.column==s.column : touchFocus && Eq(r.key,touchFocus);
        const bool active=selected && !disabled;
        const bool latched=s.kind==TEXT && ((!s.abc && ((Eq(r.key,"KATAKANA")&&s.katakana)||
            ((Eq(r.key,"HIRAGANA")||Eq(r.key,"AIU"))&&!s.katakana))) ||
            (s.abc && (Eq(r.key,"ABC")||(Eq(r.key,"CAPS")&&s.caps)||(Eq(r.key,"SHIFT")&&s.shift))));
        const uint32_t fill=disabled?0xAD121512:active?0xD1445831:latched?0xCC364B23:0xC21E231C;
        const uint32_t line=disabled?(selected?0xFF5B6159:0xFF292D27):(active||latched)?0xFFA4E463:0xFF3D453A;
        const uint32_t col=disabled?0xFF525851:active?0xFFFFFFFF:latched?0xFFB7F08F:0xFFB9C1B7;
        Frame(r.x,r.y,r.w,r.h,line,fill,a);
        char buf[8]; const char *label=Display(r.key,buf);
        const bool numeric=s.kind==NUMBER && r.key[0] && !r.key[1] &&
            ((r.key[0]>='0' && r.key[0]<='9')||(r.key[0]>='A'&&r.key[0]<='F')||r.key[0]=='.');
        int width=GuiV2::MeasureText(label,1,numeric?GuiV2::FONT_NUM:GuiV2::FONT_MAIN);
        Label(r.x+(int)std::floor((r.w-width)/2.0f+0.5f),r.y+(r.h-8+1)/2,label,col,a,numeric);
    }
    double Parse() {
        char *end=nullptr;
        double v=s.hex ? (double)std::strtoll(s.buffer,&end,16) : std::strtod(s.buffer,&end);
        if(end==s.buffer || *end || !std::isfinite(v)) return s.minimum/(s.format==FLOAT_TENTHS?10.0:1.0);
        return v;
    }
    void NumberFormat(char *out,size_t cap,int32_t value) {
        if(s.format==HEXADECIMAL) {
            const int64_t v=value;
            std::snprintf(out,cap,v<0?"0x-%llX":"0x%llX",(unsigned long long)(v<0?-v:v));
        } else if(s.format==FLOAT_TENTHS) std::snprintf(out,cap,"%.1f",value/10.0);
        else std::snprintf(out,cap,"%ld",(long)value);
    }
}
void Reset() {
    s=State{}; result=Result{}; pending=false; touched=false; padFocus=false; touchFocus=nullptr;
    std::strcpy(saved,"PLAYER");
}
bool Active() { return s.kind!=NONE; }
const State &Current() { return s; }
const char *TextValue() { Utf8(text,s.length); return text; }
void SetCursor(int cursor) { if(s.kind==TEXT) s.cursor=std::max(0,std::min(cursor,s.length)); }
void OpenText(bool compact,uint32_t now) {
    s=State{}; pending=false; touched=true; padFocus=false; touchFocus=nullptr; s.kind=TEXT; s.compact=compact;
    s.started=now; s.duration=180;
    const char *p=saved;
    while(*p && s.length<8) s.chars[s.length++]=Decode(p);
    s.cursor=s.length;
}
void OpenNumber(int item,int32_t value,int32_t minimum,int32_t maximum,Format format,bool apply,uint32_t now) {
    s=State{}; pending=false; touched=true; s.kind=NUMBER; s.item=item;
    s.minimum=minimum; s.maximum=maximum; s.format=format; s.apply=apply;
    s.started=now; s.hex=format==HEXADECIMAL;
    if(s.hex) {
        const int64_t v=value;
        std::snprintf(s.buffer,sizeof(s.buffer),v<0?"-%llX":"%llX",(unsigned long long)(v<0?-v:v));
    } else if(format==FLOAT_TENTHS) {
        std::snprintf(s.buffer,sizeof(s.buffer),"%.1f",value/10.0);
        size_t n=std::strlen(s.buffer);
        if(n>=2 && s.buffer[n-2]=='.' && s.buffer[n-1]=='0') s.buffer[n-2]=0;
    }
    else std::snprintf(s.buffer,sizeof(s.buffer),"%ld",(long)value);
}
void Cancel(uint32_t now) {
    if(s.kind==TEXT && !s.closing) { s.from=Amount(now); s.duration=180*s.from; s.started=now; s.closing=true; }
    else if(s.kind==NUMBER) s.kind=NONE;
}
void Update(uint32_t now) { if(s.closing && now-s.started>=s.duration) s.kind=NONE; }
bool TakeResult(Result &out) { if(!pending) return false; out=result; pending=false; return true; }
bool Enabled(const char *key) {
    if(!Active()) return false;
    if(s.kind==NUMBER) {
        if(key[0]>='A'&&key[0]<='F'&&!key[1]) return s.hex;
        if(Eq(key,".")) return !s.hex && s.format==FLOAT_TENTHS;
        if(Eq(key,"SIGN")) return !s.hex && s.minimum<0;
        if(Eq(key,"HEX")) return !s.hex && s.format!=FLOAT_TENTHS;
        if(Eq(key,"DEC")) return s.hex;
        return *key!=0;
    }
    const int transform=TransformIndex(key);
    if(transform>=0) return Variant(transform)!=0;
    if(Eq(key,"HIRAGANA")||Eq(key,"KATAKANA")) return !s.abc;
    return !Eq(key,"ENTER")&&!Eq(key,"BLANK")&&!Eq(key,"SYMBOL")&&!Eq(key,"PHONE");
}
bool Activate(const char *key,uint32_t now) {
    if(!Active()||s.closing||!Enabled(key)) return false;
    if(s.kind==NUMBER) {
        size_t n=std::strlen(s.buffer);
        if(!key[1] && ((key[0]>='0'&&key[0]<='9')||(key[0]>='A'&&key[0]<='F'))) {
            if(Eq(s.buffer,"0")) n=0;
            if(n<20) { s.buffer[n]=key[0]; s.buffer[n+1]=0; }
        } else if(Eq(key,".") && !std::strchr(s.buffer,'.') && n<20) { s.buffer[n]='.'; s.buffer[n+1]=0; }
        else if(Eq(key,"BS")) { if(n>1) s.buffer[n-1]=0; else std::strcpy(s.buffer,"0"); }
        else if(Eq(key,"SIGN")) {
            if(s.buffer[0]=='-') std::memmove(s.buffer,s.buffer+1,n);
            else if(n<20) { std::memmove(s.buffer+1,s.buffer,n+1); s.buffer[0]='-'; }
        } else if(Eq(key,"HEX")||Eq(key,"DEC")) {
            // The UI's bounded buffer can exceed int64. Clamp before conversion;
            // never cast an out-of-range floating value to an integer.
            double v=Parse();
            v=std::max(-2147483648.0,std::min(v,2147483647.0));
            const int64_t iv=(int64_t)v;
            s.hex=Eq(key,"HEX"); s.row=s.column=0;
            if(s.hex) std::snprintf(s.buffer,sizeof(s.buffer),iv<0?"-%llX":"%llX",(unsigned long long)(iv<0?-iv:iv));
            else std::snprintf(s.buffer,sizeof(s.buffer),"%lld",(long long)iv);
        } else if(Eq(key,"CANCEL")) Cancel(now);
        else if(Eq(key,"OK")) {
            const double scale=s.format==FLOAT_TENTHS?10.0:1.0;
            double v=std::max((double)s.minimum,std::min(Parse()*scale,(double)s.maximum));
            result=Result{}; result.kind=NUMBER; result.item=s.item;
            result.value=(int32_t)std::floor(v+0.5); result.apply=s.apply;
            pending=true; s.kind=NONE;
        }
        return true;
    }
    const int transform=TransformIndex(key);
    if(Eq(key,"ABC")) { s.abc=true; s.row=s.column=0; }
    else if(Eq(key,"AIU")||Eq(key,"HIRAGANA")||Eq(key,"KATAKANA")) {
        s.abc=false; s.katakana=Eq(key,"KATAKANA"); s.row=s.column=0;
    } else if(transform>=0) s.chars[s.cursor-1]=Variant(transform);
    else if(Eq(key,"CAPS")) { s.caps=!s.caps; if(s.caps) s.shift=false; }
    else if(Eq(key,"SHIFT")) { s.shift=!s.shift; if(s.shift) s.caps=false; }
    else if(Eq(key,"SPACE")) Insert(' ');
    else if(Eq(key,"LONG")) Insert(0x30FC);
    else if(Eq(key,"BS")) {
        if(s.cursor>0) { --s.cursor; --s.length; for(int i=s.cursor;i<s.length;++i) s.chars[i]=s.chars[i+1]; }
    } else if(Eq(key,"CANCEL")) Cancel(now);
    else if(Eq(key,"CONFIRM")) {
        Utf8(saved,s.length); result=Result{}; result.kind=TEXT;
        std::strcpy(result.text,saved); pending=true; Cancel(now);
    } else {
        const char *p=key; uint32_t c=Decode(p);
        if(!*p) {
            if(c>='a'&&c<='z'&&(s.caps||s.shift)) c-='a'-'A';
            if(!s.abc&&s.katakana) c=Katakana(c);
            Insert(c); if(s.shift) s.shift=false;
        }
    }
    return true;
}
void Handle(int dx,int dy,bool accept,bool cancel,bool touch,int tx,int ty,uint32_t now) {
    const bool tap=touch&&!touched; touched=touch;
    if(!touch) touchFocus=nullptr;
    if(!Active()||s.closing) return;
    if(cancel) { Cancel(now); return; }
    if(dx||dy) { padFocus=true; touchFocus=nullptr; Move(dx,dy); }
    if(s.kind==NUMBER) {
        if(tap) for(int r=0;r<6;++r) for(int c=0;c<(r==5?2:4);++c) {
            KeyRect k=NumberRect(r,c);
            if(tx>=k.x&&tx<k.x+k.w&&ty>=k.y&&ty<k.y+k.h) {
                if(!Enabled(k.key)) return;
                s.row=r; s.column=c; Activate(k.key,now); return;
            }
        }
        if(accept) Activate(NumberKey(s.row,s.column),now);
    } else {
        const int inputX=s.compact?39:23, inputY=s.compact?37:25;
        if(tap && tx>=inputX && tx<inputX+(s.compact?242:274) && ty>=inputY && ty<inputY+(s.compact?21:25)) {
            int best=0, distance=10000;
            for(int i=0;i<=s.length;++i) {
                char prefix[33]; Utf8(prefix,i);
                int d=std::abs(tx-((s.compact?45:31)+GuiV2::MeasureText(prefix)));
                if(d<distance) { best=i; distance=d; }
            }
            s.cursor=best; return;
        }
        int count; const KeyRect *layout=Layout(count);
        for(int i=0;i<count;++i) {
            const KeyRect &k=layout[i];
            if(tap && tx>=k.x&&tx<k.x+k.w&&ty>=k.y&&ty<k.y+k.h) {
                if(!Enabled(k.key)) return;
                padFocus=false; touchFocus=k.key;
                s.row=k.row; s.column=k.column; Activate(k.key,now); return;
            }
            if(accept && k.row==s.row && k.column==s.column) { Activate(k.key,now); return; }
        }
    }
}
// ★下画面ロックの暗幕の色と時間（F-350）。GuiMenu::SyncInputLock が読む。
//   文字入力は薄め（Simulator の TEXT_KEYBOARD.backdropAlpha = 0.38）、
//   数値入力はリストボックスと同じ濃さ。
uint32_t DimColor(void) { return s.kind==TEXT ? 0x61000000u : 0xB8111410u; }
int      DimFadeMs(void) { return 180; }

void Draw(uint32_t now) {
    if(!Active()) return;
    const float a=Amount(now);
    if(a<=0) return;
    // ★暗幕は GuiV2 の「下画面ロック」が出す（F-350）。ここでは描かない。
    //   色と時間は DimColor() / DimFadeMs() で外へ渡している。
    if(s.kind==NUMBER) {
        Label(12,10,s.hex?u8"数値入力 HEX":u8"数値入力 DEC",0xFFA4E463,1);
        Frame(12,25,296,30,0xFF47513F,0xBF090A08,1);
        char buf[32]; std::snprintf(buf,sizeof(buf),s.hex?"0x%s":"%s",s.buffer);
        Label(20,36,buf,0xFFFFFFFF,1,true);
        Label(12,61,"MIN:",0xFF81897D,1);
        int x=12+GuiV2::MeasureText("MIN:");
        NumberFormat(buf,sizeof(buf),s.minimum); Label(x,61,buf,0xFF81897D,1,true);
        x+=GuiV2::MeasureText(buf,1,GuiV2::FONT_NUM);
        Label(x,61,"  MAX:",0xFF81897D,1); x+=GuiV2::MeasureText("  MAX:");
        NumberFormat(buf,sizeof(buf),s.maximum); Label(x,61,buf,0xFF81897D,1,true);
        for(int r=0;r<6;++r) for(int c=0;c<(r==5?2:4);++c) DrawKey(NumberRect(r,c),1);
    } else {
        char title[64]; std::snprintf(title,sizeof(title),u8"%s文字入力 %s",s.compact?u8"小型":"",s.abc?"QWERTY":s.katakana?u8"カナ":u8"かな");
        Label(s.compact?39:8,s.compact?23:8,title,0xFFA4E463,a);
        Frame(s.compact?39:23,s.compact?37:25,s.compact?242:274,s.compact?21:25,0xFF47513F,0xBF090A08,a);
        Label(s.compact?45:31,s.compact?44:34,TextValue(),0xFFFFFFFF,a);
        if((now/500)%2==0) {
            char prefix[33]; Utf8(prefix,s.cursor);
            Rect((s.compact?45:31)+GuiV2::MeasureText(prefix),s.compact?42:32,1,s.compact?11:12,0xFFA4E463,a);
        }
        char count[8]; std::snprintf(count,sizeof(count),"%d/8",s.length);
        Label(s.compact?253:269,s.compact?44:34,count,0xFF81897D,a);
        int n; const KeyRect *layout=Layout(n);
        for(int i=0;i<n;++i) DrawKey(layout[i],a);
    }
}
}}
