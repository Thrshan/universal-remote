from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
screens = [
    ("32-bt-mouse-modes", "Choose mouse mode"),
    ("33-bt-mouse-keymap", "Mapped: OK / drag"),
    ("34-bt-mouse-drag", "Mapped: left held"),
    ("35-bt-mouse-pattern", "Patterns: organic motion"),
    ("36-bt-mouse-speed", "Saved movement speed"),
]
canvas = Image.new("RGB", (len(screens) * 218 + 20, 478), "#e9edf1")
draw = ImageDraw.Draw(canvas)
font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12)
draw.text((20, 12), "BLUETOOTH MOUSE  /  64 x 128 OLED  /  ACTUAL FIRMWARE RENDER", font=small, fill="#455361")
for index, (name, label) in enumerate(screens):
    x = 20 + index * 218
    draw.text((x, 44), label, font=font, fill="#18222d")
    frame = Image.open(root / "build_host" / (name + ".pbm")).convert("RGB")
    canvas.paste(frame.resize((192, 384), Image.Resampling.NEAREST), (x, 69))
    draw.rectangle((x - 1, 68, x + 192, 453), outline="#596574")
path = root / "docs" / "bluetooth-mouse-preview.png"
canvas.save(path)
print(path)
