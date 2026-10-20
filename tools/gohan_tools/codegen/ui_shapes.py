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

LETTER = {  # ボタンの字（5x7。美咲の半角は 3 画素幅で丸の中では細すぎるので自作）
    'A': ['.WWW.', 'W...W', 'W...W', 'WWWWW', 'W...W', 'W...W', 'W...W'],
    'B': ['WWWW.', 'W...W', 'WWWW.', 'W...W', 'W...W', 'W...W', 'WWWW.'],
    'X': ['W...W', 'W...W', '.W.W.', '..W..', '.W.W.', 'W...W', 'W...W'],
    'Y': ['W...W', 'W...W', '.W.W.', '..W..', '..W..', '..W..', '..W..'],
    'L': ['W....', 'W....', 'W....', 'W....', 'W....', 'W....', 'WWWWW'],
    'R': ['WWWW.', 'W...W', 'W...W', 'WWWW.', 'W.W..', 'W..W.', 'W...W'],
}
