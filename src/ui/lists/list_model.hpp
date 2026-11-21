#pragma once
// 一覧の行を一度に渡す入れ物。文字は確保して持ち、閉じたら返す。渡すたびに世代が進む。
#include "ui/timed_repeat.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gohan::ui::lists {
struct Row {
    std::string text;           // 行の文字
    std::uint32_t message = 0;  // メッセージ番号（0 は無し）
    std::string prefix;         // 前置き
    std::string extra;          // 追加テキスト
};
class ListModel {
  public:
    void Set(std::vector<Row> rows) { rows_ = std::move(rows); ++generation_; if (selected_ >= rows_.size()) selected_ = rows_.empty() ? 0 : rows_.size() - 1; }
    void Close() { std::vector<Row>().swap(rows_); selected_ = 0; ++generation_; }
    std::size_t Size() const { return rows_.size(); }
    const Row &At(std::size_t i) const { return rows_[i]; }
    std::uint32_t Generation() const { return generation_; }
    std::size_t Selected() const { return selected_; }
    void Select(std::size_t i) { if (i < rows_.size()) selected_ = i; }
    // 十字の送り: step は +1/-1 行。端では止まる。
    void Move(int rows) {
        if (rows_.empty()) return;
        long v = static_cast<long>(selected_) + rows;
        if (v < 0) v = 0;
        if (v >= static_cast<long>(rows_.size())) v = static_cast<long>(rows_.size()) - 1;
        selected_ = static_cast<std::size_t>(v);
    }
  private:
    std::vector<Row> rows_;
    std::size_t selected_ = 0;
    std::uint32_t generation_ = 0;
};

// 十字の送り。押した時 1 行、400 ms 後から 100 ms ごと。左右は見える行数ぶん。
class DpadRepeat {
  public:
    enum Dir : std::uint32_t { Up = 1, Down = 2, Left = 4, Right = 8 };
    DpadRepeat() : repeat_({400, 100, 0, 100, 0}) {}
    // 返り値: 動かす行数（符号つき）。0 は動かさない。
    int Step(std::uint32_t dirs, std::uint32_t now_ms, int visible_rows) {
        if (!repeat_.Step(dirs, now_ms)) return 0;
        if (dirs & Up) return -1;
        if (dirs & Down) return 1;
        if (dirs & Left) return -visible_rows;
        if (dirs & Right) return visible_rows;
        return 0;
    }
  private:
    TimedRepeat repeat_;
};
} // namespace gohan::ui::lists
