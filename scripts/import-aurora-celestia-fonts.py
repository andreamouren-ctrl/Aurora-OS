#!/usr/bin/env python3
"""Import the exact Aurora Celestia UI v1.0 ZIP, validating original file hashes."""
import argparse
import hashlib
from pathlib import Path
from zipfile import ZipFile

EXPECTED = {
"Regular.ttf":"4637123e5ac90e21e9b16efc0ec8c6ee3a75d17727f33305137e3539b54eee59",
"Medium.ttf":"607f4061ffc148ac949faa4739bae53f0ec0ed298adac7827fa5c951ead23092",
"SemiBold.ttf":"22eb223074e1a98393c94526b76d1dcd4f46b8eec4fb4d1e530fc05c5caaec2e",
"Bold.ttf":"f9a1855012a8e5c36d4522aa4e99ee1f8eb1ad565d35287e903d60f9a42a399c",
"Regular.otf":"8ff1d9305513741dea75d05bce98546adaa9ee6c25386600d20e80802e207ef8",
"Medium.otf":"700a2dba3814a7212419c9aa9775f9b1605b845f83d5be6b852d0c1fd0e449bd",
"SemiBold.otf":"a2b4d488e727a467458e9f84db42cb249ab81db015d8dd07047302e59c2f35c7",
"Bold.otf":"edb942bb123f4b7bc872acf35c25a3b44add8406048658ceb4116687714b209d",
"Regular.woff2":"f8085d29df725984a26b71db42213ee9bbd537650c4aabea2e97c7b918c06992",
"Medium.woff2":"9123a4d514ceb7872cc42b65861b8235e7d395ccb15ff68474e050ec4c189578",
"SemiBold.woff2":"09165445c8a5a839a81e8f1348241046b60329bf5b7b2e60f59c7e38e34076b6",
"Bold.woff2":"96ec98fc792817f53ffe22fccc19530f3485af06ca57ef054bb12152d5198c87"
}

def load(source):
    names = {"AuroraCelestiaUI-" + k: v for k,v in EXPECTED.items()}
    if source.is_file():
        with ZipFile(source) as z:
            found = {}
            for f in z.infolist():
                name = Path(f.filename).name
                if name not in names or f.is_dir(): continue
                if name in found or f.file_size > 2_000_000:
                    raise ValueError("duplicate/oversized font " + name)
                found[name] = z.read(f)
    elif source.is_dir():
        found = {p.name: p.read_bytes() for p in source.rglob("*")
                 if p.is_file() and p.name in names}
    else:
        raise ValueError("Need release ZIP or extracted directory")
    if set(found) != set(names):
        raise ValueError("missing fonts: " + ", ".join(sorted(set(names) - set(found))))
    for name, data in found.items():
        if hashlib.sha256(data).hexdigest() != names[name]:
            raise ValueError("checksum mismatch: " + name)
        magic = b"wOF2" if name.endswith(".woff2") else (b"OTTO" if name.endswith(".otf") else bytes([0,1,0,0]))
        if not data.startswith(magic):
            raise ValueError("bad SFNT signature: " + name)
    return found

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("release", type=Path)
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--verify-only", action="store_true")
    args = p.parse_args()
    files = load(args.release)
    print("PASS: exact 12 Aurora Celestia UI 1.0 fonts verified")
    if args.verify_only: return
    dest = args.repo / "assets/fonts/aurora-celestia/ui/v1.0"
    dest.mkdir(parents=True, exist_ok=True)
    for name, content in sorted(files.items()): (dest / name).write_bytes(content)
    print("Imported fonts to", dest)

if __name__ == "__main__":
    main()
