"""DARC の読み書き。ゲームの入力を収録せず、小さな自作データでも検査できる。"""
import struct


def align(n, boundary):
    return (n + boundary - 1) // boundary * boundary


def pack(files: dict[str, bytes]) -> bytes:
    tree = {}
    for path, blob in files.items():
        parts = path.split('/')
        if any(p in ('', '.', '..') or '\\' in p or '\0' in p for p in parts):
            raise ValueError('DARC のパスが不正です')
        node = tree
        for part in parts[:-1]:
            node = node.setdefault(part, {})
            if not isinstance(node, dict):
                raise ValueError('DARC のパスが重複しています')
        if parts[-1] in node:
            raise ValueError('DARC のパスが重複しています')
        node[parts[-1]] = bytes(blob)
    entries = [{'name': '', 'parent': 0}, {'name': '.', 'parent': 0}]

    def walk(node, parent):
        for name in sorted(node, key=lambda x: (isinstance(node[x], dict), x.upper(), x)):
            child = node[name]
            entry = {'name': name}
            entries.append(entry)
            if isinstance(child, dict):
                entry['parent'] = parent
                walk(child, len(entries) - 1)
                entry['end'] = len(entries)
            else:
                entry['data'] = child
    walk(tree, 1)
    entries[0]['end'] = entries[1]['end'] = len(entries)
    names = bytearray()
    for e in entries:
        e['name_offset'] = len(names)
        names += e['name'].encode('utf-16-le') + b'\0\0'
    table_length = 12 * len(entries) + len(names)
    position = align(28 + table_length, 32)
    for e in entries:
        if 'data' in e:
            position = align(position, 128 if e['name'].endswith('.bclim') else 32)
            e['offset'] = position
            position += len(e['data'])
    result = bytearray(position)
    struct.pack_into('<4sHHIIIII', result, 0, b'darc', 0xFEFF, 28, 0x01000000,
                     position, 28, table_length, align(28 + table_length, 32))
    for i, e in enumerate(entries):
        if 'data' in e:
            fields = e['name_offset'], e['offset'], len(e['data'])
            result[e['offset']:e['offset'] + len(e['data'])] = e['data']
        else:
            fields = e['name_offset'] | 0x01000000, e['parent'], e['end']
        struct.pack_into('<III', result, 28 + 12 * i, *fields)
    result[28 + 12 * len(entries):28 + table_length] = names
    return bytes(result)


def read(data: bytes) -> dict[str, bytes]:
    def fits(offset, length, size):
        return 0 <= offset <= size and 0 <= length <= size - offset

    if len(data) < 28:
        raise ValueError('DARC の見出しがありません')
    magic, bom, header, version, size, table, length, payload = struct.unpack_from('<4sHHIIIII', data)
    if (magic, bom, header, version, size) != (b'darc', 0xFEFF, 28, 0x01000000, len(data)):
        raise ValueError('DARC の見出しが不正です')
    if table < 28 or table % 4 or not fits(table, length, size) or length < 12 or not table + length <= payload <= size:
        raise ValueError('DARC の表が範囲外です')
    root, parent, count = struct.unpack_from('<III', data, table)
    if root != 0x01000000 or parent or not 1 <= count <= length // 12:
        raise ValueError('DARC の根が不正です')
    names = table + count * 12
    names_end = table + length
    stack = []
    out = {}
    for i in range(count):
        while stack and i >= stack[-1][1]:
            stack.pop()
        raw, offset, n = struct.unpack_from('<III', data, table + i * 12)
        at = names + (raw & 0x00FFFFFF)
        if raw & 0xFE000000 or at % 2 or not fits(at, 2, names_end):
            raise ValueError('DARC の名前が範囲外です')
        end = at
        while fits(end, 2, names_end) and data[end:end + 2] != b'\0\0':
            end += 2
        if not fits(end, 2, names_end):
            raise ValueError('DARC の名前が終端されていません')
        name = data[at:end].decode('utf-16-le')
        if i == 0 and name:
            raise ValueError('DARC の根の名前が不正です')
        if '/' in name or '\\' in name or name == '..':
            raise ValueError('DARC の名前が不正です')
        if raw & 0x01000000:
            if not i < n <= count or (stack and (offset != stack[-1][0] or n > stack[-1][1])):
                raise ValueError('DARC の階層が不正です')
            stack.append((i, n, name))
        else:
            if not stack or offset < payload or not fits(offset, n, size):
                raise ValueError('DARC のファイルが範囲外です')
            path = '/'.join([e[2] for e in stack if e[2] not in ('', '.')] + [name])
            if path in out:
                raise ValueError('DARC の名前が重複しています')
            out[path] = data[offset:offset + n]
    return out
