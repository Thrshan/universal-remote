from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[2]
work = root / "build_host" / "typography"
work.mkdir(parents=True, exist_ok=True)
fonts_source = (root / "managed_components/u8g2/csrc/u8g2_fonts.c").read_text()
fonts = ['u8g2_font_4x6_tf', 'u8g2_font_5x8_tf', 'u8g2_font_6x10_tf']
extracted = ['#include "u8g2.h"']
for name in fonts:
    start = fonts_source.index('const uint8_t ' + name + '[')
    end = fonts_source.index(';\n', start) + 2
    extracted.append(fonts_source[start:end])
(work / "fonts.c").write_text('\n'.join(extracted))
sources = [
    'tests/host/typography_preview.c', 'tests/host/host_support.c', 'tests/host/bt_fake.c',
    'components/app_core/app_state.c', 'components/app_core/remote_model.c',
    'components/app_core/app_storage.c', 'components/app_core/profiles/bt_keymap.c',
    'components/app_core/profiles/bt_mouse.c', 'components/app_core/profiles/bt_output.c',
    'components/ui/ui_nav.c', 'components/ui/ui_screens.c',
    'components/ui/ui_icons.c', 'components/ui/ui_common.c',
    'components/ui/screens/ui_home.c', 'components/ui/screens/ui_settings.c',
    'components/ui/screens/ui_remote.c', 'components/ui/screens/ui_edit.c',
    'components/ui/screens/ui_wizard.c', 'components/ui/screens/ui_bluetooth.c',
    'components/ui/screens/ui_ir.c',
    'build_host/typography/fonts.c',
] + [str(path) for path in (root / 'managed_components/u8g2/csrc').glob('*.c')
     if path.name != 'u8g2_fonts.c']
args = ['gcc', '-std=c11', '-O0', '-ffunction-sections', '-fdata-sections',
        '-Itests/host/include', '-Itests/host',
        '-Icomponents/app_core', '-Icomponents/app_core/profiles', '-Icomponents/bluetooth',
        '-Imain/drivers', '-Icomponents/ui/include', '-Icomponents/ui',
        '-Imanaged_components/u8g2/csrc'] + sources + ['-Wl,--gc-sections', '-o', 'build_host/typography/preview.exe']
subprocess.run(args, cwd=root, check=True)
subprocess.run([str(work / 'preview.exe')], cwd=root, check=True)

destination = root / 'docs' / 'typography'
destination.mkdir(exist_ok=True)
items = [('home-before', 'Home: before'), ('home-after', 'Home: after'),
         ('ir-before', 'Learn IR: before'), ('ir-after', 'Learn IR: after')]
canvas = Image.new('RGB', (1120, 594), '#e9edf1')
draw = ImageDraw.Draw(canvas)
font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 16)
small = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 12)
draw.text((20, 12), 'TEXT COMPARISON  /  64 x 128 NATIVE PIXELS  /  APPLIED LARGER TEXT', font=small, fill='#455361')
for i, (name, label) in enumerate(items):
    # Keep the original-font baseline captured before applying the approved style.
    if name.endswith('-before'):
        frame = Image.open(destination / (name + '.bmp')).convert('1')
    else:
        frame = Image.open(work / (name + '.pbm')).convert('1')
        frame.save(destination / (name + '.bmp'))
    x = 20 + i * 280
    draw.text((x, 44), label, font=font, fill='#18222d')
    canvas.paste(frame.convert('RGB').resize((256, 512), Image.Resampling.NEAREST), (x, 72))
canvas.save(destination / 'before-after.bmp')
canvas.save(destination / 'before-after.png')
print(destination / 'before-after.bmp')
