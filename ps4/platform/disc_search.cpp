#include "../../game/PsyDoom/DiscInfo.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

// Roots are ordered by priority. Keep the production resolver and host tests
// on the same implementation; never mix tracks from different disc folders.
extern "C" int psydoomFindDisc(const char* const* roots, size_t count,
                              char* output, size_t capacity, FILE* log) {
    if (!output || !capacity) return 0;
    output[0]=0;
    for (size_t i=0; i<count; ++i) {
        DIR* dir=opendir(roots[i]);
        if (!dir) { if(log) fprintf(log,"Disc directory unavailable: %s\n",roots[i]); continue; }
        std::vector<std::string> names;
        while (dirent* entry=readdir(dir)) {
            std::string name=entry->d_name;
            if (name.size()<4) continue;
            std::string ext=name.substr(name.size()-4);
            for(char& c:ext) if(c>='A' && c<='Z') c+='a'-'A';
            if(ext==".cue") names.push_back(name);
        }
        closedir(dir);
        std::sort(names.begin(),names.end());
        auto preferred=std::find(names.begin(),names.end(),"Doom.cue");
        if(preferred!=names.end()) std::rotate(names.begin(),preferred,preferred+1);
        for (const auto& name:names) {
            std::string path=std::string(roots[i])+"/"+name;
            if(path.size()>=capacity) continue;
            DiscInfo disc;
            std::string error;
            bool valid=disc.parseFromCueFile(path.c_str(),error);
            bool data=false;
            if(valid) for(const DiscTrack& track:disc.tracks) {
                if(track.bIsData && track.blockCount>0) data=true;
                FILE* file=fopen(track.sourceFilePath.c_str(),"rb");
                if(!file) { valid=false; error="Track file is not readable"; break; }
                fclose(file);
            }
            if(!valid || !data) {
                if(log) fprintf(log,"Skipping unusable disc: %s (%s)\n",path.c_str(),error.c_str());
                continue;
            }
            memcpy(output,path.c_str(),path.size()+1);
            if(log) fprintf(log,"Selected disc: %s\n",output);
            return 1;
        }
    }
    if(log) fprintf(log,"No usable CUE/BIN disc found on HDD or USB\n");
    return 0;
}
