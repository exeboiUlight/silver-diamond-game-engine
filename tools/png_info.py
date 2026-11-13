"""Print the average color of a PNG (pure stdlib, first rows only).

Usage: python tools/png_info.py file.png [rows]
"""
import struct
import sys
import zlib


def chunks(data):
    i = 8
    while i + 8 <= len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        tag = data[i + 4:i + 8]
        payload = data[i + 8:i + 8 + length]
        yield tag, payload
        i += 12 + length


def main(path, max_rows=64):
    data = open(path, "rb").read()
    w = h = depth = ctype = None
    idat = b""
    for tag, payload in chunks(data):
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", payload[:10])
        elif tag == b"IDAT":
            idat += payload
    print(f"{path}: {w}x{h} depth={depth} color_type={ctype}")
    if depth != 8:
        print("only 8 bit images are supported")
        return
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * nch
    prev = bytearray(stride)
    pos = 0
    total = [0, 0, 0]
    count = 0
    rows = 0
    while pos < len(raw) and rows < max_rows:
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filt == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        prev = line
        for x in range(0, w, max(1, w // 64)):
            for k in range(min(3, nch)):
                total[k] += line[x * nch + k]
            count += 1
        rows += 1
    if count:
        print("  average color (top rows): "
              + ", ".join(f"{t / count:.1f}" for t in total))


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 64)
