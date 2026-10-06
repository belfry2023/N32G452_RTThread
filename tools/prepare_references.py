"""Extract reference packages to short paths, preserving the original archives."""
from pathlib import Path, PurePosixPath
import argparse, json, zipfile, hashlib
ROOT=Path(__file__).resolve().parents[1]
NAMES={'1':'rtdev','2':'hsi','3':'poweron','4':'iap','5':'gcc',
       '6':'rsram','7':'mmu','8':'secure','9':'jlink','10':'flash'}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packages', nargs='*', choices=sorted(NAMES.values()) + ['all'],
                        help='Packages to extract, e.g. gcc; all extracts 1-10. No arguments only updates inventory.')
    args=parser.parse_args()
    out=ROOT/'references'
    out.mkdir(exist_ok=True)
    inventory=[]
    for archive in sorted((ROOT/'zip').iterdir()):
        if not zipfile.is_zipfile(archive): continue
        with zipfile.ZipFile(archive) as package:
            entries=package.infolist()
            roots={n.filename.replace('\\','/').split('/')[0] for n in entries}
            short=NAMES.get(archive.stem)
            row={'archive':archive.name,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
                 'entries':len(entries),'original_roots':sorted(roots),
                 'destination':f'references/{short}' if short else 'existing SDK / CMSIS pack'}
            if short and (short in args.packages or 'all' in args.packages):
                target=(out/short).resolve()
                for item in entries:
                    parts=PurePosixPath(item.filename.replace('\\','/')).parts
                    if len(roots)==1: parts=parts[1:]
                    if not parts: continue
                    if any(x in ('..','.') or ':' in x for x in parts): raise ValueError(item.filename)
                    dest=target.joinpath(*parts).resolve()
                    if not dest.is_relative_to(target): raise ValueError(item.filename)
                    if item.is_dir(): dest.mkdir(parents=True,exist_ok=True)
                    else:
                        dest.parent.mkdir(parents=True,exist_ok=True)
                        data=package.read(item)
                        if not dest.exists(): dest.write_bytes(data)
                        elif dest.read_bytes()!=data: raise ValueError(f'Preserving modified reference: {dest}')
            inventory.append(row)
    (out/'manifest.json').write_text(json.dumps(inventory,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Indexed',len(inventory),'packages; extracted:', ', '.join(args.packages) or 'none')
if __name__=='__main__': main()
