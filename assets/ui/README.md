# Rank roster

Generated with the built-in image generation tool on 2026-10-18, then reduced to
576 x 192 and 48 colours with `scripts/pixelate_roster.py`. The editor draws with
nearest-neighbour resampling. Both source and final atlas are retained here.

Prompt: Six equal vertical panels in a horizontal sprite atlas. Full-body original
military band characters, gritty late-1990s gothic action RPG class-selection
atmosphere. Left to right: Sergeant with tenor saxophone, Lieutenant with trumpet,
Captain with trombone, Major with tuba, Staff Sergeant with second tenor saxophone,
Colonel with second trumpet. Distinct dress uniforms and rank insignia, varied
faces and genders, no weapons. Charcoal wool, burgundy and olive, antique brass,
firelight, stone plinths, near-black background. Equal scale and baseline, complete
instruments and feet, no panel overlap. Crisp square pixels and dithering, no
text, logos or UI.

Ranks identify fixed instrument slots. The second sax shares the global sax voice;
explicit tenor details, baritone keys and airflow belong to the first sax only.
The second sax and trumpet start disabled. Each player has independent mixing and
routing. Existing factory presets reset additional players to disabled.

## Shared stage and idle animation

The current editor uses `rank-sprites.png` over `rank-stage.png`. Generated source
images are preserved in the corresponding `*-source.png` files. Run
`scripts/pixelate_scene.py` to repack the six isolated figures and reduce both
assets to 48 colours. Alpha is preserved; each figure has its own equal-width cell.

Sprite edit prompt: preserve the six existing military musicians, faces, uniforms,
instruments and order; remove all scenery, plinths and shadows; place full-body
figures at equal scale on a genuine transparent background, with clear separation.

Stage prompt: one continuous empty gothic rehearsal courtyard at night, ruined
vaulted hall, shared stone floor, two edge braziers, moonlight, dark uncluttered
centre for six musicians. Wide 3:1 composition, chunky pixel art and dithering,
48-colour charcoal, brown and olive palette. No people, instruments or panels.

Both images were produced with the built-in image generation tool. Idle breathing
and sway are UI transforms anchored at the feet, with independent phase per player.
Embers drift above the braziers. These are decorative animations, not note activity
indicators. Existing player toggles and accessibility controls remain interactive.

## TopBrass logo

`topbrass-logo.png` is the transparent header mark and source for the native app
icon. Generated with the built-in image generation tool on 2026-10-20.

Prompt: a small original pixel-art military brass-band insignia: front-facing
antique golden trumpet bell, two strong sergeant chevrons beneath it and one
four-point glint on the rim. Dark bronze shadows, restrained dithering, simple
silhouette readable at 32 pixels, crisp square edges and transparent padding.
No lettering, monogram, weapons, laurels, shield, frame or background.
