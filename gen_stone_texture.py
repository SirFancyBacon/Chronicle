#!/usr/bin/env python3
import math
import struct
import zlib

SIZE = 512
MASK = SIZE - 1

BAYER_4X4 = (
    (-0.46875,  0.03125, -0.34375,  0.15625),
    ( 0.28125, -0.21875,  0.40625, -0.09375),
    (-0.28125,  0.21875, -0.40625,  0.09375),
    ( 0.46875, -0.03125,  0.34375, -0.15625),
)

def hash2(ix, iy, seed=0):
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFFFFFF) / 4294967295.0

def smooth_noise_pow2(x, y, px_mask, py_mask, seed):
    x0 = math.floor(x)
    y0 = math.floor(y)
    fx = x - x0
    fy = y - y0
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    ix0, ix1 = x0 & px_mask, (x0 + 1) & px_mask
    iy0, iy1 = y0 & py_mask, (y0 + 1) & py_mask
    v00 = hash2(ix0, iy0, seed)
    v10 = hash2(ix1, iy0, seed)
    v01 = hash2(ix0, iy1, seed)
    v11 = hash2(ix1, iy1, seed)
    return (v00 + (v10 - v00) * ux) * (1.0 - uy) + (v01 + (v11 - v01) * ux) * uy

def fbm_periodic512(x, y, cell_size, octaves, seed):
    val, amp = 0.0, 0.5
    inv_cell = 1.0 / cell_size
    period = max(1, 512 // cell_size)
    for i in range(octaves):
        m = period - 1
        val += amp * smooth_noise_pow2(x * inv_cell, y * inv_cell, m, m, seed + i * 17)
        inv_cell *= 2.0
        period <<= 1
        amp *= 0.5
    return val

def soft_ridged_periodic512(x, y, cell_size, octaves, seed):
    val, amp = 0.0, 0.5
    inv_cell = 1.0 / cell_size
    period = max(1, 512 // cell_size)
    for i in range(octaves):
        m = period - 1
        n = smooth_noise_pow2(x * inv_cell, y * inv_cell, m, m, seed + i * 29)
        ridge = 1.0 - math.sqrt((n * 2.0 - 1.0) ** 2 + 0.08)
        val += ridge * amp
        inv_cell *= 2.0
        period <<= 1
        amp *= 0.5
    return val

def blur_periodic512(field, passes):
    tmp = [0.0] * (SIZE * SIZE)
    for _ in range(passes):
        for y in range(SIZE):
            row = y * SIZE
            for x in range(SIZE):
                tmp[row + x] = (
                    field[row + ((x - 2) & MASK)] * 1.0 +
                    field[row + ((x - 1) & MASK)] * 4.0 +
                    field[row + (x & MASK)]       * 6.0 +
                    field[row + ((x + 1) & MASK)] * 4.0 +
                    field[row + ((x + 2) & MASK)] * 1.0
                ) / 16.0
        for y in range(SIZE):
            rm2 = ((y - 2) & MASK) * SIZE
            rm1 = ((y - 1) & MASK) * SIZE
            r0  = y * SIZE
            rp1 = ((y + 1) & MASK) * SIZE
            rp2 = ((y + 2) & MASK) * SIZE
            for x in range(SIZE):
                field[r0 + x] = (
                    tmp[rm2 + x] * 1.0 +
                    tmp[rm1 + x] * 4.0 +
                    tmp[r0  + x] * 6.0 +
                    tmp[rp1 + x] * 4.0 +
                    tmp[rp2 + x] * 1.0
                ) / 16.0

def write_png(path, width, height, rgba):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * width * 4 : (y + 1) * width * 4])
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
    png.extend(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
    png.extend(chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)

def generate():
    height_map = [0.0] * (SIZE * SIZE)
    tone_map   = [0.0] * (SIZE * SIZE)

    for y in range(SIZE):
        for x in range(SIZE):
            wx = (fbm_periodic512(x, y, 64, 3, 501) - 0.5) * 14.0
            wy = (fbm_periodic512(x, y, 64, 3, 601) - 0.5) * 14.0
            macro  = fbm_periodic512(x + wx, y + wy, 128, 4, 101)
            med    = fbm_periodic512(x - wy * 0.5, y + wx * 0.5, 64, 4, 303)
            detail = fbm_periodic512(x, y, 32, 3, 202)
            soft_r = soft_ridged_periodic512(x + wx * 0.4, y - wy * 0.4, 64, 2, 404)

            idx = y * SIZE + x
            height_map[idx] = macro * 0.35 + med * 0.38 + detail * 0.15 + soft_r * 0.12
            tone_map[idx]   = ((macro - 0.48) * 34.0 + (med - 0.48) * 22.0 +
                               (detail - 0.47) * 12.0 + (soft_r - 0.32) * 14.0)

    blur_periodic512(height_map, 2)
    blur_periodic512(tone_map, 1)

    for y in range(SIZE):
        for x in range(SIZE):
            tooth = smooth_noise_pow2(x * 0.5, y * 0.5, 255, 255, 808) * 0.65 + hash2(x, y, 909) * 0.35
            height_map[y * SIZE + x] += tooth * 0.085

    rgba = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        ym1, yp1 = (y - 1) & MASK, (y + 1) & MASK
        for x in range(SIZE):
            xm1, xp1 = (x - 1) & MASK, (x + 1) & MASK
            idx = y * SIZE + x
            bump = ((height_map[ym1 * SIZE + xm1] - height_map[yp1 * SIZE + xp1]) * 0.7 +
                    (height_map[ym1 * SIZE + x]   - height_map[yp1 * SIZE + x])   * 0.3) * 32.0
            dither = BAYER_4X4[y & 3][x & 3] * 2.2
            delta = tone_map[idx] + bump + dither

            r = round(max(0.0, min(255.0, 78.0 + delta * 0.88)) / 3.0) * 3
            g = round(max(0.0, min(255.0, 92.0 + delta * 1.00)) / 3.0) * 3
            b = round(max(0.0, min(255.0, 116.0 + delta * 1.14)) / 3.0) * 3

            out = idx * 4
            rgba[out + 0] = int(r)
            rgba[out + 1] = int(g)
            rgba[out + 2] = int(b)
            rgba[out + 3] = 255

    write_png("ui_stone.png", SIZE, SIZE, rgba)
    print("Wrote 512x512 blurred & embossed PS2 stone base tile ui_stone.png")

if __name__ == "__main__":
    generate()
