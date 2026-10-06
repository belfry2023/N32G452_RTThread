"""Make relocatable build-only copies of two unmodified official SDK examples.

Keep vendor source, device, clock, compiler options and generated-memory-layout
settings. Only rebase paths and select the locally available AC5 version/DFP.
Never download these N32G457/HSE examples onto the user's N32G452/HSI board.
"""
from pathlib import Path
import os
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'Nations.N32G45x_Library.2.6.0'
EXAMPLES = {
    'Official_LedBlink': 'GPIO/LedBlink/MDK-ARM/LedBlink.uvprojx',
    'Official_RT_PIN': 'RT_Thread/RT_Thread8_PIN_DEVICE_REGISTER/MDK-ARM/PIN_DEVICE_REGISTER.uvprojx',
}
for label, template in EXAMPLES.items():
    original = SDK / 'projects/n32g45x_EVAL/examples' / template
    destination = ROOT / 'build/keil-official' / label
    destination.mkdir(parents=True, exist_ok=True)
    tree = ET.parse(original)
    target = tree.find('Targets/Target')

    def set_value(path, text):
        node = target
        for tag in path.split('/'):
            child = node.find(tag)
            if child is None:
                child = ET.SubElement(node, tag)
            node = child
        node.text = text

    def rebase(text):
        path = (original.parent / text.replace('\\', '/')).resolve()
        if not path.is_relative_to(ROOT):
            raise ValueError('Unexpected external SDK path: ' + str(path))
        if not path.exists():
            raise FileNotFoundError(path)
        return os.path.relpath(path, destination).replace('/', '\\')

    for entry in target.findall('.//FilePath'):
        entry.text = rebase(entry.text)
    for entry in target.findall('.//VariousControls/IncludePath'):
        if entry.text:
            entry.text = ';'.join(rebase(p) for p in entry.text.split(';') if p)
    set_value('TargetName', label)
    set_value('pCCUsed', '5060960::V5.06 update 7 (build 960)::ARMCC')
    set_value('uAC6', '0')
    common = 'TargetOption/TargetCommonOption/'
    set_value(common + 'PackID', 'Nationstech.N32G45x_DFP.1.3.0')
    set_value(common + 'OutputDirectory', '.\\Objects\\')
    set_value(common + 'ListingPath', '.\\Listings\\')
    set_value(common + 'AfterMake/RunUserProg1', '0')
    set_value(common + 'AfterMake/RunUserProg2', '0')
    set_value(common + 'AfterMake/UserProg1Name', '')
    set_value(common + 'AfterMake/UserProg2Name', '')
    # Firmware header was removed from DFP in 1.0.2; use the SDK copy.
    set_value(common + 'RegisterFile', os.path.relpath(SDK / 'firmware/CMSIS/device/n32g45x.h', destination))
    ET.indent(tree, space='  ')
    result = destination / (label + '.uvprojx')
    tree.write(result, encoding='utf-8', xml_declaration=True)
    print(result)
