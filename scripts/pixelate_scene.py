"""Pack generated transparent musicians into six equal cells and reduce the scene palette."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / 'assets/ui'
source = Image.open(root / 'rank-sprites-source.png').convert('RGBA')
alpha = source.getchannel('A')
occupied = [alpha.crop((x, 0, x+1, source.height)).getextrema()[1] > 128
            for x in range(source.width)]
runs = []
start = None
for x, visible in enumerate(occupied + [False]):
    if visible and start is None:
        start = x
    if not visible and start is not None:
        runs.append((start, x))
        start = None
if len(runs) != 6:
    raise ValueError(f'Expected six separated musicians, found {len(runs)}')
atlas = Image.new('RGBA', (576, 192))
for index, (left, right) in enumerate(runs):
    sprite = source.crop((left, 0, right, source.height))
    sprite = sprite.crop(sprite.getbbox())
    sprite.thumbnail((90, 174), Image.Resampling.BOX)
    atlas.alpha_composite(sprite, (index*96+(96-sprite.width)//2, 180-sprite.height))
atlas.quantize(colors=48, method=Image.Quantize.FASTOCTREE).save(root / 'rank-sprites.png')
stage = Image.open(root / 'rank-stage-source.png').convert('RGB')
stage.resize((576, 192), Image.Resampling.BOX).quantize(colors=48).save(root / 'rank-stage.png')
