#pragma once
// 組む前の bclyt の節（セクション）編集。探す・複製して改名・値・枠・節の数と全体の大きさ。
// 形式: 'FLYT' 見出し(0x14) の後に、節が [4 文字の印][u32 大きさ][本体] で並ぶ。
// 大きさ・数は見出しの値と照合し、壊れた入力は断る。
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gohan::game::lyt {
class BclytEdit {
  public:
    // 壊れた入力（印・大きさ・節数の不整合）は false。
    bool Load(const std::uint8_t *data, std::size_t size);
    std::size_t SectionCount() const { return sections_.size(); }
    std::size_t TotalSize() const { return Header() + Body(); }
    // 印 tag の n 番目（0 始まり）。無ければ -1。
    int Find(const char tag[4], int nth = 0) const;
    // 名前欄（pan1 の先頭 +0x08 から 24 バイト）で探す。
    int FindByName(const char *name) const;
    // 節 index を複製して直後に挿入し、新しい名前を付ける。返り値は新しい節の番号、失敗は -1。
    int Duplicate(int index, const char *new_name);
    bool Rename(int index, const char *name);
    // 節本体（印・大きさを除く）の offset に値を書く。
    bool SetU32(int index, std::size_t offset, std::uint32_t v);
    bool SetFloat(int index, std::size_t offset, float v);
    // pan1 の枠: 位置(x,y)と大きさ(w,h)。本体の +0x2C 以降の float 4 つ。
    bool SetRect(int index, float x, float y, float w, float h);
    bool GetRect(int index, float out[4]) const;
    // 出力。見出しの大きさと節数を更新して並べ直す。
    std::vector<std::uint8_t> Build() const;
    static constexpr std::size_t NameOffset = 0x08, NameBytes = 24, RectOffset = 0x2C;
  private:
    static constexpr std::size_t Header() { return 0x14; }
    std::size_t Body() const;
    struct Section { char tag[4]; std::vector<std::uint8_t> body; };
    std::vector<std::uint8_t> head_;
    std::vector<Section> sections_;
};
} // namespace gohan::game::lyt
