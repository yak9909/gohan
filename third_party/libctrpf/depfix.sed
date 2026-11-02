# depfix.mk が使う。ドライブ付きのパスを MSYS 形式に直す（A:/x → /a/x、C:x → /c/x）。
s@(^|[[:space:]])([A-Za-z]):/?([^[:space:]])@\1/\L\2/\3@g
