"""PNG を埋め込んだ、外部ファイルに依存しない一覧ページ。"""
import base64
from html import escape


def gallery(frames: list[dict], screen: str) -> str:
    cards = []
    for frame in frames:
        views = []
        for scale in (1, 2):
            images = []
            for which, width in (('top', 400), ('bottom', 320)):
                data = base64.b64encode(frame[which].png()).decode('ascii')
                images.append(f'<img alt="{which}" width="{width * scale}" height="{240 * scale}" '
                              f'src="data:image/png;base64,{data}">')
            views.append(f'<figure><figcaption>{scale}倍</figcaption>{"".join(images)}</figure>')
        cards.append(f'<article><h2>{escape(frame["label"])}</h2><p>{frame["at"]} ms</p>'
                     f'<div class="views">{"".join(views)}</div></article>')
    return ('<!doctype html><html lang="ja"><meta charset="utf-8"><title>メニューのプレビュー</title>'
            '<style>body{background:#151a20;color:#edf2f4;font:14px sans-serif;margin:24px}'
            'article{border-top:1px solid #53606b;margin:24px 0}h2{font-size:18px}'
            '.views{display:flex;gap:24px;align-items:flex-start;flex-wrap:wrap}'
            'figure{margin:0}img{display:block;image-rendering:pixelated;margin:8px auto}'
            'figcaption{margin:8px 0}</style><h1>メニューのプレビュー</h1>'
            f'<p>メニューの画面: {escape(screen)}。ブラウザーの表示倍率を100%にして確認してください。</p>'
            + ''.join(cards) + '</html>')
