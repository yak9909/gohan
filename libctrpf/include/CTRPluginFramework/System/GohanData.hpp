#ifndef CTRPLUGINFRAMEWORK_SYSTEM_GOHANDATA_HPP
#define CTRPLUGINFRAMEWORK_SYSTEM_GOHANDATA_HPP

#include "types.h"
#include <vector>

// gohan: the settings file of this vendored CTRPF is GohanCTRPFData.bin. It keeps the normal CTRPF data
// (header, enabled cheats, favorites, hotkeys) exactly as CTRPFData.bin does and adds one gohan section.
// The header's reserved words [0..3] locate that section. No migration from CTRPFData.bin.
namespace CTRPluginFramework
{
    class GohanData
    {
    public:
        static const char   *FileName;          ///< "GohanCTRPFData.bin" (relative, like CTRPFData.bin)
        static const u32    Magic = 0x4E484F47; ///< 'GOHN' in header.reserved[0]

        /// Fills the gohan section. Called by every save (CTRPF menu close or GohanData::Save).
        using WriteFn = void (*)(std::vector<u8> &out);
        /// Receives the gohan section (size 0 when the file or the section is missing / invalid).
        using ReadFn = void (*)(const u8 *data, u32 size);

        static void     SetHandlers(WriteFn writer, ReadFn reader);

        /// Reads the gohan section and passes it to the reader. Returns true when a section was found.
        static bool     Load(void);
        /// Writes the whole file now (CTRPF part and gohan part). Safe from any thread.
        static void     Save(void);
    };
}

#endif
