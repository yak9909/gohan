#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// ゲームのポインタを持たない構文/暦処理。native Dateとの境界でだけ使う。
namespace ChatCommandParser {
enum class Kind { None, Error, List, ItemId, ItemName, Weather, Style, TimeMenu, Time };
enum class TimeOp { Set, Add, Sub };
enum class TimePart { Minutes, Clock, Date, Named };
struct Command {
    Kind kind = Kind::None;
    uint16_t item = 0;
    int weather = 0;
    TimeOp op = TimeOp::Set;
    TimePart part = TimePart::Minutes;
    int a = 0, b = 0, c = 0;
    bool hasYear = false;
    char query[100] = {};
};
struct Date {
    int32_t year;
    uint8_t month, day, weekday, hour, minute, second;
    uint16_t millis;
};
static_assert(sizeof(Date) == 12, "native Date ABI");
inline bool Number(const char *s, size_t n, int &out) {
    if (!n || n > 4) return false;
    out = 0;
    for (size_t i=0;i<n;++i) { if (s[i]<'0'||s[i]>'9') return false; out=out*10+s[i]-'0'; }
    return true;
}
inline bool TimeValue(const char *s, Command &out) {
    const size_t n=std::strlen(s);
    const char *colon=std::strchr(s,':');
    if (colon) {
        out.part=TimePart::Clock;
        return !std::strchr(colon+1,':') && Number(s,colon-s,out.a) && Number(colon+1,n-(colon-s)-1,out.b);
    }
    const char *first=std::strpbrk(s,"-/");
    if (!first) { out.part=TimePart::Minutes; return Number(s,n,out.a); }
    const char *last=std::strpbrk(first+1,"-/");
    out.part=TimePart::Date;
    out.hasYear=last!=nullptr;
    if (!last) return Number(s,first-s,out.b) && Number(first+1,n-(first-s)-1,out.c);
    return !std::strpbrk(last+1,"-/") && Number(s,first-s,out.a)
        && Number(first+1,last-first-1,out.b) && Number(last+1,n-(last-s)-1,out.c);
}
inline Command Parse(const char *text) {
    Command out;
    if (!text || !*text) return out;
    if (!std::strcmp(text,"list")) { out.kind=Kind::List; return out; }
    if (!std::strcmp(text,"hair")||!std::strcmp(text,"style")||!std::strcmp(text,"eye")||!std::strcmp(text,"eyes")) {
        out.kind=Kind::Style; return out;
    }
    if (!std::strncmp(text,"weather",7) && (!text[7] || text[7]==' ')) {
        out.kind=Kind::Error;
        if (!std::strcmp(text,"weather clear")) {out.kind=Kind::Weather;out.weather=1;}
        if (!std::strcmp(text,"weather cloudy")) {out.kind=Kind::Weather;out.weather=3;}
        if (!std::strcmp(text,"weather rain")) {out.kind=Kind::Weather;out.weather=5;}
        return out;
    }
    if (!std::strncmp(text,"time",4) && (!text[4] || text[4]==' ')) {
        out.kind=Kind::Error;
        if (!std::strcmp(text,"time set")) {out.kind=Kind::TimeMenu;return out;}
        const char *value=nullptr;
        if (!std::strncmp(text,"time set ",9)) {out.op=TimeOp::Set;value=text+9;}
        if (!std::strncmp(text,"time add ",9)) {out.op=TimeOp::Add;value=text+9;}
        if (!std::strncmp(text,"time sub ",9)) {out.op=TimeOp::Sub;value=text+9;}
        if (!value) return out;
        if (out.op==TimeOp::Set) {
            const char *names[]={"day","night","noon","midnight"};
            const int hours[]={7,18,12,0};
            for (size_t i=0;i<sizeof(names)/sizeof(names[0]);++i) if (!std::strcmp(value,names[i])) {
                out.kind=Kind::Time;out.part=TimePart::Named;out.a=hours[i];return out;
            }
        }
        if (TimeValue(value,out)) out.kind=Kind::Time;
        return out;
    }
    const char c=text[0];
    if ((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) {
        out.kind=Kind::Error;
        size_t n=std::strlen(text); if(n>4) return out;
        uint32_t id=0;
        for(size_t i=0;i<n;++i) {
            const char x=text[i];
            int d=x>='0'&&x<='9'?x-'0':x>='a'&&x<='f'?x-'a'+10:x>='A'&&x<='F'?x-'A'+10:-1;
            if(d<0)return out;
            id=id*16+static_cast<unsigned>(d);
        }
        if (id<0x7FFE) {out.kind=Kind::ItemId;out.item=static_cast<uint16_t>(id);}
        return out;
    }
    // §14のかな名検索。普通のASCIIチャットは変更しない。
    if (static_cast<unsigned char>(c)>=0x80) {
        out.kind=Kind::ItemName;
        std::strncpy(out.query,text,sizeof(out.query)-1);
    }
    return out;
}
inline bool Leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
inline int Days(int y,int m) {
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m>=1&&m<=12?days[m-1]+(m==2&&Leap(y)?1:0):0;
}
inline bool Valid(const Date &d) {
    return d.year>=2012&&d.year<=2050 && d.month>=1&&d.month<=12 && d.day>=1&&d.day<=Days(d.year,d.month)
        && d.hour<24&&d.minute<60&&d.second<60&&d.millis<1000;
}
inline int Ordinal(const Date &d) {
    int day=0;
    for(int y=2012;y<d.year;++y)day+=Leap(y)?366:365;
    for(int m=1;m<d.month;++m)day+=Days(d.year,m);
    return day+d.day-1;
}
inline bool FromOrdinal(int value,Date &d) {
    if(value<0)return false;
    int y=2012,m=1;
    while(y<=2050 && value>=(Leap(y)?366:365)) {value-=Leap(y)?366:365;++y;}
    if(y>2050)return false;
    while(value>=Days(y,m)){value-=Days(y,m);++m;}
    d.year=y;d.month=static_cast<uint8_t>(m);d.day=static_cast<uint8_t>(value+1);
    return true;
}
inline bool ApplyTime(const Command &cmd,const Date &current,Date &out) {
    if(cmd.kind!=Kind::Time||!Valid(current))return false;
    out=current;
    if(cmd.op==TimeOp::Set) {
        if(cmd.part==TimePart::Minutes) {if(cmd.a>=60)return false;out.minute=static_cast<uint8_t>(cmd.a);out.second=0;out.millis=0;}
        else if(cmd.part==TimePart::Clock||cmd.part==TimePart::Named) {
            if(cmd.a>=24||cmd.b>=60)return false;
            out.hour=static_cast<uint8_t>(cmd.a);out.minute=static_cast<uint8_t>(cmd.b);out.second=0;out.millis=0;
        } else {
            if(cmd.b<1||cmd.b>12||cmd.c<1||cmd.c>31)return false;
            if(cmd.hasYear)out.year=cmd.a;
            out.month=static_cast<uint8_t>(cmd.b);out.day=static_cast<uint8_t>(cmd.c);
        }
    } else {
        const int sign=cmd.op==TimeOp::Add?1:-1;
        int64_t delta=0;
        if(cmd.part==TimePart::Date) {
            // 月の加減算は暦で扱い、月末は移動先の最終日へ丸める（30日固定にはしない）。
            const int64_t months=static_cast<int64_t>(out.year)*12+out.month-1+sign*(static_cast<int64_t>(cmd.a)*12+cmd.b);
            if(months<2012*12||months>=2051*12)return false;
            out.year=static_cast<int32_t>(months/12);out.month=static_cast<uint8_t>(months%12+1);
            if(out.day>Days(out.year,out.month))out.day=static_cast<uint8_t>(Days(out.year,out.month));
            delta=static_cast<int64_t>(cmd.c)*24*60;
        } else delta=cmd.part==TimePart::Minutes?cmd.a:static_cast<int64_t>(cmd.a)*60+cmd.b;
        const int64_t minutes=static_cast<int64_t>(Ordinal(out))*24*60+out.hour*60+out.minute+sign*delta;
        if(minutes<0 || minutes/1440>20000 || !FromOrdinal(static_cast<int>(minutes/1440),out))return false;
        out.hour=static_cast<uint8_t>(minutes%1440/60);out.minute=static_cast<uint8_t>(minutes%60);
    }
    if(!Valid(out))return false;
    // 2012-01-01は日曜。nativeが使う曜日も日付と一致させる。
    out.weekday=static_cast<uint8_t>(Ordinal(out)%7);
    return true;
}
inline bool NormalizeKana(const char *in,char *out,size_t cap) {
    size_t n=0;
    for(const unsigned char *p=reinterpret_cast<const unsigned char *>(in);*p;) {
        uint32_t c=*p++;
        if(c>=0x80) {
            if(c>=0xC2&&c<0xE0) {if((*p&0xC0)!=0x80)return false;c=((c&31)<<6)|(*p++&63);}
            else if(c>=0xE0&&c<0xF0) {
                if(!p[0]||!p[1]||(p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80)return false;
                c=((c&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;
            } else return false;
        }
        if(c>=0x3041&&c<=0x3096)c+=0x60;
        if(c>=0xFF01&&c<=0xFF5E)c-=0xFEE0;
        if(c>='A'&&c<='Z')c+='a'-'A';
        const size_t bytes=c<0x80?1:c<0x800?2:3;
        if(n+bytes>=cap)return false;
        if(bytes==1)out[n++]=static_cast<char>(c);
        else if(bytes==2){out[n++]=static_cast<char>(0xC0|(c>>6));out[n++]=static_cast<char>(0x80|(c&63));}
        else{out[n++]=static_cast<char>(0xE0|(c>>12));out[n++]=static_cast<char>(0x80|((c>>6)&63));out[n++]=static_cast<char>(0x80|(c&63));}
    }
    out[n]=0;return true;
}
}
