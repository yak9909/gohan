"""自作の UI のドット絵。点は透明、W は白、K は指の縁。"""

FINGER = [
    '....KKKK.....',
    '...KWWWWK....',
    'KKKKWWWWKKKK.',
    'KWWWWWWWWWWWK',
    'KKKKWWWWWWWWK',
    '...KWWWWWWWWK',
    '...KWWWWWWWK.',
    '...KKKKKKKK..',
]

CHEVRON = ['W...', 'WW..', '.WW.', '..WW', '.WW.', 'WW..', 'W...']

ARROW_L = ['..W', '.WW', 'WWW', '.WW', '..W']

ARROW_R = ['W..', 'WW.', 'WWW', 'WW.', 'W..']

ARROW_U = ['..W..', '.WWW.', 'WWWWW']

ARROW_D = ['WWWWW', '.WWW.', '..W..']

CHECK = ['.......W', '......WW', 'W....WW.', 'WW..WW..', '.WWWW...', '..WW....']

BANG = ['WW', 'WW', 'WW', 'WW', '..', 'WW']

LOCK = ['.WWW.', 'W...W', 'W...W', 'WWWWW', 'WW.WW', 'WW.WW', 'WWWWW']

DOT = ['.WW.', 'WWWW', 'WWWW', '.WW.']

PLUS = ['..W..', '..W..', 'WWWWW', '..W..', '..W..']

# お気に入りの星（7×7）。K は縁、W は中。
STAR = [
    '...K...',
    '..KWK..',
    'KKKWKKK',
    'KWWWWWK',
    '.KWWWK.',
    '.KWKWK.',
    '.KK.KK.',
]

# 操作のリストから行への指し（5×9、白い三角。頂点色で色を付ける）。
POINTER = ['....W', '...WW', '..WWW', '.WWWW', 'WWWWW', '.WWWW', '..WWW', '...WW', '....W']

# START の字（3×5）。START の印（D.12.5）の中に 4 画素ごとに置く。
START_WORD = 'START'
START_LETTERS = {
    'S': ['WWW', 'W..', 'WWW', '..W', 'WWW'],
    'T': ['WWW', '.W.', '.W.', '.W.', '.W.'],
    'A': ['.W.', 'W.W', 'WWW', 'W.W', 'W.W'],
    'R': ['WW.', 'W.W', 'WW.', 'W.W', 'W.W'],
}

# 複数選択の箱（9×9）。四隅の 1 画素を落として角を少し丸める（D.12.4）。K は縁、F は中、✓ の画素は CHECK_MARK。
CHECK_BOX = [
    '.KKKKKKK.',
    'KFFFFFFFK',
    'KFFFFFFFK',
    'KFFFFFFFK',
    'KFFFFFFFK',
    'KFFFFFFFK',
    'KFFFFFFFK',
    'KFFFFFFFK',
    '.KKKKKKK.',
]
# ✓ の画素（x, y）。箱の中の 5×5 で、(2, 2) から書く。
CHECK_MARK = {(6, 2), (5, 3), (6, 3), (2, 4), (4, 4), (5, 4), (2, 5), (3, 5), (4, 5), (3, 6)}

LETTER = {  # ボタンの字（5x7。美咲の半角は 3 画素幅で丸の中では細すぎるので自作）
    'A': ['.WWW.', 'W...W', 'W...W', 'WWWWW', 'W...W', 'W...W', 'W...W'],
    'B': ['WWWW.', 'W...W', 'WWWW.', 'W...W', 'W...W', 'W...W', 'WWWW.'],
    'X': ['W...W', 'W...W', '.W.W.', '..W..', '.W.W.', 'W...W', 'W...W'],
    'Y': ['W...W', 'W...W', '.W.W.', '..W..', '..W..', '..W..', '..W..'],
    'L': ['W....', 'W....', 'W....', 'W....', 'W....', 'W....', 'WWWWW'],
    'R': ['WWWW.', 'W...W', 'W...W', 'WWWW.', 'W.W..', 'W..W.', 'W...W'],
}
