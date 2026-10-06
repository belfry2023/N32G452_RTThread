"""Generate two RTE-only projects to test installed-pack component resolution.

No C/asm source or include path is added by this script. uVision must obtain
them and the config copies from the actual installed PDSC. Install the pack
before building. Generated projects are disposable and never flashed.
"""
from pathlib import Path
import copy
import xml.etree.ElementTree as ET
from build_pack import VERSION

ROOT = Path(__file__).resolve().parents[1]
for compiler in ('AC5', 'AC6'):
    label = 'RTE_' + compiler + '_LVGL'
    directory = ROOT / 'build/keil-check' / label
    directory.mkdir(parents=True, exist_ok=True)
    root = ET.parse(ROOT / 'keil/N32G452_RTThread.uvprojx').getroot()
    targets = root.find('Targets')
    target = copy.deepcopy(next(t for t in targets if t.findtext('TargetName') == compiler + '_LVGL'))
    targets.clear()
    targets.append(target)
    target.find('TargetName').text = label
    target.find('Groups').clear()
    arm = target.find('TargetOption/TargetArmAds')
    arm.find('Cads/VariousControls/Define').text = ''
    arm.find('Cads/VariousControls/IncludePath').text = ''
    arm.find('LDads/ScatterFile').text = '.\\RTE\\RT-Thread_Full\\N32G452VEL7\\n32g452_rtthread.sct'
    common = target.find('TargetOption/TargetCommonOption')
    common.find('OutputDirectory').text = '.\\Objects\\'
    common.find('ListingPath').text = '.\\Listings\\'
    common.find('RegisterFile').text = ''
    components = ET.SubElement(ET.SubElement(root, 'RTE'), 'components')
    for group in ('Kernel', 'Board', 'GP21', 'GC9307C', 'LVGL', 'Application'):
        item = ET.SubElement(components, 'component', Cvendor='Belfry', Cclass='RT-Thread Full',
                             Cgroup=group, Cversion=VERSION, condition=group)
        ET.SubElement(item, 'package', name='N32G452_RTThread', vendor='Belfry', version=VERSION,
                      url='https://github.com/belfry2023/')
        ET.SubElement(ET.SubElement(item, 'targetInfos'), 'targetInfo', name=label)
    ET.indent(root, space='  ')
    result = directory / (label + '.uvprojx')
    ET.ElementTree(root).write(result, encoding='utf-8', xml_declaration=True)
    print(result)
