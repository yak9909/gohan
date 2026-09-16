// 通知の公開窓（段 7）。
//
// ★旧版は OSD（screen.DrawRect）で描いていたが、OSD はチカチカして
//   ゲームを止めるメニューと連動しない。段 7 で自前 GUI（GuiRenderer）の
//   timeline へ載せ替えた。上画面右下・キュー・押し上げは GuiMenu 側。
//   ここは依頼だけ受けて GuiMenu::Notify へ渡す。

#include "GuiNotification.hpp"
#include "GuiMenu.hpp"

#include <CTRPluginFramework.hpp>

namespace CTRPluginFramework
{
    namespace GuiNotification
    {
        void Initialize(void)
        {
        }

        void Show(void)
        {
            GuiMenu::Notify("CHEAT ENABLED", u8"歩行速度アップを有効にしました");
        }

        void Notify(const char *title, const char *message)
        {
            GuiMenu::Notify(title, message);
        }

        void NotifyRed(const char *title, const char *message)
        {
            GuiMenu::NotifyRed(title, message);
        }

        void Shutdown(void)
        {
        }
    }
}
