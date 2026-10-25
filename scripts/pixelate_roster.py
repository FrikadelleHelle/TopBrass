"""Prepare the six-column generated roster atlas for nearest-neighbour drawing."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = Image.open(root / 'assets/ui/rank-roster-source.png').convert('RGB')
source.resize((576, 192), Image.Resampling.BOX).quantize(colors=48).convert('RGB').save(
    root / 'assets/ui/rank-roster.png')
