# 144 Hz unit animation archive

`Jw2_09_144hz.rfa` is an offline-generated, presentation-only companion to
the original `Jw2_09.trc`. Archive format version 2 stores eleven hard-pixel
intermediate poses for each retained transition. Twelve pose phases exactly
represent the phase locations required by the 720 Hz common clock of 60 Hz
source presentation and 144 Hz output (`144 / 60 = 12 / 5`).

The game simulation itself normally advances every 45 ms (or slower according
to game speed), not every 60 Hz display refresh. Runtime therefore selects from
the twelve-phase bank using the real simulation-transition alpha. At the
default 45 ms interval, a 144 Hz display typically observes phases
`0, 2, 4, 6, 7, 9, 11, 12`.

Image groups with directional rows are recovered from the original definition
frame table at `0x140c` and the one-based row table at `0x2248`. They are never
treated as a flat resource-index cycle. For BuildMan group 0 this removes the
manufactured row-boundary transitions `2->3`, `5->6`, `8->9`, `11->12`, and
`14->0`. Different direction rows are never interpolated with one another;
turning selects the exact original pose for the new view.

The all-unit generator renders each original strip to a common, original-scale
canvas and runs FFmpeg `minterpolate` with motion-compensated, bidirectional
AOBMC independently on RGB, body alpha, and token-1 ground-shadow masks. RGB
uses a black matte. Both masks are thresholded back to binary, and opaque body
pixels are requantized without dithering to colors used by that original
animation strip. The runtime archive therefore contains only integer
coordinates and original palette tokens: there is no runtime image generation,
blur, antialiasing, semi-transparent edge, or colored matte.

`Jw2_09.trc` remains authoritative for endpoints and palettes. This archive
never changes simulation frames, gameplay RNG, unit commands, or P2P
checksums. If it is missing, malformed, or does not match the source TRC, the
renderer draws the exact current TRC endpoint.

Generate the complete all-unit archive (the cache makes the long offline job
restartable):

```powershell
python ranker_reconstructed_code\tools\animation_144hz\generate_all_units_ffmpeg_144hz.py `
  --archive RankerOCPV_Win\Jw2_09.trc `
  --output ranker_reconstructed_code\assets\animation_144hz\Jw2_09_144hz.rfa `
  --manifest ranker_reconstructed_code\assets\animation_144hz\archive_manifest.json `
  --ffmpeg third_party\ffmpeg\ffmpeg-9.0.1-essentials_build\bin\ffmpeg.exe `
  --cache debug_artifacts\animation_144hz\all_units_ffmpeg_cache `
  --keep-cache --jobs 8

python ranker_reconstructed_code\tools\animation_144hz\unit_sprite_assets.py `
  --archive RankerOCPV_Win\Jw2_09.trc validate-144hz `
  --input ranker_reconstructed_code\assets\animation_144hz\Jw2_09_144hz.rfa
```

The build copies the validated companion next to `ranker_rebuild.exe` as
`Jw2_09_144hz.rfa`.
