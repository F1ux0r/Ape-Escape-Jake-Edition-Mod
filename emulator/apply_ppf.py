"""Apply a PPF 3.0 patch to a copy of a disc image (the original is not
touched), checking first that it is the Redump Ape Escape (USA) image.

    python tools/apply_ppf.py "Ape Escape (USA).bin" "patch.ppf" "Ape Escape (USA) [modded].bin"

Writes a matching .cue next to the output.
"""
import hashlib, os, shutil, struct, sys

DISC_SHA256 = '1ae17e78ebb8c782c7c1785b0a0bd7b0ee28235b8a0c83c8df887129899a852a'


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, ppf, out = sys.argv[1:]
    print('checking the disc image ...')
    h = hashlib.sha256()
    with open(src, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 22), b''):
            h.update(chunk)
    if h.hexdigest() != DISC_SHA256:
        sys.exit('This is not the Redump Ape Escape (USA) image (SCUS-94423, single .bin).')
    patch = open(ppf, 'rb').read()
    if patch[:5] != b'PPF30':
        sys.exit('Not a PPF 3.0 patch.')
    print('copying ...')
    shutil.copyfile(src, out)
    pos = 56 + 4   # header: magic, encoding, description, image type, block check, undo, pad
    with open(out, 'r+b') as f:
        while pos < len(patch):
            offset, n = struct.unpack_from('<QB', patch, pos)
            pos += 9
            f.seek(offset)
            f.write(patch[pos:pos + n])
            pos += n
    base = os.path.splitext(os.path.basename(out))[0]
    with open(os.path.splitext(out)[0] + '.cue', 'w') as f:
        f.write('FILE "%s.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' % base)
    print('done: %s (open the .cue in DuckStation)' % (os.path.splitext(out)[0] + '.cue'))


if __name__ == '__main__':
    main()
