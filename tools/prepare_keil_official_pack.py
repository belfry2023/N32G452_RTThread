"""Create the full RT-Thread workspace project using official RTE components.

CMSIS and SPL headers/sources come exclusively from installed ARM/NSING packs.
Application and BSP files reference the workspace; no installed pack is edited.
RTE startup/system copies are project-local and created only when absent.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'keil/official-pack'
PACK_ROOT = Path(os.environ['LOCALAPPDATA']) / 'Arm/Packs'
DFP = PACK_ROOT / 'NSING/N32G45x_DFP/1.0.7'
CMSIS = PACK_ROOT / 'ARM/CMSIS/5.9.0'


def set_value(root, path, value):
    node = root
    for part in path.split('/'):
        child = node.find(part)
        if child is None:
            child = ET.SubElement(node, part)
        node = child
    node.text = str(value)


def relative(path):
    return os.path.relpath(path, OUT).replace('/', '\\')


def write_xml(path, root):
    ET.indent(root, space='  ')
    ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)


def pin_packs(rte, target_names, selections):
    """Disable automatic latest-pack selection for these targets only."""
    previous = rte.find('packages')
    if previous is not None:
        rte.remove(previous)
    packages = ET.Element('packages')
    rte.insert(0, packages)
    ET.SubElement(ET.SubElement(packages, 'filter'), 'targetInfos')
    for vendor, name, version in selections:
        item = ET.SubElement(packages, 'package', vendor=vendor, name=name,
                             version=version, url='https://www.keil.com/pack/')
        infos = ET.SubElement(item, 'targetInfos')
        for target_name in target_names:
            ET.SubElement(infos, 'targetInfo', name=target_name, versionMatchMode='fixed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--regenerate-project', action='store_true', help='Replace generated uvprojx; retain main and RTE config copies')
    args = parser.parse_args()
    project = OUT / 'N32G452_OfficialPack_RTThread.uvprojx'
    if project.exists() and not args.regenerate_project:
        raise SystemExit('Project exists; use --regenerate-project only if replacing its target settings is intended.')
    for path in (DFP / 'NSING.N32G45x_DFP.pdsc', CMSIS / 'ARM.CMSIS.pdsc',
                 DFP / 'firmware/CMSIS/device/startup/startup_n32g45x.s'):
        if not path.is_file():
            raise SystemExit('Install the required official pack first: ' + str(path))
    OUT.mkdir(parents=True, exist_ok=True)
    config_dir = OUT / 'RTE/Device/N32G452VEL7'
    config_dir.mkdir(parents=True, exist_ok=True)
    for filename, origin in [('startup_n32g45x.s', 'startup/startup_n32g45x.s'),
                             ('system_n32g45x.c', 'system_n32g45x.c')]:
        path = config_dir / filename
        if not path.exists():
            text = (DFP / 'firmware/CMSIS/device' / origin).read_text(encoding='utf-8-sig')
            if filename.startswith('startup'):
                text = re.sub(r'Stack_Size\s+EQU\s+0x[0-9a-fA-F]+', 'Stack_Size      EQU     0x00000800', text)
                text = re.sub(r'Heap_Size\s+EQU\s+0x[0-9a-fA-F]+', 'Heap_Size       EQU     0x00000200', text)
                text = '; Project copy: 2 KiB MSP and 512-byte MicroLIB heap for RT-Thread scatter.\n' + text
            path.write_text(text, encoding='utf-8')
    main_file = OUT / 'App/main.c'
    main_file.parent.mkdir(exist_ok=True)
    if not main_file.exists():
        main_file.write_text('''/**
 * @file main.c
 * @brief 官方 Pack 工程的用户入口，位于 RT-Thread main 线程中。
 * 启动链：官方 Reset_Handler -> SystemInit -> Arm __main -> RT 内核包装 -> 用户 main。
 * 引脚改工作区 inc/board_config.h，采集/界面改工作区 app/app_tasks.c。
 */
#include <rtthread.h>

/**
 * @brief 执行一次性的用户初始化并打印启动信息。
 * @return 0；只结束 main 线程，采集、界面、按键和 LED 线程继续工作。
 * @details 用法：在这里添加自己的启动设置；持续业务放到 app/ 中的线程。
 *          动作：设备和应用 INIT_* 初始化项已自动执行，本函数无需再次调用 app_tasks_init()。
 * @note LVGL 对象只能在界面线程中操作；如果添加循环，应通过延时或 IPC 等待让出 CPU。
 */
int main(void)
{
    rt_kprintf("\\nN32G452 / Official NSING Pack / RT-Thread 3.1.4\\n");
    rt_kprintf("HSI PLL 128 MHz, GP21 reference 5 MHz, GC9307C 240x320\\n");
    return 0;
}
''', encoding='utf-8')

    original = ROOT / 'keil/N32G452_RTThread.uvprojx'
    root = ET.parse(original).getroot()
    rte = root.find('RTE')
    if rte is not None:
        root.remove(rte)
    targets = root.find('Targets')
    target_names = []
    for target in targets:
        name = target.findtext('TargetName')
        target_names.append(name)
        common = 'TargetOption/TargetCommonOption/'
        set_value(target, common + 'Vendor', 'NSING')
        set_value(target, common + 'PackID', 'NSING.N32G45x_DFP.1.0.7')
        set_value(target, common + 'PackURL', 'https://www.keil.com/pack/')
        set_value(target, common + 'RegisterFile', '$$Device:N32G452VEL7$firmware\\CMSIS\\device\\n32g45x.h')
        set_value(target, common + 'OutputDirectory', '.\\Objects\\' + name + '\\')
        set_value(target, common + 'ListingPath', '.\\Listings\\' + name + '\\')
        set_value(target, common + 'CreateHexFile', '1')
        arm = 'TargetOption/TargetArmAds/'
        # Official Startup is ARMASM syntax. Both installed compiler releases
        # include armasm; keep RT context in the same syntax as the startup.
        set_value(target, arm + 'Aads/uClangAs', '0')
        set_value(target, arm + 'Aads/ClangAsOpt', '0')
        set_value(target, arm + 'Aads/VariousControls/MiscControls', '--fpu=SoftVFP')
        includes = target.find(arm + 'Cads/VariousControls/IncludePath')
        includes.text = ';'.join(relative((original.parent / p).resolve()) for p in includes.text.split(';')
                                 if '\\firmware\\' not in p)
        set_value(target, arm + 'LDads/ScatterFile', relative(ROOT / 'pack/keil/n32g452_rtthread.sct'))
        # The DFP declares N32G45x and USE_STDPERIPH_DRIVER. Avoid overriding
        # that declaration with a conflicting macro value; the other BSP
        # macros remain explicit at target scope.
        groups = target.find('Groups')
        for group in list(groups):
            group_name = group.findtext('GroupName')
            if group_name == 'AC6':
                groups.remove(group)
                continue
            if group_name == 'AC5':
                group.find('GroupName').text = 'RT-Thread CPU port'
            files = group.find('Files')
            if files is None:
                continue
            for file in list(files):
                old_path = file.findtext('FilePath')
                filename = file.findtext('FileName')
                if '\\firmware\\' in old_path or filename.startswith('startup_'):
                    files.remove(file)
                    continue
                new_path = main_file if filename == 'rtthread_entry.c' else (original.parent / old_path).resolve()
                file.find('FilePath').text = relative(new_path)
                if filename == 'rtthread_entry.c':
                    file.find('FileName').text = 'main.c'
                if filename == 'context_ac5.s':
                    set_value(file, 'FileOption/CommonProperty/IncludeInBuild', '1')

    rte = ET.SubElement(root, 'RTE')
    pin_packs(rte, target_names, [('ARM', 'CMSIS', '5.9.0'), ('NSING', 'N32G45x_DFP', '1.0.7')])
    components = ET.SubElement(rte, 'components')
    configs = ET.SubElement(rte, 'files')
    dfp_xml = ET.parse(DFP / 'NSING.N32G45x_DFP.pdsc').getroot()
    cmsis_xml = ET.parse(CMSIS / 'ARM.CMSIS.pdsc').getroot()
    core = next(c for c in cmsis_xml.findall('./components/component')
                if c.get('Cgroup') == 'CORE' and c.get('Cversion') == '5.6.0')
    chosen = [(core, 'ARM', 'CMSIS', '5.9.0')]
    required = {'MISC', 'RCC', 'GPIO', 'EXTI', 'SPI', 'TIMER', 'USART', 'FLASH'}
    for c in dfp_xml.findall('./components/component'):
        if c.get('Cgroup') == 'Startup' or (c.get('Cgroup') == 'N32G45x_StdPeripherals' and c.get('Csub') in required):
            chosen.append((c, 'NSING', 'N32G45x_DFP', '1.0.7'))
    for source, vendor, package, version in chosen:
        attrs = dict(source.attrib, Cvendor=vendor)
        component = ET.SubElement(components, 'component', attrs)
        pack_attrs = {'name': package, 'vendor': vendor, 'version': version, 'url': 'https://www.keil.com/pack/'}
        ET.SubElement(component, 'package', pack_attrs)
        infos = ET.SubElement(component, 'targetInfos')
        for name in target_names:
            ET.SubElement(infos, 'targetInfo', name=name, versionMatchMode='fixed')
        for f in source.findall('./files/file'):
            if f.get('attr') != 'config':
                continue
            config = ET.SubElement(configs, 'file', f.attrib)
            ET.SubElement(config, 'instance', index='0').text = 'RTE\\Device\\N32G452VEL7\\' + Path(f.get('name')).name
            ET.SubElement(config, 'component', attrs)
            ET.SubElement(config, 'package', pack_attrs)
            infos = ET.SubElement(config, 'targetInfos')
            for name in target_names:
                ET.SubElement(infos, 'targetInfo', name=name)
    write_xml(project, root)
    manifest = {'project': str(project.relative_to(ROOT)),
                'packs': ['ARM.CMSIS.5.9.0', 'NSING.N32G45x_DFP.1.0.7'],
                'cmsis_core_component': '5.6.0', 'targets': target_names,
                'source_roots': ['app', 'board', 'inc', 'Nations.N32G45x_Library.2.6.0/middlewares/rt-thread', 'third_party/lvgl'],
                'startup_adjustment': 'Project-local official ARMASM startup: Stack_Size=0x800, Heap_Size=0x200',
                'dfp_pdsc_sha256': hashlib.sha256((DFP / 'NSING.N32G45x_DFP.pdsc').read_bytes()).hexdigest()}
    (OUT / 'dependencies.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(project)


if __name__ == '__main__':
    main()
