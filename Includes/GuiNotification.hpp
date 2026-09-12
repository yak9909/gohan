#pragma once

namespace CTRPluginFramework
{
    namespace GuiNotification
    {
        /// Register the OSD callback. Call once before PluginMenu::Run().
        void Initialize(void);

        /// Add a notification to the bottom-right stack.
        void Show(void);

        /// Add a notification with an explicit title and message.
        void Notify(const char *title, const char *message);

        /// Add a red notification (e.g. cheat disabled).
        void NotifyRed(const char *title, const char *message);

        /// Unregister the OSD callback during plugin shutdown.
        void Shutdown(void);
    }
}
