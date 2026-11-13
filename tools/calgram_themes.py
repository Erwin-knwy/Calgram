"""Calgram's Looks: the CRM's themes as Telegram Desktop themes.

Reads the CRM's CSS (frontend/src/styles/theme.css and themes.css in the
CalRecycles_Bot repo), and for every Look and mode recolors Telegram's
own Day or Night palette with that Look's colors:
  - grays become the Look's card / text / chrome shades,
  - Telegram blue becomes the Look's accent,
  - greens, reds and violets become its success, danger and info colors,
  - Telegram's green "sent" bubble becomes a wash of the accent.

Usage:
  python3 tools/calgram_themes.py <CalRecycles_Bot repo> [--preview <dir>]
Writes Telegram/Resources/calgram/<look>-<mode>.tdesktop-theme.
"""
import colorsys
import io
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / 'Telegram' / 'Resources'
OUT = RES / 'calgram'

LOOKS = [  # key, name, the mode the CRM shows first
    ('modern', 'Modern', 'light'),
    ('classic', 'Classic', 'light'),
    ('cyberpunk', 'Cyberpunk', 'dark'),
    ('gtavi', 'GTA VI', 'dark'),
    ('kurosawa', 'Kurosawa', 'light'),
    ('ca', 'CA', 'light'),
    ('lumiere', 'Lumiere', 'dark'),
]
NEEDED = ('bg', 'fg', 'card', 'muted', 'chrome', 'accent', 'accent-fg',
          'danger', 'success', 'info')


# ---- CRM CSS ------------------------------------------------------------

def css_blocks(css):
    """[(selector, {var: value})] for every rule with custom properties."""
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.S)
    out = []
    for sel, body in re.findall(r'([^{}]+)\{([^{}]*)\}', css):
        props = dict(re.findall(r'--([\w-]+)\s*:\s*([^;]+);', body))
        if props:
            out.append((' '.join(sel.split()), {k: v.strip() for k, v in props.items()}))
    return out


def look_colors(crm):
    css = ((crm / 'frontend/src/styles/theme.css').read_text()
           + (crm / 'frontend/src/styles/themes.css').read_text())
    blocks = css_blocks(css)
    result = {}
    for key, _name, first in LOOKS:
        base, light, dark = {}, {}, {}
        for sel, props in blocks:
            for part in (p.strip() for p in sel.split(',')):
                if part == f'html[data-ui="{key}"]':
                    base.update(props)
                elif part == f'html[data-ui="{key}"][data-theme="light"]':
                    light.update(props)
                elif part == f'html[data-ui="{key}"][data-theme="dark"]':
                    dark.update(props)
        modes = {first: dict(base)}
        other = 'dark' if first == 'light' else 'light'
        extra = dark if other == 'dark' else light
        if extra:
            modes[other] = {**base, **extra}
        for mode, colors in modes.items():
            hexes = {k: v for k, v in colors.items() if re.fullmatch(r'#[0-9a-fA-F]{6}', v)}
            if all(k in hexes for k in NEEDED):
                result[(key, mode)] = hexes
    return result


# ---- colors -------------------------------------------------------------

def parse_hex(value):
    v = value.lstrip('#')
    r, g, b = (int(v[i:i + 2], 16) / 255 for i in (0, 2, 4))
    a = int(v[6:8], 16) if len(v) == 8 else None
    return (r, g, b), a


def to_hex(rgb, alpha=None):
    out = '#' + ''.join(f'{round(max(0, min(1, c)) * 255):02x}' for c in rgb)
    return out + (f'{alpha:02x}' if alpha is not None else '')


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def hls(rgb):
    return colorsys.rgb_to_hls(*rgb)


def shift(target, light_delta):
    h, l, s = hls(target)
    return colorsys.hls_to_rgb(h, max(0, min(1, l + light_delta)), s)


# ---- Telegram palettes --------------------------------------------------

LINE = re.compile(r'^(\w+)\s*:\s*([^;]+);')


def read_palette(text):
    out = {}
    for line in text.splitlines():
        m = LINE.match(line.strip())
        if m:
            out[m.group(1)] = m.group(2).strip()
    return out


def resolve(palette, key, seen=()):
    value = palette[key]
    if value.startswith('#'):
        return value
    if value in seen or value not in palette:
        return None
    return resolve(palette, value, seen + (key,))


def base_palette(mode):
    # A copy of lib_ui/ui/colors.palette (Telegram's own defaults).
    default = read_palette((ROOT / 'tools' / 'telegram_default.palette').read_text())
    if mode == 'light':
        return default
    with zipfile.ZipFile(RES / 'night.tdesktop-theme') as z:
        night = read_palette(z.read('colors.tdesktop-theme').decode())
    return {**default, **night}


def recolor(palette, look, mode):
    c = {k: parse_hex(v)[0] for k, v in look.items()}
    bg_rgb = parse_hex(resolve(palette, 'windowBg'))[0]
    fg_rgb = parse_hex(resolve(palette, 'windowFg'))[0]
    acc_rgb = parse_hex(resolve(palette, 'windowBgActive'))[0]
    l_bg, l_fg, l_acc = hls(bg_rgb)[1], hls(fg_rgb)[1], hls(acc_rgb)[1]
    out = {}
    for key, value in palette.items():
        if not value.startswith('#'):
            out[key] = value  # a reference to another key: follows it
            continue
        rgb, alpha = parse_hex(value)
        h, l, s = hls(rgb)
        deg = h * 360
        # Chroma, not HLS saturation: Night's grays are faintly blue.
        chroma = max(rgb) - min(rgb)
        neutral = chroma < 0.16 or l > 0.94 or l < 0.06
        if neutral:
            t = (l - l_bg) / (l_fg - l_bg) if l_fg != l_bg else 0
            if t >= 0:
                new = mix(c['card'], c['fg'], min(t, 1))
            else:
                new = mix(c['card'], c['chrome'], min(-t * 3, 1))
            if ('FgActive' in key or key.startswith('activeButtonFg')
                    or key.endswith('ActiveFg')) and l > 0.85:
                new = c['accent-fg']
        elif 170 <= deg < 265:
            new = shift(c['accent'], l - l_acc)
        elif 65 <= deg < 170:
            new = mix(c['card'], c['accent'], 0.16) if l > 0.75 else shift(c['success'], (l - 0.5) * 0.5)
        elif deg >= 330 or deg < 20:
            new = shift(c['danger'], (l - 0.5) * 0.5)
        elif 265 <= deg < 330:
            new = shift(c['info'], (l - 0.5) * 0.5)
        else:
            new = rgb  # yellow/orange: stars, warnings - keep
        out[key] = to_hex(new, alpha)
    # Your own messages: a soft wash of the accent, readable with the
    # Look's text color in both modes (Telegram's own is pale green).
    dark = mode == 'dark'
    for key, amount in (('msgOutBg', 0.30 if dark else 0.16),
                        ('msgOutBgSelected', 0.42 if dark else 0.26)):
        if key in out and out[key].startswith('#'):
            out[key] = to_hex(mix(c['card'], c['accent'], amount))
    return out


def png_1x1(rgb):
    import struct
    import zlib
    raw = b'\x00' + bytes(round(x * 255) for x in rgb)
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def write_theme(path, palette, chat_bg):
    text = ''.join(f'{k}: {v};\n' for k, v in palette.items())
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('colors.tdesktop-theme', text)
        z.writestr('background.png', png_1x1(chat_bg))
    path.write_bytes(buf.getvalue())


def preview(path, palette, name):
    from PIL import Image, ImageDraw
    def col(key):
        return tuple(round(x * 255) for x in parse_hex(resolve(palette, key) or '#ff00ff')[0])
    im = Image.new('RGB', (420, 230), col('windowBg'))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 140, 230), fill=col('dialogsBg'))
    d.rectangle((0, 40, 140, 80), fill=col('dialogsBgActive'))
    d.text((8, 52), 'Office group', fill=col('dialogsNameFgActive'))
    d.text((8, 12), name, fill=col('dialogsNameFg'))
    d.text((8, 92), 'Other chat', fill=col('dialogsNameFg'))
    d.text((8, 108), 'last message', fill=col('dialogsTextFg'))
    d.rectangle((140, 0, 420, 230), fill=col('windowBg'))
    d.rectangle((150, 20, 330, 70), fill=col('msgInBg'))
    d.text((158, 26), 'Hanna', fill=col('msgInServiceFg'))
    d.text((158, 44), 'Incoming message', fill=col('historyTextInFg'))
    d.rectangle((230, 85, 410, 135), fill=col('msgOutBg'))
    d.text((238, 100), 'My message', fill=col('historyTextOutFg'))
    d.rectangle((150, 190, 410, 222), fill=col('historyComposeAreaBg'))
    d.text((158, 200), 'Write a message...', fill=col('historyComposeAreaFgService'))
    d.rectangle((330, 150, 410, 178), fill=col('activeButtonBg'))
    d.text((340, 158), 'Button', fill=col('activeButtonFg'))
    im.save(path)


def main():
    crm = Path(sys.argv[1])
    preview_dir = Path(sys.argv[sys.argv.index('--preview') + 1]) if '--preview' in sys.argv else None
    OUT.mkdir(parents=True, exist_ok=True)
    looks = look_colors(crm)
    names = {key: name for key, name, _ in LOOKS}
    for (key, mode), colors in sorted(looks.items()):
        palette = recolor(base_palette(mode), colors, mode)
        chat_bg = parse_hex(colors['bg'])[0]
        write_theme(OUT / f'{key}-{mode}.tdesktop-theme', palette, chat_bg)
        if preview_dir:
            preview_dir.mkdir(parents=True, exist_ok=True)
            preview(preview_dir / f'{key}-{mode}.png', palette, f'{names[key]} {mode}')
        print(f'{key}-{mode}')


if __name__ == '__main__':
    main()
