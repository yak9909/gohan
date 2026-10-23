#!/usr/bin/env python3
"""手元のゲームコードと最適化済みの帯コールバックを ARM CPU で実行する。"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
                               UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                               UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)

ROOT = Path(__file__).resolve().parents[2]
CODE_BASE = 0x100000
CODE_SHA256 = 'c635af8f08937b66f123b91b02391a68c034393320cdb94d603b9291383faabb'
CTX, PIC, MAT, TEX = (0x20000000, 0x20001000, 0x20002000, 0x20003000)
RECORD, CAPACITY = 0x20010080, 0x20000
STACK, STOP = 0x300FF000, 0x30000000
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)


class Machine:
    def __init__(self, code, elf):
        self.cpu = u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        u.mem_map(CODE_BASE, 0xA00000)
        u.mem_write(CODE_BASE, code)
        u.mem_map(0x10000000, 0x10000)
        u.mem_map(0x20000000, 0x100000)
        u.mem_map(STOP, 0x100000)
        if 'plugin_range' in elf:
            start, end = elf['plugin_range']
            u.mem_map(start, end - start)
        for address, data in elf['segments']:
            u.mem_write(address, data)
        self.symbols = elf['symbols']
        self.picture, self.flush, plen, pcrc, flen, fcrc = self.words(self.symbols['GameEntries'], 6)
        for address, length, crc in ((self.picture, plen, pcrc), (self.flush, flen, fcrc)):
            assert zlib.crc32(u.mem_read(address, length)) == crc, 'ゲーム入口の照合不一致'
        u.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
        u.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        self.entry = self.run(self.symbols['StreamingEntry'])
        if 'plugin_entry' in elf:
            self.entry = elf['plugin_entry']
        self.events = []
        self.instructions = 0
        self.copies = 0
        u.hook_add(UC_HOOK_CODE, self.instruction)
        u.hook_add(UC_HOOK_MEM_WRITE, self.write)
        # ゲームの描画作業体と同じ配列配置。画素・レイアウト資源は使わない。
        self.put(CTX + 0x668, CTX + 0x94, CTX + 0x244, CTX + 0x454, CTX + 0x284)
        for offset, index in ((0x88, 6), (0x238, 0x20), (0x448, 0x40)):
            self.put(CTX + offset, 0x80000000 | index, 0xF02C0, 0, 0)
        self.put(CTX + 0x680, 0x20008000)
        self.put(MAT + 0x10, 0, 0xFFFFFFFF)
        self.put(MAT + 0x30, 0x15, TEX)
        self.byte(MAT + 0x4D, 4)
        self.put(TEX + 4, 0x21000000)
        self.put(TEX + 0x14, 0x2200, 0x00400040, 10)
        self.floats(PIC + 0x48, 182., 11.)
        self.floats(PIC + 0x80, 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., -1.)
        self.byte(PIC + 0xB5, 255, 0x14, 1)
        self.byte(PIC + 0xD4, 1)
        self.put(PIC + 0xD8, 1)
        self.floats(PIC + 0xDC, 0., 0., 1., 1.)
        self.put(PIC + 0x13C, MAT)
        self.put(PIC + 0x140, *([0xFFFFFFFF] * 4))
        u.mem_write(RECORD - 128, bytes([0xA5]) * (CAPACITY + 256))
        self.cursor = RECORD

    def words(self, address, count):
        return struct.unpack('<' + 'I' * count, self.cpu.mem_read(address, count * 4))

    def put(self, address, *values):
        self.cpu.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def floats(self, address, *values):
        self.cpu.mem_write(address, struct.pack('<' + 'f' * len(values), *values))

    def byte(self, address, *values):
        self.cpu.mem_write(address, bytes(values))

    def instruction(self, cpu, address, size, _):
        self.instructions += 1
        if address == self.flush:
            assert cpu.reg_read(UC_ARM_REG_R0) == CTX
            cursor = cpu.reg_read(UC_ARM_REG_R1)
            assert RECORD <= cursor < RECORD + CAPACITY and cursor % 4 == 0
            assert self.cpu.mem_read(CTX + 0x24, 1)[0] > 0, '空バッチの流し出し'
            self.events.append(('flush', cpu.reg_read(UC_ARM_REG_R1)))
        if address == 0x2FBB8C:
            self.copies += 1
        assert self.instructions < 500000, '命令数の上限超過'

    def write(self, cpu, access, address, size, value, _):
        # 元の関数の差分キャッシュ以外では、作業体・Picture・記録・スタックだけを許す。
        allowed = ((CTX, CTX + 0x700), (PIC, PIC + 0x158), (MAT, MAT + 0x50),
                   (0x93F000, 0x97C768), (RECORD, RECORD + CAPACITY), (STOP + 0x80000, STACK))
        assert any(start <= address and address + size <= end for start, end in allowed), \
            f'予定外の書き込み: {address:#x}, PC={cpu.reg_read(UC_ARM_REG_PC):#x}'

    def run(self, address, *args):
        self.cpu.reg_write(UC_ARM_REG_SP, STACK)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for register, value in zip(REGS, args):
            self.cpu.reg_write(register, value)
        for index, register in enumerate(SAVED):
            self.cpu.reg_write(register, 0xA0000000 + index)
        self.cpu.emu_start(address, STOP, count=100000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP, '復帰しなかった'
        assert self.cpu.reg_read(UC_ARM_REG_SP) == STACK, 'スタックが壊れた'
        for index, register in enumerate(SAVED):
            assert self.cpu.reg_read(register) == 0xA0000000 + index, '保存レジスタが壊れた'
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def draw(self, streaming, reference=False):
        old = self.cursor
        prefix = bytes(self.cpu.mem_read(RECORD, old - RECORD))
        self.instructions = 0
        if streaming and reference:
            if self.cpu.mem_read(CTX + 0x24, 1)[0]:
                self.cursor = self.run(self.flush, CTX, self.cursor)
            self.put(self.cursor, 0x10000, 0x40080)
            self.cursor += 8
        self.cursor = self.run(self.entry if streaming and not reference else self.picture,
                               PIC, self.cursor, 0, CTX)
        assert old <= self.cursor <= RECORD + CAPACITY, f'不正な戻りカーソル: {self.cursor:#x}'
        assert bytes(self.cpu.mem_read(RECORD, old - RECORD)) == prefix, '既存の記録を上書きした'
        self.guards()
        return self.instructions

    def finish(self):
        old = self.cursor
        prefix = bytes(self.cpu.mem_read(RECORD, old - RECORD))
        if self.cpu.mem_read(CTX + 0x24, 1)[0]:
            self.instructions = 0
            self.cursor = self.run(self.flush, CTX, self.cursor)
        assert old <= self.cursor <= RECORD + CAPACITY
        assert bytes(self.cpu.mem_read(RECORD, old - RECORD)) == prefix
        self.guards()
        assert bytes(self.cpu.mem_read(CTX + 0x24, 3)) == b'\0\0\0'
        data = bytes(self.cpu.mem_read(RECORD, self.cursor - RECORD))
        # PICA 記録の全ヘッダーを歩き、パラメータ数と偶数語境界を検査する。
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        index = 0
        while index < len(words):
            assert index + 1 < len(words), 'ヘッダー欠落'
            count = (words[index + 1] >> 20) & 0x7FF
            index += (count + 3) & ~1
            assert index <= len(words), 'パラメータ数が記録の末尾を越えた'
        return data

    def guards(self):
        assert bytes(self.cpu.mem_read(RECORD - 128, 128)) == bytes([0xA5]) * 128
        assert bytes(self.cpu.mem_read(RECORD + CAPACITY, 128)) == bytes([0xA5]) * 128


def scenario(code, elf, name, operations):
    actual, reference = Machine(code, elf), Machine(code, elf)
    instructions = Counter()
    for streaming, uv, color, texture in operations:
        for machine in (actual, reference):
            machine.put(PIC + 0xD8, uv)
            machine.put(PIC + 0x140, *([color] * 4))
            machine.put(TEX + 4, texture)
        instructions['streaming' if streaming else 'picture'] += actual.draw(streaming)
        reference.draw(streaming, reference=True)
        assert actual.cursor == reference.cursor
        assert actual.cpu.mem_read(CTX, 0x700) == reference.cpu.mem_read(CTX, 0x700)
    data = actual.finish()
    assert data == reference.finish(), '元の ARM 関数で作る参照記録と一致しない'
    assert actual.copies > 0 and actual.events, '元のコピーと流し出しを実行していない'
    return {'scenario': name, 'pictures': len(operations), 'record_bytes': len(data),
            'flushes': len(actual.events), 'native_copies': actual.copies,
            'instructions': dict(instructions), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--code', required=True, type=Path, help='対象版の未変更 code.bin（収録しない）')
    parser.add_argument('--cxx', default='arm-none-eabi-g++')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--plugin-elf', type=Path, help='指定時は製品 ELF 内のコールバックを実行する')
    args = parser.parse_args()
    code = args.code.read_bytes()
    assert hashlib.sha256(code).hexdigest() == CODE_SHA256, '対象版のゲームコードではない'
    output = ROOT / 'build/arm/streaming_picture.elf'
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.cxx, '-std=gnu++20', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-marm',
                    '-mword-relocations', '-fno-rtti', '-fno-exceptions', '-D__3DS__', '-Isrc',
                    '-nostdlib', '-Wl,-Ttext=0x10000000,-e,StreamingEntry',
                    'tests/arm/streaming_picture.cpp', '-o', str(output)], cwd=ROOT, check=True)
    with output.open('rb') as stream:
        binary = ELFFile(stream)
        elf = {'segments': [(s['p_vaddr'], s.data()) for s in binary.iter_segments() if s['p_type'] == 'PT_LOAD'],
               'symbols': {s.name: s['st_value'] for s in binary.get_section_by_name('.symtab').iter_symbols()}}
    if args.plugin_elf:
        with args.plugin_elf.open('rb') as stream:
            plugin = ELFFile(stream)
            segments = [s for s in plugin.iter_segments() if s['p_type'] == 'PT_LOAD']
            elf['plugin_range'] = (min(s['p_vaddr'] for s in segments) & ~0xFFF,
                                   (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 0xFFF) & ~0xFFF)
            elf['segments'] += [(s['p_vaddr'], s.data()) for s in segments]
            entries = [s['st_value'] for s in plugin.get_section_by_name('.symtab').iter_symbols()
                       if 'detail16StreamingPictureE' in s.name]
            assert len(entries) == 1, '製品の帯コールバックが一意に見つからない'
            elf['plugin_entry'] = entries[0]
    plain = (False, 1, 0xFFFFFFFF, 0x21000000)
    band = (True, 1, 0xFFFFFFFF, 0x21001000)
    cases = [('empty', [band]), ('pending', [plain, band, plain]),
             ('same_texture', [plain, (True, 1, 0xFFFFFFFF, 0x21000000), plain])]
    # 未送出の行列・色の個数とUV数の変化、ゲーム自身の満杯時の送出を通す。
    for count in (1, 3, 8, 9, 25, 26):
        for uv in (0, 1, 2, 3):
            for color in (0xFFFFFFFF, 0x80A0C0FF):
                cases.append((f'pending-{count}-uv{uv}-color{color:x}',
                              [(False, 1, color, 0x21000000)] * count +
                              [(True, uv, color, 0x21001000), plain]))
    cases.append(('repeat-300', [(i % 3 == 1, 1, 0xFFFFFFFF if i % 2 else 0x80A0C0FF,
                                  0x21000000 + (i % 10) * 0x800) for i in range(300)]))
    rows = [scenario(code, elf, name, ops) for name, ops in cases]
    report = {'code_sha256': CODE_SHA256, 'product_callback': bool(args.plugin_elf), 'cases': rows}
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'ARM 試験: {len(rows)} 条件、{sum(r["pictures"] for r in rows)} 枚。'
          '元の Picture・流し出し・コピーを実行し、記録の一致・境界・復帰を確認しました。')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, UcError) as error:
        raise SystemExit(f'ARM 試験失敗: {error}') from error
