# WizardNet UI tool sprites

These PNG files were generated without resampling from the original
`tools.png` atlas. The atlas is intentionally not distributed after extraction.
To regenerate the sprites, temporarily place the same atlas at
`resources/tools.png` and run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/extract_ui_tools.ps1
```

`manifest.json` records every sprite's source bounds, padded crop rectangle,
intended family/state, and encoded SHA-256. Runtime files are copied to
`media/ui/tools` beside `ranker_rebuild.exe`.

The atlas uses partial alpha. Load these resources through
`ranker_ui_png_resource`, not the opaque lobby-background bitmap loader.
