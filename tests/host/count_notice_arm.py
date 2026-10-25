"""通知の ARM 命令数を数え、仮定のサイクル数から時間の幅を見積もる。実機の測定ではない。"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import struct
import subprocess


def main():
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK
    from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0,
                                  UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_CPSR)

    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=root)
    parser.add_argument('--name', default='after')
    parser.add_argument('--out', type=Path, required=True, help='計測 JSON（作業ツリー外）')
    args = parser.parse_args()
    if Path(args.name).name != args.name or args.name in ('.', '..'):
        parser.error('--name は一つのフォルダ名にしてください')
    tree = args.tree.resolve()
    target = root / 'build/bench-notice-arm' / args.name
    target.mkdir(parents=True, exist_ok=True)
    executable = target / 'notice.elf'
    compiler = Path(os.environ['DEVKITARM']) / 'bin/arm-none-eabi-g++.exe'
    sources = [root / 'tests/host/bench_notice_arm.cpp', tree / 'src/core/utf.cpp',
               *[tree / f'src/menu/{name}.cpp' for name in ('surface', 'notices')],
               *[tree / f'src/ui/renderer/{name}.cpp' for name in
                 ('clip', 'font_resource', 'image', 'picture', 'slots', 'text')]]
    flags = ['-std=c++20', '-Os', '-march=armv6k', '-mtune=mpcore', '-marm', '-mfloat-abi=hard',
             '-fno-rtti', '-fno-exceptions', '-ffunction-sections', '-fdata-sections',
             '-fno-threadsafe-statics', '-nostartfiles', '-specs=nosys.specs',
             '-Wl,--gc-sections,-Ttext=0x10000000,-e,BenchReset,-u,BenchDraw']
    subprocess.run([str(compiler), *flags, f'-I{tree / "src"}', f'-I{tree / "build/gen"}',
                    *map(str, sources), '-o', str(executable)], check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000000, 0x2000000)
    uc.mem_map(0x20000000, 0x100000)
    uc.mem_map(0x30000000, 0x1000)
    with executable.open('rb') as file:
        elf = ELFFile(file)
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                uc.mem_write(segment['p_vaddr'], segment.data())
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.reg_write(UC_ARM_REG_SP, 0x200FF000)
    decoders = {False: Cs(CS_ARCH_ARM, CS_MODE_ARM), True: Cs(CS_ARCH_ARM, CS_MODE_THUMB)}
    for decoder in decoders.values():
        decoder.detail = True
    cache = {}
    counts = Counter()
    # 外した後に付け直すと、翻訳済みの塊から呼ばれなくなる。フックは付けたままにして数える区間だけ切り替える。
    counting = False

    def block(uc, address, size, _):
        if not counting:
            return
        thumb = bool(uc.reg_read(UC_ARM_REG_CPSR) & 0x20)
        key = (address, size, thumb)
        if key not in cache:
            value = Counter()
            for instruction in decoders[thumb].disasm(bytes(uc.mem_read(address, size)), address):
                value['instructions'] += 1
                mnemonic = instruction.mnemonic
                if mnemonic.startswith('vdiv'):
                    value['divide'] += 1
                elif mnemonic.startswith(('vadd', 'vsub', 'vmul', 'vmla', 'vmls', 'vcmp', 'vcvt', 'vneg', 'vabs', 'vsqrt')):
                    value['fp'] += 1
                elif mnemonic.startswith(('ldr', 'str', 'ldm', 'stm', 'vldr', 'vstr', 'vldm', 'vstm', 'push', 'pop')):
                    value['memory'] += 1
                elif any(instruction.group(group) for group in (CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET)):
                    value['branch'] += 1
            cache[key] = value
        counts.update(cache[key])

    uc.hook_add(UC_HOOK_BLOCK, block)

    def call(name, arg):
        uc.reg_write(UC_ARM_REG_LR, 0x30000000)
        uc.reg_write(UC_ARM_REG_R0, arg)
        uc.emu_start(symbols[name], 0x30000000, count=100_000_000)
        if uc.reg_read(UC_ARM_REG_SP) != 0x200FF000:
            raise RuntimeError('入口が復帰しませんでした')

    rows = []
    # 33 ms 刻みで実際の全更新を通し、代表フレームだけ命令を数える。
    for name, burst, stream, last in [('zero', 0, 0, 1), ('one', 1, 0, 36), ('seven', 7, 0, 80),
                                      ('sixteen', 16, 0, 36), ('stream', 0, 40, 242)]:
        call('BenchReset', burst)
        injected, next_at = 0, 100066
        samples = {0, 12, 18, 36, last}
        if stream:
            samples = {12, 36, 120, 240, 242}
        for frame in range(last + 1):
            now = 100000 + frame * 33
            while injected < stream and now >= next_at:
                call('BenchAdd', now)
                injected += 1
                next_at += 220
            counts.clear()
            counting = frame in samples
            try:
                call('BenchDraw', now)
            finally:
                counting = False
            if frame in samples:
                row = {'scenario': name, 'frame': frame, **counts}
                row['commands'] = struct.unpack('<I', uc.mem_read(symbols['bench_commands'], 4))[0]
                row['texts'] = struct.unpack('<I', uc.mem_read(symbols['bench_texts'], 4))[0]
                for label, ordinary, memory, branch, fp, divide in (
                        ('optimistic', 1, 1, 1, 4, 28), ('conservative', 2, 3, 3, 8, 36)):
                    cycles = counts['instructions'] * ordinary
                    for key, weight in [('memory', memory), ('branch', branch), ('fp', fp), ('divide', divide)]:
                        cycles += counts[key] * (weight - ordinary)
                    row[label + '_us_268mhz'] = cycles / 268
                rows.append(row)
                print(json.dumps(row), flush=True)
    args.out.write_text(json.dumps({'flags': flags, 'rows': rows}, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
