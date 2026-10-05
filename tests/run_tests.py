#!/usr/bin/env python3
"""Host tests of production backup and SD routines with fault-injecting I/O."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'firmware.c').read_text()
backup = source[source.index('// Use bounded paths'):source.index('\nvoid backup_process()')]
crc = source[source.index('uint16_t gen_crc16('):source.index('\n    return crc;', source.index('uint16_t gen_crc16(')) + len('\n    return crc;\n}')]
spi = (root / 'spi_sd.c').read_text()
# Substitute only the MMIO SPI primitives; retain command/block/write logic.
spi = '#include "sd_test.h"\n' + spi[spi.index('void spi_readblock('):]
with tempfile.TemporaryDirectory(prefix='snestang-firmware-tests-') as build:
    out = Path(build)
    (out / 'backup_under_test.h').write_text(backup + '\n' + crc)
    (out / 'sd_under_test.h').write_text(spi)
    for name in ('backup', 'sd', 'disk'):
        exe = out / name
        subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-I', str(out), '-I', str(root / 'tests'),
                        str(root / 'tests' / f'test_{name}.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
