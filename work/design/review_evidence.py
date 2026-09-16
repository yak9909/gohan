"""Read-only source inspection for the 2026-09-13 architecture review."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'Sources/GuiMenu.cpp').read_text(encoding='utf-8')
build = source[source.index('void    BuildTree(void)'):source.index('Frame  &Cur(void)')]
sites = len(re.findall(r'\bAddItem\(', build))
loops = [int(n) for n in re.findall(r'while \(i < (\d+)\)', build)]
assert loops == [24, 12], 'Re-inspect BuildTree loops before reusing this calculation'
files = ['gohan.md', 'gohan-menu.md', 'Sources/GuiMenu.cpp', 'Includes/GuiMenu.hpp',
         'Sources/ShizueSkip.cpp', 'Includes/ChatKanji.hpp', 'Makefile']
result = {
    'build_tree_textual_AddItem_calls': sites,
    'constant_loop_counts_with_one_AddItem_each': loops,
    'actual_item_count': sites - len(loops) + sum(loops),
    'kMaxItems': int(re.search(r'kMaxItems\s*=\s*(\d+)', source).group(1)),
    'method': 'Inspected branch-free BuildTree; one AddItem in each constant loop; kLongList loop has no AddItem',
    'hashes': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in files},
}
print(json.dumps(result, indent=2))
