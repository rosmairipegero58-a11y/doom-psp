#!/usr/bin/env python3
"""Convierte EBOOT.PBP en un DOOM_PSP.iso que PPSSPP puede abrir.
Uso: python3 make_iso.py EBOOT.PBP carpeta_salida [--solo-carpeta]
"""
import os, struct, subprocess, sys, shutil

NAMES = ["PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC0.PNG", "PIC1.PNG", "SND0.AT3", "DATA.PSP", "DATA.PSAR"]

def read_pbp(path):
    d = open(path, "rb").read()
    if d[:4] != b"\0PBP":
        sys.exit("ERROR: %s no es un EBOOT.PBP valido" % path)
    offs = list(struct.unpack("<8I", d[8:40])) + [len(d)]
    return {n: d[offs[i]:offs[i + 1]] for i, n in enumerate(NAMES)}

def make_sfo(title, disc_id):
    """PARAM.SFO de tipo UMD (UG) con las claves ordenadas alfabeticamente."""
    ent = [  # (clave, tipo, valor, tamano_max)
        ("BOOTABLE", "i", 1, 4), ("CATEGORY", "s", "UG", 4), ("DISC_ID", "s", disc_id, 16),
        ("DISC_NUMBER", "i", 1, 4), ("DISC_TOTAL", "i", 1, 4), ("DISC_VERSION", "s", "1.00", 8),
        ("PARENTAL_LEVEL", "i", 1, 4), ("PSP_SYSTEM_VER", "s", "1.00", 8),
        ("REGION", "i", 32768, 4), ("TITLE", "s", title, 128)]
    ent.sort(key=lambda e: e[0])
    keys = b"".join(e[0].encode() + b"\0" for e in ent)
    while len(keys) % 4: keys += b"\0"
    n = len(ent)
    kstart = 20 + 16 * n
    dstart = kstart + len(keys)
    idx, data, koff = b"", b"", 0
    for k, t, v, mx in ent:
        if t == "i":
            raw, fmt, ln = struct.pack("<I", v), 0x0404, 4
        else:
            raw, fmt, ln = v.encode() + b"\0", 0x0204, len(v) + 1
        idx += struct.pack("<HHIII", koff, fmt, ln, mx, len(data))
        data += raw + b"\0" * (mx - len(raw))
        koff += len(k) + 1
    return struct.pack("<4sIIII", b"\0PSF", 0x101, kstart, dstart, n) + idx + keys + data

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pbp, out = sys.argv[1], sys.argv[2]
    only_tree = "--solo-carpeta" in sys.argv
    f = read_pbp(pbp)
    if not f["DATA.PSP"]:
        sys.exit("ERROR: el EBOOT.PBP no contiene el ejecutable (DATA.PSP)")
    root = os.path.join(out, "iso_root")
    shutil.rmtree(root, ignore_errors=True)
    sysdir = os.path.join(root, "PSP_GAME", "SYSDIR")
    os.makedirs(sysdir)
    open(os.path.join(root, "PSP_GAME", "PARAM.SFO"), "wb").write(make_sfo("DOOM PSP", "DOOM00001"))
    if f["ICON0.PNG"]:
        open(os.path.join(root, "PSP_GAME", "ICON0.PNG"), "wb").write(f["ICON0.PNG"])
    for name in ("EBOOT.BIN", "BOOT.BIN"):
        open(os.path.join(sysdir, name), "wb").write(f["DATA.PSP"])
    open(os.path.join(root, "UMD_DATA.BIN"), "w").write("DOOM00001|0000000000000000|0000|G")
    print("carpeta del disco lista:", root)
    if only_tree:
        return
    iso = os.path.join(out, "DOOM_PSP.iso")
    tool = shutil.which("genisoimage") or shutil.which("mkisofs") or shutil.which("xorrisofs")
    if not tool:
        sys.exit("ERROR: falta genisoimage (sudo apt-get install genisoimage)")
    cmd = [tool, "-iso-level", "1", "-V", "DOOMPSP", "-sysid", "PSP GAME", "-o", iso, root]
    subprocess.check_call(cmd)
    print("ISO creado:", iso, os.path.getsize(iso), "bytes")

if __name__ == "__main__":
    main()
