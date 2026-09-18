# Offline map tile generation

`generate_osm_tiles.py` creates a small, bundled Shenzhen seed map in the
LVGL v8 RGB565 format used by the bike-computer map page. It queries raw
OpenStreetMap vector data and renders locally; it does not download raster
tiles from the OpenStreetMap tile service.

Example:

```powershell
python tools/generate_osm_tiles.py `
  --output disk/MAP `
  --zoom 15 16 `
  --latitude 22.543096 `
  --longitude 114.057865
```

The generated map is a Produced Work based on OpenStreetMap data. Keep the
on-screen `Map data: OpenStreetMap` attribution and the repository license
notice when redistributing it. A TF card remains the intended medium for a
full regional map because the internal filesystem is only 4 MiB.
