#!/usr/bin/env python3
"""Рендер встроенных обоев EndixOS (3D-сцены: рельеф, вода, небо) -> assets/wall/*.jpg и assets/wall.pak.
Нужны numpy и Pillow. Готовый wall.pak лежит в репозитории, пересобирать его нужно только при изменении сцен:
    python3 tools/render_wallpapers.py [имя ...]
"""
import sys, os, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

RW, RH = (480, 300) if os.environ.get("PREVIEW") else (1200, 750)      # размер рендера, затем увеличивается до 1280x800
OW, OH = 1920, 1200
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")

# ---------- шум ----------
def hash2(ix, iy, seed=0):
    n = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n = n ^ (n >> 16)
    return (n & 0xFFFFFF).astype(np.float32) / 16777215.0

def vnoise(x, y, seed=0):
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = (x - ix).astype(np.float32), (y - iy).astype(np.float32)
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(ix, iy, seed), hash2(ix + 1, iy, seed)
    c, d = hash2(ix, iy + 1, seed), hash2(ix + 1, iy + 1, seed)
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy

def fbm(x, y, oct=5, seed=0, lac=2.0, gain=0.5):
    s, amp, f, norm = 0, 1.0, 1.0, 0
    for i in range(oct):
        s = s + amp * vnoise(x * f, y * f, seed + i * 17)
        norm += amp; amp *= gain; f *= lac
    return s / norm

def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)

def mix(a, b, t):
    t = np.asarray(t, np.float32)
    if t.ndim == 2: t = t[..., None]
    return a + (b - a) * t

def col(r, g, b): return np.array([r, g, b], np.float32) / 255.0

# ---------- камера и марш по высотному полю ----------
def rays(w, h, fov=1.1, pitch=0.0):
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    u = (xs - w / 2) / h * fov * 2
    v = -(ys - h / 2) / h * fov * 2
    d = np.stack([u, v - pitch, np.ones_like(u)], -1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)

def march(ro, rd, height, tmax=400.0, steps=140, k=0.45, tmin=0.5):
    h, w = rd.shape[:2]
    t = np.full((h, w), tmin, np.float32)
    hit = np.zeros((h, w), bool)
    for _ in range(steps):
        p = ro + rd * t[..., None]
        dh = p[..., 1] - height(p[..., 0], p[..., 2])
        newhit = dh < 0.02 * t
        hit |= newhit
        step = np.where(hit, 0, np.maximum(dh * k, 0.05 + 0.002 * t))
        t = np.minimum(t + step, tmax)
    p = ro + rd * t[..., None]
    return t, hit & (t < tmax - 1), p

def normal(height, p, e=0.15):
    hx = height(p[..., 0] + e, p[..., 2]) - height(p[..., 0] - e, p[..., 2])
    hz = height(p[..., 0], p[..., 2] + e) - height(p[..., 0], p[..., 2] - e)
    n = np.stack([-hx, np.full_like(hx, 2 * e), -hz], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)

def shadow(p, sun, height, steps=26):
    t = np.full(p.shape[:2], 0.6, np.float32)
    res = np.ones(p.shape[:2], np.float32)
    for _ in range(steps):
        q = p + sun * t[..., None]
        dh = q[..., 1] - height(q[..., 0], q[..., 2])
        res = np.minimum(res, np.clip(6.0 * dh / t, 0, 1))
        t = t + np.clip(dh * 0.6, 0.35, 8)
    return np.clip(res, 0, 1)

def sky_gradient(rd, horizon, zenith, sun_dir, sun_col, sun_pow=60.0, glow=0.0, glow_col=None):
    y = np.clip(rd[..., 1], 0, 1)
    c = mix(horizon, zenith, np.power(y, 0.55))
    s = np.clip((rd * sun_dir).sum(-1), 0, 1)
    c = c + sun_col * (np.power(s, sun_pow) * 1.2)[..., None] + (glow_col if glow_col is not None else sun_col) * (np.power(s, 6.0) * glow)[..., None]
    c = c + sun_col * (np.power(s, 600.0) * 3.0)[..., None]
    return c

def clouds(rd, c, sunc, lowc, cov=0.5, scale=3.0, y0=0.0):
    ok = rd[..., 1] > 0.015
    yy = np.maximum(rd[..., 1], 0.03)
    u, v = rd[..., 0] / yy * scale, rd[..., 2] / yy * scale
    n = fbm(u + 3.1, v * 1.3, 6, 71)
    a = smooth(cov - 0.12, cov + 0.25, n) * smooth(0.0, 0.18, rd[..., 1]) * ok
    shade = np.clip(0.55 + 0.8 * (fbm(u + 3.1 + 0.2, v * 1.3 + 0.05, 5, 71) < n) * 0 + (n - cov) * 1.4, 0, 1)
    cc = mix(lowc, sunc, shade)
    return mix(c, cc, a * 0.85)

def tonemap(c, exposure=1.0, sat=1.0, vig=0.18):
    c = c * exposure
    c = c / (1 + 0.25 * c)                      # мягкое сжатие светов
    g = c.mean(-1, keepdims=True)
    c = g + (c - g) * sat
    h, w = c.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((xs - w / 2) / (w / 2)) ** 2 + ((ys - h / 2) / (h / 2)) ** 2)
    c = c * (1 - vig * r * r)[..., None]
    return np.clip(c, 0, 1) ** (1 / 2.2)

def finish(c, name, grain=0.012, seed=1):
    im = Image.fromarray((np.clip(c, 0, 1) * 255 + 0.5).astype(np.uint8))
    im = im.resize((OW, OH), Image.LANCZOS)
    a = np.asarray(im).astype(np.float32)
    rng = np.random.default_rng(seed)
    a = a + rng.normal(0, grain * 255, a.shape[:2])[..., None]
    out = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    path = os.path.join(ROOT, "wall", name + ".jpg")
    out.save(path, quality=84, optimize=True, progressive=False, subsampling=2)
    print(name, os.path.getsize(path) // 1024, "KB")

# ---------- сцены ----------
def scene_sahara():
    def height(x, z):
        d = x * 0.045 + z * 0.03
        w = fbm(x * 0.015, z * 0.015, 3, 4) * 5
        ridge = 1 - np.abs(np.sin(d * 2.4 + w))              # гребни дюн
        ridge = ridge ** 2.2
        big = fbm(x * 0.008 + 2, z * 0.008, 4, 9)
        return 5.5 * ridge + 5 * big + 0.10 * fbm(x * 1.3, z * 1.3, 3, 3)
    ro = np.array([0, float(height(np.array([0.0]), np.array([0.0]))[0]) + 16.0, 0], np.float32)
    rd = rays(RW, RH, 1.0, pitch=0.20)
    sun = np.array([0.80, 0.24, 0.55], np.float32); sun /= np.linalg.norm(sun)
    sunc = col(255, 178, 100) * 2.6
    t, hit, p = march(ro, rd, height, 700, 170)
    sky = sky_gradient(rd, col(255, 190, 140), col(70, 104, 178), sun, sunc, 30, 0.7, col(255, 140, 80))
    sky = clouds(rd, sky, col(255, 196, 150), col(150, 110, 140), 0.60, 2.5)
    n = normal(height, p, 0.12)
    sh = shadow(p + n * 0.05, sun, height, 30)
    diff = np.clip((n * sun).sum(-1), 0, 1) * sh
    amb = (0.25 + 0.3 * n[..., 1])[..., None] * col(150, 135, 170)
    base = mix(col(214, 150, 92), col(240, 196, 130), smooth(0.5, 1.0, n[..., 1]))
    ripple = 0.88 + 0.24 * fbm(p[..., 0] * 6, p[..., 2] * 6, 3, 21)
    c = base * ripple[..., None] * (amb + diff[..., None] * sunc * 0.85)
    c = c + col(255, 120, 60) * (np.power(1 - np.clip(n[..., 1], 0, 1), 2) * sh * 0.35)[..., None]   # тёплый отсвет на склонах
    fog = 1 - np.exp(-t * 0.0032)
    c = mix(c, col(240, 170, 130), fog * 0.9)
    c = np.where(hit[..., None], c, sky)
    finish(tonemap(c, 1.15, 1.2), "Sahara", seed=11)

def scene_ocean():
    ro = np.array([0, 3.2, 0], np.float32)
    rd = rays(RW, RH, 1.0, pitch=0.03)
    sun = np.array([0.0, 0.08, 1.0], np.float32); sun /= np.linalg.norm(sun)
    sunc = col(255, 170, 90) * 2.6
    sky = sky_gradient(rd, col(255, 188, 122), col(36, 66, 130), sun, sunc, 120, 0.9, col(255, 120, 60))
    sky = clouds(rd, sky, col(255, 168, 120), col(70, 60, 100), 0.5, 2.2)
    below = rd[..., 1] < -0.0005
    t = np.where(below, -ro[1] / np.minimum(rd[..., 1], -1e-4), 1e5).astype(np.float32)
    x = ro[0] + rd[..., 0] * t; z = ro[2] + rd[..., 2] * t
    def wave(x, z):
        return (np.sin(x * 0.7 + z * 0.35 + fbm(x * 0.1, z * 0.1, 3, 2) * 5) * 0.35 + np.sin(x * 1.9 - z * 0.8 + 1.3) * 0.12 +
                (fbm(x * 0.9, z * 0.9 * 0.6, 4, 41) - 0.5) * 0.35)
    e = 0.06
    hx = wave(x + e, z) - wave(x - e, z); hz = wave(x, z + e) - wave(x, z - e)
    damp = np.exp(-t * 0.012)
    n = np.stack([-hx / (2 * e) * 0.5 * damp, np.ones_like(hx), -hz / (2 * e) * 0.5 * damp], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    refl = rd - 2 * (rd * n).sum(-1, keepdims=True) * n
    refl[..., 1] = np.abs(refl[..., 1])
    rs = sky_gradient(refl, col(255, 188, 122), col(36, 66, 130), sun, sunc, 120, 0.9, col(255, 120, 60))
    rs = clouds(refl, rs, col(255, 168, 120), col(70, 60, 100), 0.5, 2.2)
    cosv = np.clip(-(rd * n).sum(-1), 0, 1)
    fres = 0.04 + 0.96 * np.power(1 - cosv, 5)
    deep = mix(col(4, 16, 34), col(10, 42, 62), np.clip(0.5 + 0.7 * (n[..., 1] - 0.9) * 8, 0, 1))
    water = mix(deep, rs, np.clip(fres * 1.1, 0, 1)[..., None])
    water = mix(water, col(255, 190, 130), smooth(0.998, 1.0, (refl * sun).sum(-1))[..., None] * 0.7)
    fog = np.exp(-np.minimum(t, 4000) * 0.0009)
    water = mix(col(255, 178, 120), water, fog[..., None] * 0.5 + 0.5 * smooth(0, 1, fog)[..., None])
    c = np.where(below[..., None], water, sky)
    finish(tonemap(c, 1.0, 1.15), "Sunset", seed=14)

def scene_ridges():
    W, H = OW, OH
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    yy = ys / H
    top, mid, hor = col(54, 74, 138), col(214, 128, 150), col(255, 196, 140)
    c = mix(hor, top, np.power(np.clip(1 - yy / 0.62, 0, 1), 0.0) * 0 + smooth(0.0, 0.62, 0.62 - yy))
    c = mix(c, mid, (np.exp(-((yy - 0.36) / 0.16) ** 2) * 0.55).astype(np.float32))
    sx, sy = W * 0.34, H * 0.42
    d = np.sqrt((xs - sx) ** 2 + (ys - sy) ** 2)
    c = c + col(255, 214, 150) * (np.exp(-d / 40) * 1.0 + np.exp(-d / 260) * 0.55)[..., None]
    c = c + col(255, 250, 235) * (d < 22)[..., None] * 1.2
    xx = np.arange(W, dtype=np.float32)
    n = 7
    for L in range(n):
        z = L / (n - 1)
        base = H * (0.50 + 0.05 * L) - 40 * (1 - z)
        amp = 120 * (1 - 0.55 * z)
        prof = base - amp * (fbm(xx * 0.0042 * (1 + 0.3 * z) + L * 7.3, np.full_like(xx, L * 1.9), 6, 90 + L) - 0.35) * 2.2
        ridge_mask = ys > prof[None, :]
        far = mix(col(236, 160, 168), col(24, 30, 66), np.float32(z) ** 0.9)
        shade = 0.75 + 0.5 * fbm(xs * 0.004 + L * 3, ys * 0.02, 4, 120 + L)
        face = np.clip((xs - sx) / W + 0.6, 0, 1)
        layer = far * (shade * (0.8 + 0.4 * (1 - z)))[..., None]
        edge = np.exp(-np.maximum(ys - prof[None, :], 0) / 3.0) * (1 - z) * 0.5
        layer = layer + col(255, 190, 130) * edge[..., None] * 0.6
        fog = np.exp(-np.maximum(ys - prof[None, :], 0) / (60 + 90 * (1 - z)))
        layer = mix(layer, mix(col(250, 196, 170), far, 0.4), fog * 0.55 * (1 - z) )
        c = np.where(ridge_mask[..., None], layer, c)
    c = tonemap(c, 1.0, 1.1, 0.18)
    arr = c * 255 + np.random.default_rng(17).normal(0, 2.2, (H, W))[..., None]
    path = os.path.join(ROOT, "wall", "Ridges.jpg")
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(path, quality=86, optimize=True, progressive=False)
    print("Ridges", os.path.getsize(path) // 1024, "KB")

def scene_forest():
    W, H = OW, OH
    rng = np.random.default_rng(5)
    sky = np.zeros((H, W, 3), np.float32)
    yy = np.linspace(0, 1, H)[:, None]
    top, hor = col(120, 150, 170), col(232, 230, 214)
    sky[:] = (top + (hor - top) * np.power(yy, 0.7)[..., None])
    sx, sy = W * 0.72, H * 0.30                       # низкое солнце за дымкой
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    d = np.sqrt((xs - sx) ** 2 + (ys - sy) ** 2)
    sky += col(255, 236, 190) * (np.exp(-d / 70) * 0.55 + np.exp(-d / 380) * 0.28)[..., None]
    img = Image.fromarray((np.clip(sky, 0, 1) * 255).astype(np.uint8))
    layers = 9
    for L in range(layers):
        z = L / (layers - 1)                           # 0 дальний .. 1 ближний
        base_y = H * (0.50 + 0.46 * z ** 1.3)
        fogc = np.array([224, 224, 208], np.float32) * (1 - z) + np.array([14, 30, 26], np.float32) * z
        fogc = fogc * (0.9 + 0.1 * z)
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        dr = ImageDraw.Draw(layer)
        # холм
        pts = [(0, H)]
        for x in range(0, W + 8, 8):
            hy = base_y - (40 + 70 * (1 - z)) * (np.sin(x * 0.004 + L * 1.7) * 0.5 + 0.5) - 25 * np.sin(x * 0.011 + L)
            pts.append((x, hy))
        pts.append((W, H))
        dr.polygon(pts, fill=tuple(int(v) for v in fogc) + (255,))
        # ели
        ntree = int(60 + 110 * z)
        scale = 14 + 78 * z ** 1.2
        for _ in range(ntree):
            x = rng.uniform(-20, W + 20)
            idx = int(np.clip(x / 8, 0, len(pts) - 3)) + 1
            by = pts[idx][1] + rng.uniform(-2, 12 * z + 4)
            h = scale * rng.uniform(0.7, 1.5); w = h * rng.uniform(0.22, 0.32)
            tiers = 5
            shade = rng.uniform(0.82, 1.08)
            tc = tuple(int(np.clip(v * shade, 0, 255)) for v in fogc * (0.9 if z > 0.3 else 1.0)) + (255,)
            for k in range(tiers):
                ty = by - h * (k / tiers) * 0.92
                tw = w * (1 - k / tiers * 0.85)
                dr.polygon([(x - tw, ty), (x + tw, ty), (x, ty - h / tiers * 1.7)], fill=tc)
            dr.rectangle([x - w * 0.06, by, x + w * 0.06, by + h * 0.06], fill=tc)
        img.paste(layer, (0, 0), layer)
        # дымка между слоями
        mist = np.asarray(img).astype(np.float32) / 255
        my = np.exp(-((ys - base_y) / (H * 0.07)) ** 2)[..., None] * (0.35 * (1 - z) + 0.05)
        mist = mist + (col(240, 236, 220) - mist) * my
        img = Image.fromarray((np.clip(mist, 0, 1) * 255).astype(np.uint8))
    a = np.asarray(img).astype(np.float32) / 255
    # лучи света из-за деревьев
    ang = np.arctan2(ys - sy, xs - sx)
    rays_ = (np.sin(ang * 26 + 2.0) * 0.5 + 0.5) ** 6 * np.exp(-d / 650)
    a = a + col(255, 232, 180) * (rays_ * 0.10)[..., None]
    img = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7))
    c = np.asarray(img).astype(np.float32) / 255
    c = tonemap(c ** 2.2, 1.0, 1.0, 0.22)
    im = Image.fromarray((c * 255).astype(np.uint8))
    path = os.path.join(ROOT, "wall", "Forest.jpg")
    arr = np.asarray(im).astype(np.float32) + np.random.default_rng(15).normal(0, 3, (H, W))[..., None]
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(path, quality=84, optimize=True, progressive=False)
    print("Forest", os.path.getsize(path) // 1024, "KB")

def scene_night():
    W, H = RW * 2, RH * 2
    rng = np.random.default_rng(8)
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = xs / W, ys / H
    # полоса Млечного пути по диагонали
    ang = 0.55
    dx = (u - 0.5) * np.cos(ang) + (v - 0.5) * np.sin(ang) * (H / W)
    dy = -(u - 0.5) * np.sin(ang) + (v - 0.5) * np.cos(ang) * (H / W)
    band = np.exp(-(dy / 0.11) ** 2)
    cloud = fbm(xs * 0.010, ys * 0.010, 6, 61)
    dust = smooth(0.5, 0.7, fbm(xs * 0.02 + 8, ys * 0.02, 5, 62))
    mw = band * (0.55 + 0.7 * cloud) * (1 - 0.45 * dust)
    c = np.zeros((H, W, 3), np.float32)
    base = mix(col(6, 10, 26), col(14, 26, 58), np.clip(1 - v, 0, 1) ** 1.3)
    c += base
    c += (col(120, 130, 190) * mw[..., None] * 0.55 + col(200, 160, 140) * (band ** 3 * cloud)[..., None] * 0.35)
    # звёзды
    n = 15000
    sxs, sys_ = rng.integers(0, W, n), rng.integers(0, int(H * 0.82), n)
    mag = rng.power(0.35, n)                            # много тусклых, мало ярких
    dens = np.exp(-(((sxs / W - 0.5) * np.cos(ang) + (sys_ / H - 0.5) * np.sin(ang) * (H / W)) * 0 + 0))
    star = np.zeros((H, W), np.float32)
    for x, y, m in zip(sxs, sys_, mag):
        br = 0.25 + 1.2 * m ** 3
        star[y, x] += br
    star = np.asarray(Image.fromarray(np.clip(star * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8))).astype(np.float32) / 255 * 4.0
    tint = np.stack([1 + 0.2 * np.sin(xs * 0.3), np.ones_like(xs), 1 - 0.2 * np.sin(xs * 0.3)], -1)
    c += star[..., None] * tint
    # пустыня: дюны-силуэт с лёгкой подсветкой неба
    hz = H * 0.80 + 26 * np.sin(xs * 0.006 + 1) + 14 * np.sin(xs * 0.017) + 40 * (fbm(xs * 0.004, ys * 0 + 3, 4, 77) - 0.5)
    ground = ys > hz
    glow = np.exp(-np.maximum(ys - hz, 0) / 18.0)
    g = col(10, 12, 22) + col(20, 28, 52) * (np.exp(-(ys - hz) / 130))[..., None].clip(0, 1)
    g = g * (0.8 + 0.4 * fbm(xs * 0.03, ys * 0.03, 3, 5))[..., None]
    c = np.where(ground[..., None], g, c)
    c += col(30, 44, 84) * (np.exp(-np.abs(ys - hz) / 16) * (~ground) * 0.25)[..., None]
    # силуэт сагуаро
    img = Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8))
    dr = ImageDraw.Draw(img)
    for cx, sc in ((W * 0.18, 1.0), (W * 0.82, 0.7)):
        by = int(hz[0, int(cx)]) + 6
        h = 150 * sc
        col_ = (6, 8, 14)
        dr.rounded_rectangle([cx - 8 * sc, by - h, cx + 8 * sc, by], radius=int(8 * sc), fill=col_)
        dr.rounded_rectangle([cx - 38 * sc, by - h * 0.62, cx - 22 * sc, by - h * 0.25], radius=int(8 * sc), fill=col_)
        dr.rectangle([cx - 38 * sc, by - h * 0.32, cx - 8 * sc, by - h * 0.25], fill=col_)
        dr.rounded_rectangle([cx + 22 * sc, by - h * 0.78, cx + 38 * sc, by - h * 0.4], radius=int(8 * sc), fill=col_)
        dr.rectangle([cx + 8 * sc, by - h * 0.46, cx + 38 * sc, by - h * 0.4], fill=col_)
    c = np.asarray(img).astype(np.float32) / 255
    c = c / (1 + 0.5 * c)
    out = Image.fromarray((np.clip(c, 0, 1) ** (1 / 1.0) * 255).astype(np.uint8)).resize((OW, OH), Image.LANCZOS)
    arr = np.asarray(out).astype(np.float32) + np.random.default_rng(16).normal(0, 2.0, (OH, OW))[..., None]
    path = os.path.join(ROOT, "wall", "Night.jpg")
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(path, quality=86, optimize=True, progressive=False)
    print("Night", os.path.getsize(path) // 1024, "KB")

# ---------- 2D-сцены в стиле macOS (рисуются сразу в цвете экрана) ----------
def uv():
    ys, xs = np.mgrid[0:RH, 0:RW].astype(np.float32)
    return xs / RW, ys / RH

def blur(c, r):
    im = Image.fromarray((np.clip(c, 0, 1) * 255 + 0.5).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r))
    return np.asarray(im).astype(np.float32) / 255

def vignette(c, k=0.22):
    u, v = uv()
    r = ((u - 0.5) * 1.6) ** 2 + ((v - 0.5) * 1.3) ** 2
    return c * (1 - k * r)[..., None]

def scene_aurora():
    u, v = uv(); hz = 0.86
    top, bot = col(3, 6, 22), col(14, 40, 70)
    sky = mix(top, bot, smooth(0.0, 0.8, v) ** 1.3)
    st = hash2(np.floor(u * RW / 1.6), np.floor(v * RH / 1.6), 5)
    sky = sky + (st > 0.9965)[..., None] * (0.5 + 0.5 * hash2(np.floor(u * 90), np.floor(v * 60), 9))[..., None]
    for k, (yc, w, c1, c2, sd) in enumerate([(0.30, 0.10, col(70, 255, 160), col(150, 90, 255), 3),
                                              (0.40, 0.08, col(60, 230, 200), col(90, 140, 255), 8),
                                              (0.24, 0.07, col(120, 255, 140), col(255, 90, 200), 13)]):
        wob = 0.10 * np.sin(u * 5.0 + k * 2.0) + 0.12 * (fbm(u * 2.5 + k, np.full_like(u, k * 3.1), 3, sd) - 0.5)
        d = (v - (yc + wob)) / w
        band = np.exp(-np.where(d > 0, d * d * 0.7, d * d * 3.0))
        streak = 0.35 + 0.9 * fbm(u * 55, v * 1.2 + k, 3, sd + 1)
        inten = band * streak * (0.55 + 0.7 * fbm(u * 4 + k * 5, v * 2, 3, sd + 2))
        tint = mix(c1, c2, smooth(-1.0, 1.4, -d))
        sky = sky + tint * inten[..., None] * 0.95
    ridge = 0.70 + 0.17 * (fbm(u * 2.2, np.zeros_like(u) + 1.5, 5, 21) - 0.5) * 2 + 0.05 * (fbm(u * 14, np.zeros_like(u), 3, 22) - 0.5)
    sky = np.clip(sky, 0, 1.2)
    out = sky.copy()
    rows = np.arange(RH)
    mrow = np.clip((2 * hz * RH - rows).astype(int), 0, RH - 1)
    refl = blur(np.clip(sky[mrow], 0, 1), 3) * 0.55 + col(2, 8, 18) * 0.45
    ripple = 0.04 * np.sin(v * 420 + u * 6) * smooth(hz, 1.0, v)
    water = mix(refl, col(3, 10, 20), smooth(hz, 1.0, v) * 0.5) * (1 + ripple)[..., None]
    out = np.where((v > hz)[..., None], water, out)
    ground = (v > ridge) & (v <= hz + 0.002)
    shade = mix(col(5, 9, 18), col(16, 30, 44), smooth(ridge, hz, v) * 0.6)
    rim = np.exp(-np.abs(v - ridge) * 260)[..., None] * col(40, 90, 80) * 0.5
    out = np.where(ground[..., None], shade + rim, out)
    finish(vignette(np.clip(out, 0, 1), 0.3), "Aurora", 0.012, 31)

def scene_liquid():
    u, v = uv()
    c = mix(col(10, 8, 40), col(52, 14, 96), smooth(0, 1, v * 0.8 + u * 0.4))
    pal = [col(255, 64, 140), col(255, 150, 54), col(70, 120, 255), col(170, 84, 255), col(40, 220, 220)]
    asp = RW / RH
    for k in range(4):
        wx = fbm(u * 1.1 + k * 3.1, v * 1.1 + k, 3, 40 + k)
        f = np.sin((u * asp * (0.55 + 0.1 * k) + k * 1.3 + 1.7 * wx) * 2.0 + v * (1.6 - 0.2 * k)) * 0.5 + 0.5
        h = smooth(0.42, 0.78, f) * (1 - smooth(0.86, 0.99, f))
        h = blur(h, 1.5) if False else h
        gy, gx = np.gradient(h)
        nz = 1.0
        n = np.stack([-gx * 9, -gy * 9, np.full_like(h, nz)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        L = np.array([-0.4, -0.55, 0.73], np.float32); L /= np.linalg.norm(L)
        dif = np.clip((n * L).sum(-1), 0, 1)
        hl = np.clip((n * (L + np.array([0, 0, 1], np.float32)) / 1.6).sum(-1), 0, 1) ** 28
        base = pal[k]
        col_ = base * (0.22 + 0.62 * dif)[..., None] + hl[..., None] * 0.30
        fresnel = (1 - n[..., 2]) ** 1.5
        col_ = col_ + fresnel[..., None] * pal[(k + 2) % 5] * 0.45
        a = (h * 0.92)[..., None]
        c = c * (1 - a) + col_ * a
    glow = blur(np.clip(c - 0.6, 0, 1), RW / 40)
    c = c + glow * 0.5
    finish(vignette(np.clip(c, 0, 1), 0.28), "Liquid", 0.010, 32)

def scene_dawn():
    u, v = uv()
    sky = mix(col(104, 96, 190), col(250, 150, 170), smooth(0.0, 0.55, v))
    sky = mix(sky, col(255, 205, 150), smooth(0.45, 0.78, v))
    sx, sy = 0.66, 0.60
    d = np.sqrt(((u - sx) * 1.6) ** 2 + (v - sy) ** 2)
    sky = sky + col(255, 190, 110) * (np.exp(-d * 4.0) * 0.55 + np.exp(-d * 14.0) * 0.7)[..., None]
    sky = sky + (smooth(0.045, 0.03, d))[..., None] * col(255, 245, 220)
    cl = fbm(u * 3.0, v * 14.0, 4, 50)
    cmask = smooth(0.52, 0.8, cl) * smooth(0.05, 0.4, v) * (1 - smooth(0.4, 0.62, v))
    sky = mix(sky, col(255, 200, 190) + col(0, 20, 40) * v[..., None], cmask * 0.55)
    out = sky
    layers = [(0.62, 0.05, col(150, 120, 190), 61), (0.68, 0.06, col(120, 92, 168), 62), (0.75, 0.07, col(88, 66, 140), 63),
              (0.83, 0.08, col(58, 42, 104), 64), (0.92, 0.07, col(32, 24, 70), 65)]
    for i, (base, amp, c_, sd) in enumerate(layers):
        r = base + amp * (fbm(u * (2.0 + i * 0.8), np.zeros_like(u) + i, 5, sd) - 0.5) * 2
        m = (v > r)[..., None]
        haze = smooth(r, r + 0.18, v) ** 0.8
        lay = mix(c_, mix(c_, col(255, 190, 170), 0.5), (1 - haze) * (0.75 - i * 0.12))
        glow = np.exp(-np.abs(v - r) * 120)[..., None] * col(255, 190, 150) * (0.35 - i * 0.05)
        out = np.where(m, lay + glow, out)
    finish(vignette(np.clip(out, 0, 1), 0.22), "Dawn", 0.012, 33)

# ---------- «живые» обои день/ночь в духе macOS (пара сцен: "<Имя> Day" и "<Имя> Night") ----------
def sheet(out, a, b, edge, c_edge, c_deep, span=0.5, shadow=0.35, rim=0.22, rimc=None):
    d = b - edge
    out = out * (1 - shadow * np.exp(-np.maximum(-d, 0) * 10) * (d <= 0))[..., None]
    t = smooth(0.0, span, d)
    body = mix(c_edge, c_deep, t)
    body = body + (rimc if rimc is not None else c_edge) * (np.exp(-np.maximum(d, 0) * 40) * rim)[..., None]
    return np.where((d > 0)[..., None], body, out)

def rot(u, v, deg, ox, oy):
    asp = RW / RH
    th = np.deg2rad(deg); x, y = (u - ox) * asp, v - oy
    return x * np.cos(th) + y * np.sin(th), -x * np.sin(th) + y * np.cos(th)

def petals(night):
    u, v = uv(); asp = RW / RH
    dx, dy = u * asp + 0.25 * asp, 1.2 - v
    r, phi = np.hypot(dx, dy), np.arctan2(dy, dx)
    if not night:
        out = mix(col(92, 168, 236), col(140, 200, 248), smooth(0, 1, v))
        P = [(1.12, 0.34, 0.3, col(255, 196, 96), col(240, 120, 44)), (0.98, 0.48, 1.4, col(255, 222, 120), col(250, 150, 50)),
             (0.70, 0.44, 2.6, col(255, 206, 100), col(236, 112, 40)), (0.46, 0.38, 3.9, col(252, 184, 84), col(214, 86, 36)),
             (0.24, 0.30, 5.0, col(255, 170, 70), col(200, 72, 40))]
    else:
        out = mix(col(4, 12, 36), col(10, 24, 58), smooth(0, 1, v))
        P = [(1.12, 0.34, 0.3, col(150, 50, 80), col(36, 20, 80)), (0.98, 0.48, 1.4, col(240, 120, 50), col(120, 34, 66)),
             (0.70, 0.44, 2.6, col(226, 96, 44), col(100, 28, 70)), (0.46, 0.38, 3.9, col(190, 64, 56), col(70, 24, 84)),
             (0.24, 0.30, 5.0, col(150, 50, 80), col(44, 20, 90))]
    for (c0, hw0, ph, ce, cd) in P:
        c = c0 + 0.09 * np.sin(r * 3.2 + ph) + 0.03 * np.sin(r * 8.0 + ph * 2)
        hw = hw0 * (0.35 + 0.9 * smooth(0.1, 1.5, r))
        p1, p2 = c - hw, c + hw
        t = (phi - p1) / (p2 - p1)
        out = out * (1 - 0.40 * np.exp(-np.maximum(p1 - phi, 0) * r * 9) * (t <= 0))[..., None]
        body = mix(ce, cd, np.clip(t, 0, 1) ** 1.3)
        body = body + ce * (np.exp(-np.clip(t, 0, 9) * 14) * 0.28)[..., None]
        out = np.where(((t > 0) & (t < 1))[..., None], body, out)
    g = blur(np.clip(out - 0.62, 0, 1), RW / 60)
    out = out + g * (0.45 if not night else 0.9)
    finish(vignette(np.clip(out, 0, 1), 0.18), "Petals" + (" Night" if night else " Day"), 0.010, 41 + night)

def layers(night):
    u, v = uv()
    if not night:
        sky = mix(col(250, 196, 150), col(120, 160, 225), smooth(0.55, 0.0, v))
        L = [(0.40, 0.10, 0.5, col(244, 150, 120), col(220, 110, 130)),
             (0.50, 0.12, 0.8, col(120, 200, 215), col(70, 150, 200)),
             (0.62, 0.13, 1.1, col(110, 130, 220), col(80, 80, 190)),
             (0.74, 0.12, 1.4, col(235, 110, 80), col(200, 60, 90)),
             (0.86, 0.10, 1.8, col(190, 110, 240), col(120, 70, 200))]
    else:
        sky = mix(col(30, 34, 80), col(8, 10, 34), smooth(0.7, 0.0, v))
        L = [(0.40, 0.10, 0.5, col(60, 62, 120), col(34, 36, 90)),
             (0.50, 0.12, 0.8, col(40, 120, 150), col(30, 80, 130)),
             (0.62, 0.13, 1.1, col(70, 80, 170), col(44, 44, 130)),
             (0.74, 0.12, 1.4, col(190, 60, 62), col(120, 34, 70)),
             (0.86, 0.10, 1.8, col(150, 80, 210), col(86, 44, 150))]
    out = sky
    for i, (base, amp, fq, ce, cd) in enumerate(L):
        edge = base + amp * 0.5 * np.sin(u * fq * 3.2 + i * 1.7 + 0.5) + amp * 0.18 * np.sin(u * fq * 7.0 + i * 2.9) - amp * 0.35 * smooth(0.0, 1.0, u) * (i % 2)
        out = sheet(out, u, v, edge, ce, cd, span=0.35, shadow=0.38, rim=0.30)
    g = blur(np.clip(out - 0.7, 0, 1), RW / 70)
    out = out + g * 0.5
    finish(vignette(np.clip(out, 0, 1), 0.16), "Layers" + (" Night" if night else " Day"), 0.010, 51 + night)

def scene_petals_day(): petals(0)
def scene_petals_night(): petals(1)
def scene_layers_day(): layers(0)
def scene_layers_night(): layers(1)

SCENES = [("Sahara", scene_sahara), ("Sunset", scene_ocean), ("Forest", scene_forest),
          ("Ridges", scene_ridges), ("Night", scene_night),
          ("Aurora", scene_aurora), ("Liquid", scene_liquid), ("Dawn", scene_dawn),
          ("Petals Day", scene_petals_day), ("Petals Night", scene_petals_night),
          ("Layers Day", scene_layers_day), ("Layers Night", scene_layers_night)]
TITLES = {"Sahara": "Sahara Dusk", "Sunset": "Ocean Sunset", "Forest": "Misty Forest 3D",
          "Ridges": "Blue Ridge", "Night": "Desert Night",
          "Aurora": "Northern Lights", "Liquid": "Liquid Glass", "Dawn": "Violet Dawn",
          "Petals Day": "Petals Day", "Petals Night": "Petals Night", "Layers Day": "Layers Day", "Layers Night": "Layers Night"}

def pack():
    names = [n for n, _ in SCENES]
    blobs = [open(os.path.join(ROOT, "wall", n + ".jpg"), "rb").read() for n in names]
    hdr = struct.pack("<I", len(names))
    off = 4 + len(names) * 32
    ent = b""
    for n, b in zip(names, blobs):
        ent += struct.pack("<II24s", off, len(b), TITLES[n].encode()[:23])
        off += len(b)
    open(os.path.join(ROOT, "wall.pak"), "wb").write(hdr + ent + b"".join(blobs))
    print("wall.pak", off // 1024, "KB")

if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "wall"), exist_ok=True)
    want = sys.argv[1:]
    for n, f in SCENES:
        if not want or n in want:
            f()
    if not want:
        pack()
