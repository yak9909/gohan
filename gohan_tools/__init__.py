"""作業ツリー直下からも tools の道具を python -m で呼べるようにする。"""
from pathlib import Path

__path__.append(str(Path(__file__).resolve().parents[1] / 'tools/gohan_tools'))
