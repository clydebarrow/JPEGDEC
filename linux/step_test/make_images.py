"""Write the JPEG files that step_test decodes, covering the layouts JPEGDEC handles."""

import sys
from pathlib import Path

from PIL import Image


def pattern(width: int, height: int, mode: str) -> Image.Image:
    """A picture with gradients, edges and fine detail, so every DCT coefficient is used."""
    img = Image.new("RGB", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            px[x, y] = (
                (x * 255) // max(1, width - 1),
                (y * 255) // max(1, height - 1),
                255 if ((x // 7) + (y // 5)) % 2 else (x * y) % 256,
            )
    return img.convert(mode)


def main(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    sizes = [(64, 48), (203, 157), (320, 240), (17, 9)]
    for width, height in sizes:
        rgb = pattern(width, height, "RGB")
        for subsampling, name in ((0, "444"), (1, "422"), (2, "420")):
            rgb.save(out / f"{name}_{width}x{height}.jpg", quality=85, subsampling=subsampling)
            rgb.save(
                out / f"{name}_rst_{width}x{height}.jpg",
                quality=85,
                subsampling=subsampling,
                restart_marker_blocks=3,
            )
        pattern(width, height, "L").save(out / f"gray_{width}x{height}.jpg", quality=85)
        rgb.save(out / f"prog_{width}x{height}.jpg", quality=85, progressive=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
