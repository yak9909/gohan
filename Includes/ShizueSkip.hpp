#ifndef SHIZUESKIP_HPP
#define SHIZUESKIP_HPP

namespace CTRPluginFramework
{
    // タイトルのプレイヤー選択後、しずえ画面（部屋 0x63）から村へ移動する。
    // ゲームのメインスレッド上で case 0 の起動時一括処理を済ませてから、
    // 実機確認済みの case 1 相当（Room_UseExit）を一度だけ実行する。
    namespace ShizueSkip
    {
        bool    Install(void);
        void    Uninstall(void);
        bool    IsHooked(void);
    }
}

#endif
