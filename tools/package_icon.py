"""Package the approved 3D PNG as a multi-resolution Windows ICO. Dev-only: Pillow."""
from pathlib import Path
from PIL import Image
root = Path(__file__).resolve().parents[1]
im = Image.open(root / "assets" / "icon-3d.png").convert("RGBA")
im.save(root / "src" / "app.ico", sizes=[(16,16),(20,20),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print("ICO packaged")
