"""Generate the synthetic test images used by the fixtures and as main.py's default input.

All images are procedural and seeded, so no personal photos are needed:

    gradient_rgb.png      continuous tones + RGB colour: exercises gamma, noise
                          floor, curve, contrast and the grayscale conversion
    shapes_rgba.png       near-binary black shapes on white with an alpha
                          channel: hard edges, and checks that alpha is ignored
    scene_portrait.heic   photo-like portrait scene (sky, sun, hills, texture)
                          encoded as HEIC: exercises the HEIC decode path

Usage (from the repository root):
    venv/bin/python reference/make_test_images.py           # create missing images
    venv/bin/python reference/make_test_images.py --force   # recreate all

Existing images are kept by default: the HEIC encoder is not guaranteed to be
byte-for-byte reproducible, and recreating an image means the fixtures must be
regenerated (reference/export_fixtures.py).
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from pillow_heif import register_heif_opener

register_heif_opener()

OUT_DIR = Path(__file__).resolve().parent / "test_images"


def _to_uint8(values: np.ndarray) -> np.ndarray:
    return np.clip(np.round(values * 255), 0, 255).astype(np.uint8)


def gradient_rgb(width: int = 400, height: int = 300, seed: int = 1) -> Image.Image:
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    u, v = x / (width - 1), y / (height - 1)

    rng = np.random.default_rng(seed)
    noise = rng.normal(0.0, 0.04, size=(height, width))

    r = u
    g = 0.5 + 0.5 * np.sin(2 * np.pi * (3 * u + 2 * v))
    b = v * (0.6 + 0.4 * np.cos(2 * np.pi * 5 * u))
    rgb = np.stack([r, g, b], axis=-1) + noise[..., None]

    # An (H, W, 3) uint8 array is read as RGB.
    return Image.fromarray(_to_uint8(rgb))


def shapes_rgba(size: int = 500) -> Image.Image:
    img = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    draw = ImageDraw.Draw(img)

    # Horizontal bars -> sustained tones; vertical bars -> clicks/noise bursts.
    for i, y in enumerate(range(60, 460, 80)):
        draw.rectangle([40 + i * 20, y, 300 + i * 25, y + 12 + i * 3], fill=(0, 0, 0, 255))
    for x in range(330, 480, 30):
        draw.rectangle([x, 40, x + 6, 300], fill=(0, 0, 0, 255))

    # Diagonal line -> pitch sweep; circle -> curved partials.
    draw.line([(40, 480), (480, 330)], fill=(0, 0, 0, 255), width=8)
    draw.ellipse([330, 330, 470, 470], outline=(0, 0, 0, 255), width=10)

    # Semi-transparent and fully transparent regions: PIL's convert("L") must
    # ignore alpha, so these still read as the underlying RGB value.
    draw.rectangle([40, 20, 200, 40], fill=(0, 0, 0, 128))
    draw.rectangle([220, 20, 300, 40], fill=(0, 0, 0, 0))
    return img


def scene_portrait(width: int = 756, height: int = 1008, seed: int = 3) -> Image.Image:
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    u, v = x / (width - 1), y / (height - 1)
    rng = np.random.default_rng(seed)

    # Sky: vertical gradient from deep blue (top) to pale orange (horizon).
    sky = np.stack([0.15 + 0.8 * v, 0.25 + 0.5 * v, 0.6 - 0.2 * v], axis=-1)

    # Sun disc with a soft glow.
    dist = np.hypot((u - 0.68) * width, (v - 0.42) * height) / width
    glow = np.exp(-((dist / 0.12) ** 2))
    sky += glow[..., None] * np.array([0.9, 0.7, 0.3])

    # Two layers of hills (sums of sines) in front of the sky.
    def ridge(base, amps, freqs, phases):
        return base + sum(a * np.sin(2 * np.pi * f * u + p) for a, f, p in zip(amps, freqs, phases))

    far = v > ridge(0.55, [0.04, 0.015], [1.3, 5.0], [0.4, 1.9])
    near = v > ridge(0.7, [0.06, 0.02, 0.008], [0.8, 3.5, 11.0], [2.1, 0.3, 4.4])
    img = sky
    img[far] = np.array([0.25, 0.3, 0.35]) * (0.7 + 0.3 * v[far, None])
    img[near] = np.array([0.12, 0.2, 0.08]) * (0.8 + 0.4 * v[near, None])

    # Fine grain plus a few bright "windows" (point-like high-frequency detail).
    img += rng.normal(0.0, 0.025, size=img.shape)
    for _ in range(40):
        cx, cy = rng.integers(0, width), rng.integers(int(0.75 * height), height)
        img[cy : cy + 4, cx : cx + 3] = [1.0, 0.9, 0.6]

    return Image.fromarray(_to_uint8(img))


IMAGES = {
    "gradient_rgb.png": lambda path: gradient_rgb().save(path),
    "shapes_rgba.png": lambda path: shapes_rgba().save(path),
    "scene_portrait.heic": lambda path: scene_portrait().save(path, quality=85),
}


def main():
    force = "--force" in sys.argv[1:]
    OUT_DIR.mkdir(exist_ok=True)
    for name, write in IMAGES.items():
        path = OUT_DIR / name
        if path.exists() and not force:
            print(f"kept    {path.name}")
            continue
        write(path)
        print(f"wrote   {path.name}  ({path.stat().st_size / 1024:,.0f} KB)")


if __name__ == "__main__":
    main()
