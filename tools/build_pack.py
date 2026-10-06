"""Build a reproducible local CMSIS-Pack and self-contained MDK example.

No global pack installation, SDK edits, downloads, or recursive deletions.
Run validate_pack.py and official PackChk separately before distributing.
"""
from pathlib import Path, PurePosixPath
import copy
import hashlib
import json
import re
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'Nations.N32G45x_Library.2.6.0'
RT = SDK / 'middlewares/rt-thread'
VERSION = '0.1.2'
PACK_ID = 'Belfry.N32G452_RTThread'
EXAMPLE = 'Examples/N32G452_RTThread'
CLASS = 'RT-Thread Full'
payload = {}
origins = {}
groups = {}


def put(name, data, origin=None):
    assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
    if isinstance(data, str):
        data = data.encode('utf-8')
    if name in payload:
        assert payload[name] == data, name
    payload[name] = data
    if origin:
        origins[name] = str(origin.relative_to(ROOT)).replace('\\', '/')
    return name


def source(path, relative, group=None, transform=None):
    data = path.read_bytes()
    if transform:
        data = transform(data.decode('utf-8-sig')).encode('utf-8')
    name = put(f'{EXAMPLE}/{relative}', data, path)
    if group:
        groups.setdefault(group, []).append(name)
    return name


def tree(path, relative, suffixes, group=None):
    for p in sorted(path.rglob('*')):
        if p.is_file() and p.suffix.lower() in suffixes:
            source(p, f'{relative}/{p.relative_to(path).as_posix()}',
                   group if p.suffix.lower() == '.c' else None)


def xml_bytes(root):
    ET.indent(root, space='  ')
    return b'<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='utf-8') + b'\n'


def element(parent, tag, text=None, **attrs):
    el = ET.SubElement(parent, tag, attrs)
    if text is not None:
        el.text = text
    return el


def populate_sources():
    tree(RT / 'src', 'Sources/RTThread/src', {'.c'}, 'Kernel')
    tree(RT / 'include', 'Sources/RTThread/include', {'.h'})
    tree(RT / 'components/drivers/include', 'Sources/RTThread/drivers/include', {'.h'})
    drivers = ['spi/spi_core.c', 'spi/spi_dev.c', 'serial/serial.c',
               'src/completion.c', 'src/dataqueue.c', 'misc/pin.c', 'misc/rt_drv_pwm.c',
               'hwtimer/hwtimer.c']
    for name in drivers:
        def patch(text):
            needle = '    case HWTIMER_CTRL_MODE_SET:'
            assert text.count(needle) == 1
            return text.replace(needle, '    break; /* Pack patch: INFO_GET must not change mode. */\n' + needle)
        source(RT / 'components/drivers' / name, 'Sources/RTThread/drivers/' + name,
               'Kernel', patch if name.endswith('hwtimer.c') else None)
    for p in sorted((RT / 'components/finsh').glob('*')):
        if p.suffix == '.h' or (p.suffix == '.c' and p.name != 'msh_file.c'):
            source(p, 'Sources/RTThread/finsh/' + p.name, 'Kernel' if p.suffix == '.c' else None)
    source(RT / 'libcpu/cortex-m4/cpuport.c', 'Sources/RTThread/port/cpuport.c', 'Kernel')
    def align_fault_call(text):
        # The vendor handler pushes LR alone before calling C. A fault from
        # PSP can therefore call C with MSP % 8 == 4. Save/restore MSP and
        # align the outgoing call independently of the fault stack pointer r0.
        pattern = r'    PUSH\s+\{LR\}\s*\n    BL\s+rt_hw_hard_fault_exception\s*\n    POP\s+\{LR\}'
        replacement = '''    MOV     r1, sp
    BIC     r2, r1, #7
    MOV     sp, r2
    PUSH    {r1, lr}
    BL      rt_hw_hard_fault_exception
    POP     {r1, lr}
    MOV     sp, r1'''
        text, count = re.subn(pattern, replacement, text, flags=re.IGNORECASE)
        assert count == 1, 'Vendor HardFault handler changed'
        return text
    source(RT / 'libcpu/cortex-m4/context_rvds.S', 'Sources/Keil/context_ac5.s', 'AC5', align_fault_call)
    source(RT / 'libcpu/cortex-m4/context_gcc.S', 'Sources/Keil/context_ac6.S', 'AC6',
           lambda text: align_fault_call(text).replace('.cpu cortex-m4', '.eabi_attribute 24, 1\n.eabi_attribute 25, 1\n.cpu cortex-m4'))

    tree(SDK / 'firmware/CMSIS/core', 'Sources/CMSIS', {'.h'})
    for name in ['n32g45x.h', 'system_n32g45x.h', 'n32g45x_conf.h']:
        source(SDK / 'firmware/CMSIS/device' / name, 'Sources/Device/' + name)
    source(SDK / 'firmware/CMSIS/device/system_n32g45x.c', 'Sources/Device/system_n32g45x.c', 'Board')
    tree(SDK / 'firmware/n32g45x_std_periph_driver/inc', 'Sources/SPL/include', {'.h'})
    for p in sorted((SDK / 'firmware/n32g45x_std_periph_driver/src').glob('*.c')):
        if p.name not in ['n32g45x_eth.c', 'n32g457_eth.c']:
            source(p, 'Sources/SPL/src/' + p.name, 'Board')

    configurations = {'rtconfig.h': ROOT / 'board/rtconfig.h',
                      'board_config.h': ROOT / 'inc/board_config.h',
                      'gc9307c_panel.h': ROOT / 'board/gc9307c_panel.h',
                      'lv_conf.h': ROOT / 'board/lv_conf.h'}
    for name, p in configurations.items():
        source(p, 'Config/' + name)
    for folder in ['board', 'inc']:
        for p in sorted((ROOT / folder).glob('*.h')):
            if p.name not in configurations:
                source(p, 'Sources/Board/include/' + p.name)
    for p in sorted((ROOT / 'board').glob('*.c')):
        if p.name in ['drv_gp21.c', 'gp21_protocol.c']:
            group = 'GP21'
        elif p.name in ['drv_lcd.c', 'drv_lcd_device.c']:
            group = 'GC9307C'
        elif p.name.startswith('lv_port_'):
            group = 'LVGL'
        elif p.name == 'rtthread_entry.c':
            group = 'Application'
        else:
            group = 'Board'
        source(p, 'Sources/Board/' + p.name, group)
    for p in sorted((ROOT / 'app').glob('*.c')):
        source(p, 'Sources/App/' + p.name, 'Application')
    for p in sorted((ROOT / 'pack/keil').glob('*.c')):
        source(p, 'Sources/Keil/' + p.name, 'Kernel' if p.name == 'finsh_vars.c' else 'Board')
    source(ROOT / 'pack/keil/n32g452_rtthread.sct', 'Config/n32g452_rtthread.sct')

    lv = ROOT / 'third_party/lvgl'
    tree(lv / 'src', 'Sources/LVGL/src', {'.h', '.c'}, 'LVGL')
    source(lv / 'lvgl.h', 'Sources/LVGL/lvgl.h')
    source(lv / 'demos/lv_demos.h', 'Sources/LVGL/demos/lv_demos.h')
    for demo in ['keypad_encoder', 'stress']:
        tree(lv / 'demos' / demo, 'Sources/LVGL/demos/' + demo, {'.h', '.c'}, 'LVGL')


def make_startups():
    original = SDK / 'firmware/CMSIS/device/startup/startup_n32g45x.s'
    def ac5(text):
        text = re.sub(r'Stack_Size\s+EQU\s+0x[0-9a-fA-F]+', 'Stack_Size      EQU     0x00000800', text)
        text = re.sub(r'Heap_Size\s+EQU\s+0x[0-9a-fA-F]+', 'Heap_Size       EQU     0x00000200', text)
        return '; Adapted for pack scatter layout: 2 KiB MSP, 512-byte MicroLIB heap.\n' + text
    source(original, 'Sources/Keil/startup_ac5.s', 'AC5', ac5)

    # Keep the exact vendor interrupt list and weak definitions, but enter
    # Arm's __main scatter-loader, not the GCC manual data/BSS copy or entry().
    gcc_path = ROOT / 'board/startup_n32g45x_rtthread.s'
    text = gcc_path.read_text(encoding='utf-8')
    begin = text.index('/* start address for the initialization')
    end = text.index('/**', text.index('.size  Reset_Handler'))
    reset = '''/* Arm Compiler 6 / MicroLIB: __main initializes RW/ZI and invokes
 * RT-Thread's $Sub$$main wrapper. Do not jump straight into user main. */
.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler, %function
.thumb_func
Reset_Handler:
    bl SystemInit
    b __main
.size Reset_Handler, .-Reset_Handler

'''
    text = text[:begin] + reset + text[end:]
    text = text.replace('.fpu softvfp', '')
    text = text.replace('.cpu cortex-m4', '.cpu cortex-m4\n  .eabi_attribute 24, 1\n  .eabi_attribute 25, 1')
    text = text.replace('.isr_vector', 'RESET')
    text = text.replace('g_pfnVectors:', '.global __Vectors\n__Vectors:\ng_pfnVectors:')
    text = text.replace('.word  _estack', '.word  __initial_sp')
    text += '''
/* These sections are placed explicitly by n32g452_rtthread.sct. */
.section STACK,"aw",%nobits
.balign 8
.space 0x800
.global __initial_sp
__initial_sp:
.section HEAP,"aw",%nobits
.balign 8
.global __heap_base
__heap_base:
.space 0x200
.global __heap_limit
__heap_limit:
'''
    name = put(f'{EXAMPLE}/Sources/Keil/startup_ac6.S', text, gcc_path)
    groups['AC6'].append(name)


def make_project():
    template = SDK / 'projects/n32g45x_EVAL/examples/GPIO/LedBlink/MDK-ARM/LedBlink.uvprojx'
    root = ET.parse(template).getroot()
    targets = root.find('Targets')
    base = copy.deepcopy(targets.find('Target'))
    targets.clear()
    includes = ['Config', 'Sources/Board/include', 'Sources/CMSIS', 'Sources/Device',
                'Sources/SPL/include', 'Sources/RTThread/include', 'Sources/RTThread/drivers/include',
                'Sources/RTThread/drivers/include/ipc', 'Sources/RTThread/finsh', 'Sources/LVGL']
    for compiler in ['AC5', 'AC6']:
        for lvgl in [False, True]:
            target = copy.deepcopy(base)
            target_name = compiler + ('_LVGL' if lvgl else '_Headless')
            def set_value(path, value):
                parent = target
                for part in path.split('/'):
                    child = parent.find(part)
                    if child is None:
                        child = ET.SubElement(parent, part)
                    parent = child
                parent.text = value
            set_value('TargetName', target_name)
            set_value('pCCUsed', '5060960::V5.06 update 7 (build 960)::ARMCC' if compiler == 'AC5' else '6240000::V6.24::ARMCLANG')
            if target.find('pArmCC') is None:
                target.insert(list(target).index(target.find('pCCUsed')), ET.Element('pArmCC'))
            set_value('uAC6', '0' if compiler == 'AC5' else '1')
            compiler_selection = target.find('uAC6')
            target.remove(compiler_selection)
            target.insert(list(target).index(target.find('TargetOption')), compiler_selection)
            common = 'TargetOption/TargetCommonOption/'
            for name, value in {
                'Device': 'N32G452VEL7', 'Vendor': 'Nationstech', 'PackID': 'Nationstech.N32G45x_DFP.1.3.0',
                'PackURL': 'https://www.keil.com/pack/',
                'Cpu': 'IRAM(0x20000000,0x24000) IROM(0x08000000,0x80000) CPUTYPE("Cortex-M4") CLOCK(128000000) ELITTLE',
                'RegisterFile': '.\\Sources\\Device\\n32g45x.h',
                'SFDFile': '$$Device:N32G452VEL7$svd\\N32G452.svd',
                'FlashDriverDll': 'UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0N32G45x -FS08000000 -FL080000 -FP0($$Device:N32G452VEL7$Flash\\N32G45x.FLM))',
                'OutputDirectory': f'.\\Objects\\{target_name}\\', 'OutputName': 'n32g452_rtthread',
                'ListingPath': f'.\\Listings\\{target_name}\\', 'SelectedForBatchBuild': '1',
                'AfterMake/RunUserProg1': '0', 'AfterMake/UserProg1Name': '',
            }.items():
                set_value(common + name, value)
            arm = 'TargetOption/TargetArmAds/'
            set_value(arm + 'ArmAdsMisc/useUlib', '1')
            set_value(arm + 'ArmAdsMisc/RvdsVP', '0')
            set_value(arm + 'Cads/uC99', '1')
            set_value(arm + 'Cads/Optim', '3')
            defines = 'N32G45X,N32G452,USE_STDPERIPH_DRIVER,N32_USE_RTTHREAD=1,N32_VARIANT_xE,SYSCLK_SRC=2,SYSCLK_FREQ=128000000,HSE_VALUE=8000000'
            if lvgl:
                defines += ',APP_USE_LVGL=1,LV_CONF_INCLUDE_SIMPLE=1'
            set_value(arm + 'Cads/VariousControls/Define', defines)
            set_value(arm + 'Cads/VariousControls/IncludePath', ';'.join(x.replace('/', '\\') for x in includes))
            set_value(arm + 'Cads/VariousControls/MiscControls', '--c99 --fpu=SoftVFP --no_multibyte_chars' if compiler == 'AC5' else '-std=c99 -mfloat-abi=soft')
            set_value(arm + 'Aads/uClangAs', '0' if compiler == 'AC5' else '1')
            set_value(arm + 'Aads/VariousControls/MiscControls', '--fpu=SoftVFP' if compiler == 'AC5' else '--target=arm-arm-none-eabi -x assembler-with-cpp -mfloat-abi=soft')
            set_value(arm + 'LDads/umfTarg', '0')
            set_value(arm + 'LDads/useFile', '1')
            set_value(arm + 'LDads/ScatterFile', '.\\Config\\n32g452_rtthread.sct')
            set_value(arm + 'LDads/Misc', '--entry=Reset_Handler --keep=*(.rti_fn*) --keep=*(FSymTab) --keep=*(VSymTab) --keep=*(HEAP) --keep=*(STACK)')
            old_groups = target.find('Groups')
            old_groups.clear()
            # uVision merges the group/file topology across all targets. Keep
            # identical ordering and explicitly exclude inactive source files.
            for group in ['Kernel', 'Board', 'AC5', 'AC6', 'GP21', 'GC9307C', 'LVGL', 'Application']:
                enabled = (group not in ['AC5', 'AC6'] or group == compiler) and (group != 'LVGL' or lvgl)
                g = element(old_groups, 'Group')
                element(g, 'GroupName', group)
                files = element(g, 'Files')
                for name in sorted(groups[group]):
                    f = element(files, 'File')
                    element(f, 'FileName', PurePosixPath(name).name)
                    element(f, 'FileType', '1' if name.endswith('.c') else '2')
                    element(f, 'FilePath', name.removeprefix(EXAMPLE + '/').replace('/', '\\'))
                    options = element(f, 'FileOption')
                    common_properties = copy.deepcopy(target.find('TargetOption/CommonProperty'))
                    common_properties.find('IncludeInBuild').text = '1' if enabled else '0'
                    options.append(common_properties)
                    element(options, 'FileArmAds')
            cfg = element(old_groups, 'Group')
            element(cfg, 'GroupName', 'Configuration')
            files = element(cfg, 'Files')
            for name in ['rtconfig.h', 'board_config.h', 'gc9307c_panel.h', 'lv_conf.h', 'n32g452_rtthread.sct']:
                f = element(files, 'File')
                element(f, 'FileName', name)
                element(f, 'FileType', '5')
                element(f, 'FilePath', 'Config\\' + name)
            targets.append(target)
    put(f'{EXAMPLE}/N32G452_RTThread.uvprojx', xml_bytes(root))


def make_pdsc():
    root = ET.Element('package', {'schemaVersion': '1.7.0', 'xmlns:xsi': 'http://www.w3.org/2001/XMLSchema-instance',
                                 'xsi:noNamespaceSchemaLocation': 'PACK.xsd'})
    element(root, 'vendor', 'Belfry')
    element(root, 'name', 'N32G452_RTThread')
    element(root, 'description', 'N32G452VE RT-Thread 3.1.4 full device framework, GP21, GC9307C and LVGL; AC5/AC6 ports.')
    element(root, 'url', 'https://github.com/belfry2023/')
    element(root, 'license', 'LICENSE.txt')
    releases = element(root, 'releases')
    element(releases, 'release', 'Application component removed: the pack now ships only reusable infrastructure (kernel, device frameworks, BSP drivers, device abstractions) plus attr=config templates. User application code and main() belong to the consuming project, matching RealThread.RT-Thread pack practice.', version=VERSION, date='2026-10-06')
    element(releases, 'release', 'AC5 5.06u7 and AC6 6.24 native builds verified. Fix target exclusions, encoding, MicroLIB and stack alignment. Hardware verification pending.', version=VERSION, date='2026-10-05')
    element(releases, 'release', 'Initial local integration pack; superseded by native-build fixes in 0.1.1.', version='0.1.0', date='2026-10-05')
    req = element(element(root, 'requirements'), 'packages')
    element(req, 'package', vendor='Nationstech', name='N32G45x_DFP', version='1.3.0:1.3.0')
    conditions = element(root, 'conditions')
    for compiler in ['AC5', 'AC6']:
        c = element(conditions, 'condition', id=compiler)
        element(c, 'require', Tcompiler='ARMCC', Toptions=compiler)
    base = element(conditions, 'condition', id='Target')
    element(base, 'require', Dvendor='Nationstech:184', Dname='N32G452VEL7')
    element(base, 'accept', condition='AC5')
    element(base, 'accept', condition='AC6')
    # This BSP supplies its own CMSIS headers, startup, system and SPL snapshot.
    for cls, grp in [('Device', 'Startup'), ('Device', 'System_N32G45x'), ('Device', 'StdPeriph Drivers'), ('CMSIS', 'CORE')]:
        element(base, 'deny', Cclass=cls, Cgroup=grp)
    element(base, 'deny', Cclass='RTOS', Cbundle='RT-Thread', Cgroup='kernel')
    dependencies = {'Kernel': [], 'Board': ['Kernel'], 'GP21': ['Board'],
                    'GC9307C': ['Board'], 'LVGL': ['GC9307C']}
    for group, deps in dependencies.items():
        c = element(conditions, 'condition', id=group)
        element(c, 'require', condition='Target')
        for dep in deps:
            element(c, 'require', Cvendor='Belfry', Cclass=CLASS, Cgroup=dep)
    components = element(root, 'components')
    descriptions = {
        'Kernel': 'RT-Thread 3.1.4 kernel, device frameworks, IPC, software timers and full FINSH/msh.',
        'Board': 'N32G452VE HSI 128MHz BSP: PIN, USART1, PWM, TIM6, SPI3, keys, startup and scatter.',
        'GP21': 'RT sensor device tdc0: GP21 SPI, 5MHz reference, IRQ, FIFO, timeout and diagnostics.',
        'GC9307C': 'RT graphic device lcd0: GC9307C GPIO 8080 16-bit, 240x320 RGB565.',
        'LVGL': 'LVGL 8.3.11 display and button input ports, keypad and stress demos.'}
    for group in dependencies:
        c = element(components, 'component', Cclass=CLASS, Cgroup=group, Cversion=VERSION, condition=group)
        element(c, 'description', descriptions[group])
        element(c, 'RTE_Components_h', '#define RTE_BELFRY_' + group.upper() + ' 1')
        if group == 'Board':
            defaults = {'N32G45X': '1', 'N32G452': '1', 'USE_STDPERIPH_DRIVER': '1',
                        'N32_USE_RTTHREAD': '1', 'N32_VARIANT_xE': '1', 'SYSCLK_SRC': '2',
                        'SYSCLK_FREQ': '128000000', 'HSE_VALUE': '8000000'}
            element(c, 'Pre_Include_Global_h', ''.join(f'#ifndef {key}\n#define {key} {value}\n#endif\n' for key, value in defaults.items()))
        if group == 'LVGL':
            element(c, 'Pre_Include_Global_h', '#define APP_USE_LVGL 1\n#define LV_CONF_INCLUDE_SIMPLE 1\n')
        files = element(c, 'files')
        element(files, 'file', category='doc', name='Documentation/KEIL_PACK.md')
        for name in sorted(groups[group]):
            element(files, 'file', category='sourceC', name=name)
        if group == 'Kernel':
            for compiler in ['AC5', 'AC6']:
                name = next(n for n in groups[compiler] if '/context_' in n)
                element(files, 'file', category='sourceAsm', name=name, condition=compiler)
            dirs = ['Sources/RTThread/include', 'Sources/RTThread/drivers/include',
                    'Sources/RTThread/drivers/include/ipc', 'Sources/RTThread/finsh']
            configs = ['rtconfig.h']
        elif group == 'Board':
            for compiler in ['AC5', 'AC6']:
                name = next(n for n in groups[compiler] if '/startup_' in n)
                element(files, 'file', category='sourceAsm', name=name, condition=compiler)
            element(files, 'file', category='linkerScript', name=f'{EXAMPLE}/Config/n32g452_rtthread.sct', attr='config', version=VERSION)
            dirs = ['Sources/Board/include', 'Sources/CMSIS', 'Sources/Device', 'Sources/SPL/include']
            configs = ['board_config.h']
        elif group == 'GC9307C':
            dirs, configs = [], ['gc9307c_panel.h']
        elif group == 'LVGL':
            dirs, configs = ['Sources/LVGL'], ['lv_conf.h']
        else:
            dirs, configs = [], []
        for directory in dirs:
            element(files, 'file', category='include', name=f'{EXAMPLE}/{directory}/')
        for name in configs:
            element(files, 'file', category='header', name=f'{EXAMPLE}/Config/{name}', attr='config', version=VERSION)
    example = element(element(root, 'examples'), 'example', name='N32G452 RT-Thread GP21 LCD',
                      folder=EXAMPLE, doc='README.md', version=VERSION)
    element(example, 'description', 'Self-contained MDK project: AC5/AC6 x Headless/LVGL, configurable pins. No hardware validation yet.')
    element(element(example, 'project'), 'environment', name='uv', folder='.', load='N32G452_RTThread.uvprojx')
    attrs = element(example, 'attributes')
    # 示例工程只用基础设施组件; 应用代码不属于任何组件
    element(attrs, 'component', Cclass=CLASS, Cgroup='Kernel')
    element(attrs, 'keyword', 'RT-Thread')
    element(attrs, 'keyword', 'N32G452VEL7')
    put(PACK_ID + '.pdsc', xml_bytes(root))


def make_workspace_project():
    """Direct workspace project: share editable sources, only materialize ports."""
    directory = ROOT / 'keil'
    directory.mkdir(exist_ok=True)
    root = ET.fromstring(payload[f'{EXAMPLE}/N32G452_RTThread.uvprojx'])
    import os

    def rel(path):
        return os.path.relpath(path, directory).replace('/', '\\')

    includes = [ROOT / 'board', ROOT / 'inc', SDK / 'firmware/CMSIS/core',
                SDK / 'firmware/CMSIS/device', SDK / 'firmware/n32g45x_std_periph_driver/inc',
                RT / 'include', RT / 'components/drivers/include',
                RT / 'components/drivers/include/ipc', RT / 'components/finsh', ROOT / 'third_party/lvgl']
    generated = {}
    for target in root.findall('Targets/Target'):
        label = target.findtext('TargetName')
        common = target.find('TargetOption/TargetCommonOption')
        common.find('RegisterFile').text = rel(SDK / 'firmware/CMSIS/device/n32g45x.h')
        common.find('OutputDirectory').text = rel(ROOT / 'build/keil' / label / 'Objects') + '\\'
        common.find('ListingPath').text = rel(ROOT / 'build/keil' / label / 'Listings') + '\\'
        arm = target.find('TargetOption/TargetArmAds')
        arm.find('Cads/VariousControls/IncludePath').text = ';'.join(rel(p) for p in includes)
        arm.find('LDads/ScatterFile').text = rel(ROOT / 'pack/keil/n32g452_rtthread.sct')
        for item in target.findall('Groups/Group/Files/File/FilePath'):
            name = f'{EXAMPLE}/' + item.text.replace('\\', '/')
            original = ROOT / origins[name]
            if original.read_bytes() == payload[name]:
                item.text = rel(original)
            else:
                destination = directory / 'generated' / PurePosixPath(name).name
                check_data = generated.setdefault(destination, payload[name])
                assert check_data == payload[name], 'Generated basename collision'
                destination.parent.mkdir(exist_ok=True)
                destination.write_bytes(payload[name])
                item.text = rel(destination)
    (directory / 'N32G452_RTThread.uvprojx').write_bytes(xml_bytes(root))


def normalize_device_pack(output):
    """Correct archive/PDSC filenames only; preserve every vendor payload byte."""
    original = ROOT / 'zip/Nations.N32G45x_DFP.1.3.0.pack'
    destination = output / 'Nationstech.N32G45x_DFP.1.3.0.pack'
    with zipfile.ZipFile(original) as src, zipfile.ZipFile(destination, 'w') as dst:
        for name in sorted(src.namelist()):
            renamed = 'Nationstech.N32G45x_DFP.pdsc' if name == 'Nations.N32G45x_DFP.pdsc' else name
            info = zipfile.ZipInfo(renamed, date_time=(2026, 10, 5, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            dst.writestr(info, src.read(name))
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix('.pack.sha256').write_text(f'{digest}  {destination.name}\n', encoding='ascii')
    print('Device dependency (filenames normalized only):', destination.name)


def main():
    populate_sources()
    make_startups()
    make_project()
    make_pdsc()
    make_workspace_project()
    for name in ['KEIL_PACK.md', 'KEIL_PACK_VALIDATION.md', 'BRINGUP_GP21.md', 'SOURCES.md']:
        put('Documentation/' + name, (ROOT / 'docs' / name).read_bytes())
    put(f'{EXAMPLE}/README.md', (ROOT / 'docs/KEIL_PACK.md').read_bytes())
    put('Licenses/RT-Thread-Apache-2.0.txt', (ROOT / 'pack/licenses/RT-Thread-Apache-2.0.txt').read_bytes())
    put('Licenses/LVGL-MIT.txt', (ROOT / 'third_party/lvgl/LICENCE.txt').read_bytes())
    put('LICENSE.txt', (ROOT / 'pack/LICENSE.txt').read_bytes())
    manifest = {name: {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                       'source': origins.get(name, 'generated/pack documentation')}
                for name, data in sorted(payload.items())}
    put('MANIFEST.json', json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    stage = ROOT / 'build/pack-stage'
    for name, data in payload.items():
        p = stage / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    output = ROOT / 'dist'
    output.mkdir(exist_ok=True)
    archive = output / f'{PACK_ID}.{VERSION}.pack'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 5, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.pack.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    (output / (PACK_ID + '.pdsc')).write_bytes(payload[PACK_ID + '.pdsc'])
    print(f'Created {archive.name}: {len(payload)} files, {archive.stat().st_size} bytes')
    print('Stage:', stage)
    print('SHA256:', digest)
    normalize_device_pack(output)


if __name__ == '__main__':
    main()
