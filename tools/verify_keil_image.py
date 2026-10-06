"""Check final Arm ELF memory, vectors and RT registration, without dependencies."""
from pathlib import Path
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ('AC5_Headless', 'AC5_LVGL', 'AC6_Headless', 'AC6_LVGL')


def verify(path):
    data = path.read_bytes()
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    assert h[0][:7] == b'\x7fELF\x01\x01\x01' and h[2] == 40, 'Not ELF32 little-endian ARM'
    sh = [struct.unpack_from('<IIIIIIIIII', data, h[6] + i * h[11]) for i in range(h[12])]
    strings = data[sh[h[13]][4]:sh[h[13]][4] + sh[h[13]][5]]
    def string(buf, offset):
        return buf[offset:buf.index(b'\0', offset)].decode('ascii')
    sections = {string(strings, s[0]): s for s in sh}
    symbols = {}
    for s in sh:
        if s[1] != 2:
            continue
        st = sh[s[6]]
        names = data[st[4]:st[4]+st[5]]
        for offset in range(s[4], s[4]+s[5], s[9]):
            n, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
            if n:
                symbols[string(names, n)] = (value, size, index)
    def value(name):
        assert name in symbols, 'Missing symbol: ' + name
        return symbols[name][0]
    def section(name, base, size=None, nobits=False):
        s = sections[name]
        assert s[3] == base and (size is None or s[5] == size), name + ' address/size'
        assert (s[1] == 8) == nobits, name + ' loading type'
        return s
    vector = section('ER_VECTORS', 0x08000000, 408)
    words = struct.unpack_from('<102I', data, vector[4])
    assert words[0] == value('__initial_sp') == 0x20012000, 'Initial MSP'
    assert words[1] == (value('Reset_Handler') | 1) == h[4], 'Reset vector/ELF entry'
    for word in words[1:]:
        assert word == 0 or (word & 1 and 0x08000000 <= word < 0x08080000), 'Invalid/ARM-state IRQ vector'
    for index, handler in {3:'HardFault_Handler',14:'PendSV_Handler',15:'SysTick_Handler',
                           53:'USART1_IRQHandler',56:'EXTI15_10_IRQHandler',70:'TIM6_IRQHandler'}.items():
        assert words[index] == value(handler) | 1, handler + ' vector binding'
    section('RW_LIB_HEAP', 0x20011600, 512, True)
    section('RW_MSP', 0x20011800, 2048, True)
    section('RT_HEAP', 0x20012000, 73728, True)
    assert value('Image$$RT_HEAP$$ZI$$Base') == 0x20012000
    assert value('Image$$RT_HEAP$$ZI$$Limit') == 0x20024000
    # armlink emits separate PROGBITS/ZI sections with the SAME region name;
    # compressed RW file size is not the initialized execution size.
    static = [s for s in sh if string(strings, s[0]) == 'RW_IRAM1']
    static_end = max(s[3]+s[5] for s in static)
    assert min(s[3] for s in static) == 0x20000000 and static_end <= 0x20011600, 'Static RAM overlaps heap/MSP'
    for s in sh:
        name = string(strings, s[0])
        if s[2] & 2 and s[5]:
            assert ((0x08000000 <= s[3] < s[3]+s[5] <= 0x08080000)
                    or (0x20000000 <= s[3] < s[3]+s[5] <= 0x20024000)), 'Allocated section out of physical memory: ' + name
    init = sections['ER_RT_INIT']
    descriptors = sorted(v[0] for n,v in symbols.items() if n.startswith('__rt_init_desc_'))
    assert descriptors == list(range(init[3], init[3]+init[5], 8)), 'Init table not contiguous 8-byte descriptors'
    sentinels = [value('__rt_init_desc_' + n) for n in ('rti_start','rti_board_start','rti_board_end','rti_end')]
    assert sentinels == sorted(sentinels) and len(set(sentinels)) == 4, 'Init order'
    for name in ('rt_hw_pin_init','register_timer','drv_pwm_init','rt_hw_spi3_init','rt_hw_usart_init'):
        assert sentinels[1] < value('__rt_init_desc_' + name) < sentinels[2], 'Board init outside board range'
    for name in ('gp21_register','lcd_register','finsh_system_init','app_tasks_init'):
        assert sentinels[2] < value('__rt_init_desc_' + name) < sentinels[3], 'Device/application init range'
    for prefix in ('FSymTab', 'VSymTab'):
        assert 0x08000000 <= value(prefix+'$$Base') < value(prefix+'$$Limit') <= 0x08080000, 'Missing FINSH table'
    # Presence of Arm runtime and wrapper proves the expected main startup chain.
    # armlink resolves $Sub$$main to main; the original is $Super$$main.
    for name in ('__main', '$Super$$main', 'rtthread_startup', 'main'):
        value(name)
    return {'image':str(path.relative_to(ROOT)), 'vector_entries':102, 'rt_init_entries':len(descriptors),
            'static_ram_region':static_end-0x20000000, 'microlib_heap':512, 'msp':2048, 'rt_heap':73728,
            'result':'PASS'}


def main():
    pack_example = '--pack-example' in sys.argv
    reports = []
    for target in TARGETS:
        path = ROOT / ('build/pack-stage/Examples/N32G452_RTThread/Objects' if pack_example else 'build/keil') / target
        if not pack_example:
            path /= 'Objects'
        reports.append(verify(path / 'n32g452_rtthread.axf'))
        print('PASS:', target, 'vectors, Arm startup, scatter RAM bounds, RT init and FINSH tables')
    destination = ROOT / 'build/keil-check' / ('pack-example' if pack_example else '')
    (destination / 'image-validation.json').write_text(json.dumps(reports, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
