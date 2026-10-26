from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
out = root / "build_host"
names = [
    ("01-startup", "Startup"), ("02-empty", "Empty home"),
    ("17-home-list", "Browse remotes"), ("12-remote", "Remote mode"),
    ("03-connection", "Connection"), ("04-keyboard", "Name keyboard"),
    ("05-icons", "Choose icon"), ("06-icons", "More icons"),
    ("07-icons", "More icons"), ("08-learn-keys", "Choose key"),
    ("09-waiting", "Learn IR"), ("10-saved", "Saved command"),
    ("11-rerecord", "Replace command"), ("14-editor", "Edit remote"),
    ("15-bluetooth", "BT placeholder"), ("16-wifi", "WiFi placeholder"),
    ("18-remove", "Remove remote"), ("13-home", "Home with remote"),
]
font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12)

def sheet(items, columns, path):
    cell_w, cell_h = 218, 434
    canvas = Image.new("RGB", (columns * cell_w + 20, ((len(items) + columns - 1) // columns) * cell_h + 44), "#e9edf1")
    draw = ImageDraw.Draw(canvas)
    draw.text((20, 12), "UNIVERSAL REMOTE  /  64 x 128 OLED  /  ACTUAL FIRMWARE RENDER", font=small, fill="#455361")
    for i, (name, label) in enumerate(items):
        x = 20 + (i % columns) * cell_w
        y = 44 + (i // columns) * cell_h
        draw.text((x, y), label, font=font, fill="#18222d")
        frame = Image.open(out / (name + ".pbm")).convert("RGB").resize((192, 384), Image.Resampling.NEAREST)
        canvas.paste(frame, (x, y + 25))
        draw.rectangle((x - 1, y + 24, x + 192, y + 409), outline="#596574")
    canvas.save(path)

sheet(names, 6, out / "all-screens.png")
selected = [names[i] for i in (2, 3, 5, 6, 9, 10, 11, 14)]
sheet(selected, 4, root / "docs" / "ui-preview.png")
print(root / "docs" / "ui-preview.png")
