#pragma once
#include "InteriorSnapshot.hpp"
#include <cstdio>

// Storage operations are supplied so failure at each filesystem step can be tested.
namespace InteriorStorage {
template<class Files>
bool Write(const char *path, const InteriorSnapshot::Snapshot &s, Files &files) {
    using InteriorSnapshot::Snapshot;
    if (!InteriorSnapshot::Valid(s)) return false;
    char tmp[224], old[224];
    const int tn=std::snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    const int on=std::snprintf(old,sizeof(old),"%s.old",path);
    if (tn<=0 || on<=0 || static_cast<unsigned>(tn)>=sizeof(tmp)
        || static_cast<unsigned>(on)>=sizeof(old)) return false;
    if (!files.Exists(path) && files.Exists(old) && !files.Rename(old,path)) return false;
    if (!files.Write(tmp,s)) return false;
    Snapshot check;
    if (!files.Read(tmp,check) || std::memcmp(&s,&check,sizeof(s))) {
        files.Remove(tmp); return false;
    }
    const bool had=files.Exists(path);
    if (files.Exists(old) && !files.Remove(old)) return false;
    if (had && !files.Rename(path,old)) return false;
    if (!files.Rename(tmp,path)) {
        if (had) files.Rename(old,path);
        return false;
    }
    if (!files.Read(path,check) || std::memcmp(&s,&check,sizeof(s))) {
        files.Remove(path);
        if (had) files.Rename(old,path);
        return false;
    }
    return true;
}
}
