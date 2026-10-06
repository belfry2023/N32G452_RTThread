"""Audit the delivered ZIP, dependency choices, portable example and ARM vectors.

Only Python's standard library is required. This is NOT an Arm compiler/linker
test. Run official PackChk and native Keil builds as separate verification steps.
"""
from pathlib import Path, PurePosixPath
from fnmatch import fnmatchcase
import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from build_pack import VERSION

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / f'dist/Belfry.N32G452_RTThread.{VERSION}.pack'
EXAMPLE = 'Examples/N32G452_RTThread/'
passed = []


def check(ok, message):
    if not ok:
        raise AssertionError(message)


def milestone(message):
    passed.append(message)
    print('PASS:', message)


def relative(name):
    path = PurePosixPath(name.replace('\\', '/'))
    check(not path.is_absolute() and '..' not in path.parts and ':' not in str(path),
          'Nonportable/unsafe path: ' + name)
    return path.as_posix()


def run():
    digest = hashlib.sha256(PACK.read_bytes()).hexdigest()
    check(PACK.with_suffix('.pack.sha256').read_text().split()[0] == digest, 'Archive SHA256')
    with zipfile.ZipFile(PACK) as archive:
        check(archive.testzip() is None, 'ZIP CRC failure')
        entries = archive.namelist()
        check(len({n.casefold() for n in entries}) == len(entries), 'Duplicate Windows paths')
        for name in entries:
            relative(name)
        files = {name: archive.read(name) for name in entries}
    manifest = json.loads(files['MANIFEST.json'])
    check(set(manifest) == set(files) - {'MANIFEST.json'}, 'Manifest file coverage')
    for name, record in manifest.items():
        check(hashlib.sha256(files[name]).hexdigest() == record['sha256'], 'Content hash: ' + name)
        check(len(files[name]) == record['bytes'], 'Content length: ' + name)
    milestone(f'ZIP integrity, safe paths and full manifest: {len(files)} files')

    pdsc = ET.fromstring(files['Belfry.N32G452_RTThread.pdsc'])
    components = {c.get('Cgroup'): c for c in pdsc.findall('components/component')}
    conditions = {c.get('id'): c for c in pdsc.findall('conditions/condition')}
    includes = [n.get('name') for n in pdsc.findall('.//files/file[@category="include"]')]
    config_names = set()
    for item in pdsc.findall('.//files/file'):
        name = relative(item.get('name'))
        if item.get('category') == 'include':
            check(any(n.startswith(name + '/') for n in files), 'Missing include directory: ' + name)
        else:
            check(name in files, 'Missing PDSC file: ' + name)
        if item.get('attr') == 'config':
            config_names.add(PurePosixPath(name).name)
            check(not any(name.startswith(d) for d in includes), 'Shadowed RTE config: ' + name)
    for name in files:
        if name.startswith(EXAMPLE + 'Sources/'):
            check(PurePosixPath(name).name not in config_names, 'Duplicate private configuration: ' + name)
    milestone('Every PDSC file/include exists; configuration headers cannot shadow RTE copies')

    def component_identity(group):
        return {'Cvendor': 'Belfry', 'Cclass': 'RT-Thread Full', 'Cgroup': group}

    def condition_ok(name, compiler, selected, device='N32G452VEL7', external=()):
        environment = {'Tcompiler': 'ARMCC', 'Toptions': compiler,
                       'Dvendor': 'Nationstech:184', 'Dname': device}
        identities = [component_identity(g) for g in selected] + list(external)

        def match(rule):
            if rule.get('condition'):
                return condition_ok(rule.get('condition'), compiler, selected, device, external)
            if any(k.startswith('C') for k in rule.attrib):
                return any(all(fnmatchcase(c.get(k, ''), v) for k, v in rule.attrib.items()) for c in identities)
            return all(fnmatchcase(environment.get(k, ''), v) for k, v in rule.attrib.items())

        condition = conditions[name]
        return (all(match(n) for n in condition.findall('require'))
                and (not condition.findall('accept') or any(match(n) for n in condition.findall('accept')))
                and not any(match(n) for n in condition.findall('deny')))

    # Simulate dependency resolution using the actual PDSC, not a parallel list.
    def resolve(initial):
        selected = set(initial)
        for _ in range(len(components) + 1):
            old = set(selected)
            for group in old:
                for requirement in conditions[components[group].get('condition')].findall('require'):
                    if requirement.get('Cvendor') == 'Belfry':
                        selected.add(requirement.get('Cgroup'))
            if old == selected:
                return selected
        raise AssertionError('Dependency cycle/resolution failure')

    # Application 已从组件中移除（应用代码归使用方工程），
    # 改为用与旧闭包等价的起点集：GP21 + GC9307C + LVGL 覆盖全部 5 个组件。
    full = resolve({'GP21', 'GC9307C', 'LVGL'})
    check(full == set(components), 'GP21+GC9307C+LVGL dependency closure')
    for compiler in ('AC5', 'AC6'):
        for group in full:
            check(condition_ok(components[group].get('condition'), compiler, full), 'Unavailable component: ' + group)
        check(not condition_ok('Target', compiler, full, 'N32G455VEL7'), 'Wrong MCU accepted')
        for conflict in ({'Cclass': 'RTOS', 'Cbundle': 'RT-Thread', 'Cgroup': 'kernel'},
                         {'Cclass': 'Device', 'Cgroup': 'Startup'},
                         {'Cclass': 'CMSIS', 'Cgroup': 'CORE'}):
            check(not condition_ok('Target', compiler, full, external=(conflict,)), 'Conflict accepted')
    check(not condition_ok('Target', 'GCC', full), 'Unsupported compiler accepted')
    check(not condition_ok('LVGL', 'AC6', {'LVGL'}), 'Missing LCD dependency accepted')
    milestone('AC5/AC6 dependency closure; wrong MCU/compiler, missing dependencies and duplicate kernels rejected')

    project = ET.fromstring(files[EXAMPLE + 'N32G452_RTThread.uvprojx'])
    targets = project.findall('Targets/Target')
    check({t.findtext('TargetName') for t in targets} ==
          {'AC5_Headless', 'AC5_LVGL', 'AC6_Headless', 'AC6_LVGL'}, 'Example target matrix')
    # 示例工程自带的应用源码【不属于任何组件】——
    # pack 只提供可复用基础设施，应用代码与 main() 归使用方工程所有。
    # 这对照 RealThread.RT-Thread 官方包（其中 main.c / app_* 出现 0 次）。
    example_only = {n for n in files
                    if n.startswith(EXAMPLE + 'Sources/App/') and n.endswith('.c')}
    example_only.add(EXAMPLE + 'Sources/Board/rtthread_entry.c')
    check(example_only and all(n in files for n in example_only),
          'Example-only application sources missing')
    counts = {}
    topology = None
    for target in targets:
        name = target.findtext('TargetName')
        compiler = name.split('_')[0]
        current_topology = [(g.findtext('GroupName'), [f.findtext('FilePath') for f in g.findall('Files/File')])
                            for g in target.findall('Groups/Group')]
        if topology is None:
            topology = current_topology
        check(topology == current_topology, 'uVision merges inconsistent cross-target file/group topology')
        selected = resolve({'GP21', 'GC9307C', 'LVGL'} if name.endswith('_LVGL')
                           else {'GP21', 'GC9307C'})
        expected = set()
        for group in selected:
            for entry in components[group].findall('files/file'):
                if entry.get('category') in ('sourceC', 'sourceAsm'):
                    if not entry.get('condition') or condition_ok(entry.get('condition'), compiler, selected):
                        expected.add(entry.get('name'))
        actual = []
        for entry in target.findall('Groups/Group/Files/File'):
            path = EXAMPLE + relative(entry.findtext('FilePath'))
            check(path in files, name + ': missing example file: ' + path)
            if entry.findtext('FileType') in ('1', '2') and entry.findtext('FileOption/CommonProperty/IncludeInBuild', '1') != '0':
                actual.append(path)
        check(len(actual) == len(set(actual)), name + ': duplicate sources')
        check(set(actual) == expected | example_only, name + ': RTE/example source graph differs')
        check(sum('/startup_' in p for p in actual) == 1, name + ': startup count')
        check(sum('/context_' in p for p in actual) == 1, name + ': context port count')
        check(not any(p.endswith('/syscalls.c') for p in actual), name + ': GCC retarget accidentally included')
        arm = target.find('TargetOption/TargetArmAds')
        for directory in arm.findtext('Cads/VariousControls/IncludePath').split(';'):
            prefix = EXAMPLE + relative(directory) + '/'
            check(any(f.startswith(prefix) for f in files), name + ': missing include')
        check(target.findtext('uAC6') == ('1' if compiler == 'AC6' else '0'), name + ': compiler selector')
        check(arm.findtext('ArmAdsMisc/useUlib') == '1', name + ': MicroLIB disabled')
        check(arm.findtext('Aads/uClangAs') == ('1' if compiler == 'AC6' else '0'), name + ': assembler selector')
        scatter = EXAMPLE + relative(arm.findtext('LDads/ScatterFile'))
        check(scatter in files and arm.findtext('LDads/umfTarg') == '0', name + ': scatter not used')
        for section in ('.rti_fn*', 'FSymTab', 'VSymTab', 'HEAP', 'STACK'):
            check('--keep=*(' + section + ')' in arm.findtext('LDads/Misc'), name + ': init/shell table not retained')
        for entry in target.findall('.//*'):
            if entry.text:
                check(str(ROOT).casefold() not in entry.text.casefold(), name + ': workspace path leaked')
        counts[name] = len(actual)
    milestone('Four self-contained examples match RTE source selection: ' + str(counts))

    vendor = (ROOT / 'Nations.N32G45x_Library.2.6.0/firmware/CMSIS/device/startup/startup_n32g45x.s').read_text(encoding='utf-8-sig')
    ac5 = files[EXAMPLE + 'Sources/Keil/startup_ac5.s'].decode()
    ac6 = files[EXAMPLE + 'Sources/Keil/startup_ac6.S'].decode()
    def arm_vectors(text):
        return re.findall(r'\bDCD\s+(\w+)', text)
    vectors = re.findall(r'^\s*\.word\s+(\w+)', ac6, re.M)
    check(len(vectors) == 102 and vectors == arm_vectors(vendor) == arm_vectors(ac5), 'Vendor vector table mismatch')
    check(vectors[14:16] == ['PendSV_Handler', 'SysTick_Handler'], 'Core interrupt positions')
    check('b __main' in ac6 and '=__main' in ac5.replace(' ', ''), 'Arm C library startup entry')
    check('__libc_init_array' not in ac6 and '_sidata' not in ac6, 'GCC startup remains in AC6')
    milestone('Both 102-entry interrupt tables match vendor; startup enters Arm __main')

    canonical = ROOT / 'dist/Nationstech.N32G45x_DFP.1.3.0.pack'
    with zipfile.ZipFile(ROOT / 'zip/Nations.N32G45x_DFP.1.3.0.pack') as old, zipfile.ZipFile(canonical) as new:
        old_files = {n.replace('Nations.N32G45x_DFP.pdsc', 'Nationstech.N32G45x_DFP.pdsc'): old.read(n) for n in old.namelist()}
        new_files = {n: new.read(n) for n in new.namelist()}
        check(old_files == new_files, 'Device pack payload changed')
        dependency = ET.fromstring(new_files['Nationstech.N32G45x_DFP.pdsc'])
        req = pdsc.find('requirements/packages/package')
        check(req.get('vendor') == dependency.findtext('vendor') and req.get('name') == dependency.findtext('name'), 'DFP identity mismatch')
        check(canonical.name.startswith(dependency.findtext('vendor') + '.'), 'DFP filename mismatch')
        for path in ['Flash/N32G45x.FLM', 'svd/N32G452.svd']:
            check(path in new_files, 'Missing vendor debug resource: ' + path)
    milestone('Canonical device dependency preserves all vendor bytes, with valid Flash/SVD resources')

    installation = ROOT / f'build/pack-install/Belfry/N32G452_RTThread/{VERSION}'
    if installation.is_dir():
        for name, content in files.items():
            path = installation / name
            check(path.is_file() and path.read_bytes() == content, 'Installed payload stale/missing: ' + name)
        milestone('Isolated cpackget installation matches delivered archive byte for byte')
    else:
        print('SKIP: isolated installation not present; use cpackget separately')
    report = {'archive': PACK.name, 'sha256': digest, 'files': len(files), 'checks': passed,
              'native_arm_build': 'See build/keil-check/results.json and image-validation.json', 'hardware': 'NOT RUN'}
    (ROOT / 'build/pack-validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS: pack audit. Native Arm compilation and hardware validation are separate requirements.')


if __name__ == '__main__':
    try:
        run()
    except (AssertionError, KeyError, OSError, ET.ParseError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
