#include "CTRPluginFramework/System/Controller.hpp"
#include "CTRPluginFrameworkImpl/Preferences.hpp"
#include "CTRPluginFrameworkImpl/Menu/PluginMenuImpl.hpp"
#include "CTRPluginFramework/System/GohanData.hpp"
#include "CTRPluginFramework/System/Mutex.hpp"
#include "CTRPluginFramework/System/Lock.hpp"
#include "3ds.h"
#include <cmath>

namespace CTRPluginFramework
{
    using LCDBacklight = Preferences::LCDBacklight;

    BMPImage *  Preferences::topBackgroundImage = nullptr;
    BMPImage *  Preferences::bottomBackgroundImage = nullptr;

    u32         Preferences::MenuHotkeys = static_cast<u32>(Key::Select);
    u64         Preferences::Flags = 0;
    LCDBacklight Preferences::Backlights[2];
    FwkSettings Preferences::Settings;

    std::string Preferences::CheatsFile;
    std::string Preferences::ScreenshotPath;
    std::string Preferences::ScreenshotPrefix;

    bool        Preferences::_cheatsAlreadyLoaded = false;
    bool        Preferences::_favoritesAlreadyLoaded = false;
    bool        Preferences::_bmpCanBeLoaded = true;

    static const char *g_signature = "CTRPF\0\0";

    // gohan: GohanCTRPFData.bin instead of CTRPFData.bin, plus one gohan section (see GohanData.hpp).
    // The CTRPF menu thread and the gohan menu thread may both save: one lock around every file access.
    const char                  *GohanData::FileName = "GohanCTRPFData.bin";
    static GohanData::WriteFn   g_gohanWriter = nullptr;
    static GohanData::ReadFn    g_gohanReader = nullptr;
    static Mutex                g_fileLock;
    static const u32            g_gohanSectionVersion = 1;

    BMPImage *RegionFromCenter(BMPImage *img, int maxX, int maxY)
    {
        BMPImage *temp = new BMPImage(maxX, maxY);

        u32 cx = img->Width() / 2;
        u32 cy = img->Height() / 2;

        img->RoiFromCenter(cx, cy, maxX, maxY, *temp);

        delete img;
        return (temp);
    }

    BMPImage *UpSampleUntilItsEnough(BMPImage *img, int maxX, int maxY)
    {
        BMPImage *temp = new BMPImage(img->Width() * 2, img->Height() * 2);

        img->UpSample(*temp);
        delete img;

        if (temp->Width() < static_cast<u32>(maxX) || temp->Height() < static_cast<u32>(maxY))
            return (UpSampleUntilItsEnough(temp, maxX, maxY));
        return (temp);
    }

    /*BMPImage *UpSampleThenCrop(BMPImage *img, int maxX, int maxY)
    {
        BMPImage *temp = UpSampleUntilItsEnough(img, maxX, maxY);

        BMPImage *res =  new BMPImage(maxX, maxY);

        u32 cx = temp->Width() / 2;
        u32 cy = temp->Height() / 2;

        temp->RoiFromCenter(cx, cy, maxX, maxY, *res);

        delete temp;

        return (res);
    }*/

    float GetRatio(int width, int height, int maxX, int maxY)
    {
        if (width >= height)
            return ((float)width / maxX);
        return ((float)height / maxY);
    }

    BMPImage *PostProcess(BMPImage *img, int maxX, int maxY)
    {
        int width = img->Width();
        int height = img->Height();

        if (width == maxX && height == maxY)
            return img;

        float ratio = GetRatio(width, height, maxX, maxY);

        int newWidth = (int)(ceilf((float)width / ratio));
        int newHeight = (int)(ceilf((float)height / ratio));

        BMPImage *temp = new BMPImage(newWidth, newHeight);

        img->Resample(*temp, newWidth, newHeight);
        delete img;

        if (newWidth != maxX || newHeight != maxY)
        {
            BMPImage *res = new BMPImage(*temp, maxX, maxY);
            delete temp;
            return res;
        }

        return (temp);
    }

    int     Preferences::OpenConfigFile(File &settings, Header &header)
    {
        if (File::Open(settings, GohanData::FileName) == 0 && settings.GetSize() > 0)
        {
             // Check version
            int     res = 0;

            if (settings.Read(&header, sizeof(u32) * 6)) return (-2);

            // Check file
            if (header.size != settings.GetSize()
               || !std::equal(g_signature, g_signature + 8, header.sig))
                return -1;

            if (header.version != SETTINGS_VERSION)
            {
                OSD::Notify(Color::Orange << "Config file version mismatch!");
                OSD::Notify("Default settings applied");
                return -1;
            }

            // Rewind file
            settings.Rewind();

            res = settings.Read(&header, sizeof(Header));

            return res;
        }

        return -1;
    }

    void    Preferences::LoadSettings(void)
    {
        Lock    lock(g_fileLock);
        File    settings;
        Header  header = { 0 };

        if (OpenConfigFile(settings, header) == 0)
        {
            MenuHotkeys = header.hotkeys & ((System::IsNew3DS() && Settings.AreN3DSButtonsAvailable) ? ~0x0 : ~(Key::CStick | Key::ZL | Key::ZR));
            Flags = header.flags;
            memcpy(reinterpret_cast<void*>(Backlights), &header.lcdbacklights, sizeof(Backlights));
        }

        // Check for hotkeys to be valid
        if (MenuHotkeys == 0)
            MenuHotkeys = Key::Select;
    }

    void    Preferences::LoadSavedEnabledCheats(void)
    {
        Lock    lock(g_fileLock);
        File    settings;
        Header  header = { 0 };

        if (_cheatsAlreadyLoaded)
        {
            MessageBox("Error\nCheats already loaded")();
            return;
        }

        if (OpenConfigFile(settings, header) == 0)
        {
            if (header.enabledCheatsCount != 0)
                PluginMenuImpl::LoadEnabledCheatsFromFile(header, settings);
            _cheatsAlreadyLoaded = true;
        }
    }

    void    Preferences::LoadSavedFavorites(void)
    {
        Lock    lock(g_fileLock);
        File    settings;
        Header  header = { 0 };

        if (_favoritesAlreadyLoaded)
        {
            MessageBox("Error\nFavorites already loaded")();
            return;
        }

        if (OpenConfigFile(settings, header) == 0)
        {
            if (header.favoritesCount != 0)
                PluginMenuImpl::LoadFavoritesFromFile(header, settings);
            _favoritesAlreadyLoaded = true;
        }
    }

    void    Preferences::LoadHotkeysFromFile(void)
    {
        Lock    lock(g_fileLock);
        File    settings;
        Header  header = { 0 };

        if (OpenConfigFile(settings, header) == 0)
        {
            if (header.hotkeysCount != 0)
                PluginMenuImpl::LoadHotkeysFromFile(header, settings);
        }
    }

    void    Preferences::LoadBackgrounds(void)
    {
        if (!_bmpCanBeLoaded)
            return;

        Task    task([](void *arg UNUSED)
        {
            std::string source;

            if (FwkSettings::Header->isDefaultPlugin)
                source = "/luma/plugins/ActionReplay/";

            // Try to load top background
            if (topBackgroundImage == nullptr && File::Exists(source + "TopBackground.bmp"))
            {
                BMPImage *image = new BMPImage(source + "TopBackground.bmp");

                if (image->IsLoaded())
                    image = PostProcess(image, 340, 200);
                else
                {
                    delete image;
                    image = nullptr;
                }

                topBackgroundImage = image;
            }

            // Try to load bottom background
            if (bottomBackgroundImage == nullptr && File::Exists(source + "BottomBackground.bmp"))
            {
                BMPImage *image = new BMPImage(source + "BottomBackground.bmp");

                if (image->IsLoaded())
                    image = PostProcess(image, 280, 200);
                else
                {
                    delete image;
                    image = nullptr;
                }

                bottomBackgroundImage = image;
            }

            // Update Window
            Window::UpdateBackgrounds();

            return (s32)0;
        });

        task.Start();

        _bmpCanBeLoaded = false;
    }

    void    Preferences::UnloadBackgrounds(void)
    {
        if (bottomBackgroundImage || topBackgroundImage)
            _bmpCanBeLoaded = true;

        if (bottomBackgroundImage)
        {
            delete bottomBackgroundImage;
            bottomBackgroundImage = nullptr;
        }

        if (topBackgroundImage)
        {
            delete topBackgroundImage;
            topBackgroundImage = nullptr;
        }

        // Update Window
        Window::UpdateBackgrounds();
    }

    void    Preferences::WriteSettings(void)
    {
        Lock    lock(g_fileLock);
        OSDImpl::DrawSaveIcon = true;

        File    settings;
        int     mode = File::READ | File::WRITE | File::CREATE | File::TRUNCATE | File::SYNC;
        Header  header = { 0 };

        std::copy(g_signature, g_signature + 8, header.sig);
        header.version = SETTINGS_VERSION;
        header.hotkeys = MenuHotkeys;
        header.flags = Flags;
        memcpy(&header.lcdbacklights, Backlights, sizeof(header.lcdbacklights));

        if (File::Open(settings, GohanData::FileName, mode) == 0)
        {
            if (settings.Write(&header, sizeof(Header)) != 0) goto error;

            if (IsEnabled(AutoSaveCheats))
                PluginMenuExecuteLoop::WriteEnabledCheatsToFile(header, settings);
            if (IsEnabled(AutoSaveFavorites))
                PluginMenuImpl::WriteFavoritesToFile(header, settings);

            PluginMenuImpl::WriteHotkeysToFile(header, settings);

            // gohan section: reserved[0] magic, [1] version, [2] offset, [3] size
            if (g_gohanWriter != nullptr)
            {
                std::vector<u8>     section;

                g_gohanWriter(section);
                header.reserved[0] = GohanData::Magic;
                header.reserved[1] = g_gohanSectionVersion;
                header.reserved[2] = static_cast<u32>(settings.Tell());
                header.reserved[3] = static_cast<u32>(section.size());
                if (!section.empty() && settings.Write(section.data(), section.size()) != 0)
                    header.reserved[0] = 0;
            }

            header.size = settings.Tell();

            settings.Rewind();
            settings.Write(&header, sizeof(Header));
        }

    error:

        PluginMenuActionReplay::SaveCodes();
        OSDImpl::DrawSaveIcon = false;
    }

    void    GohanData::SetHandlers(WriteFn writer, ReadFn reader)
    {
        Lock    lock(g_fileLock);

        g_gohanWriter = writer;
        g_gohanReader = reader;
    }

    bool    GohanData::Load(void)
    {
        Lock                lock(g_fileLock);
        File                settings;
        Preferences::Header header = { 0 };
        std::vector<u8>     section;
        bool                found = false;

        if (Preferences::OpenConfigFile(settings, header) == 0
            && header.reserved[0] == Magic && header.reserved[1] == g_gohanSectionVersion
            && static_cast<u64>(header.reserved[2]) + header.reserved[3] <= settings.GetSize())
        {
            section.resize(header.reserved[3]);
            if (section.empty()
                || (settings.Seek(header.reserved[2], File::SET) == 0 && settings.Read(section.data(), section.size()) == 0))
                found = true;
            else
                section.clear();
        }

        if (g_gohanReader != nullptr)
            g_gohanReader(found ? section.data() : nullptr, found ? static_cast<u32>(section.size()) : 0);
        return found;
    }

    void    GohanData::Save(void)
    {
        Preferences::WriteSettings();
    }

    void    Preferences::ApplyBacklight(void)
    {
        if (Backlights[0].isEnabled && Backlights[0].value > 0)
            ScreenImpl::Top->SetBacklight(Backlights[0].value);
        if (Backlights[1].isEnabled && Backlights[1].value > 0)
            ScreenImpl::Bottom->SetBacklight(Backlights[1].value);
    }

    void    Preferences::Initialize(void)
    {
        LoadBackgrounds();
    }
}
