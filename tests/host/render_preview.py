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
    ("15-bluetooth", "Bluetooth keyboard"), ("16-wifi", "WiFi placeholder"),
    ("18-remove", "Remove remote"), ("13-home", "Home with remote"),
    ("19-bt-type", "Bluetooth device type"), ("20-bt-remote", "BT remote mode"),
    ("21-bt-connected", "BT settings while connected"), ("22-bt-map", "Fire TV key map"),
    ("23-bt-confirm", "Pair new / Unpair"),
    ("24-bt-presets", "Choose keyboard preset"),
    ("25-bt-media", "Media control preset"),
    ("26-bt-custom-picker", "Custom: select physical key"),
    ("27-bt-custom-modifiers", "Custom: Ctrl + Shift + Alt"),
    ("28-bt-standard-keys", "Standard keys: two columns"),
    ("29-bt-text-picker", "Text: select physical key"),
    ("30-bt-text-editor", "Assign text to a key"),
    ("31-bt-text-limit", "64-character text limit"),
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
sheet([names[i] for i in (18, 14, 20, 19, 21, 22)], 3, root / "docs" / "bluetooth-preview.png")
sheet(names[23:], 4, root / "docs" / "bluetooth-presets-preview.png")
print(root / "docs" / "ui-preview.png")
