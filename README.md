# Romulus Router

A map-based route finder that works with any road network you give it. Load a GeoJSON file of roads, pick a start and end point on the map, and the router finds routes between them. A C++ server will do the nearest-road search (k-d tree vs. brute force) and the route finding.

> **Status: work in progress.** The web page is done. The C++ server is not written yet, so finding routes and running the benchmark do not work yet.

## What's in this repo

| File | What it is |
|---|---|
| `Map Viewer.html` | The web page: map, place search, road-network upload, boundary outline, and the route and benchmark controls. |
| `uri_roads_places_boundary.geojson` | Sample data for the University of Rhode Island campus: 265 drivable roads, 217 named places, and the campus boundary polygon (483 features). Converted from Overture Maps, release `2026-10-21.0`. Upload it as the road network to try the router. |

Not written yet: the C++ server, its build file, and the libraries it will use.

## The web page (`Map Viewer.html`)

- **Map:** OpenFreeMap vector tiles (the "liberty" style), drawn with MapLibre GL through the Leaflet plugin.
- **Place search:** type at least 3 characters in the Start or End box. Suggestions come from [Nominatim](https://nominatim.openstreetmap.org/) (OpenStreetMap's place search), so you do not need to load a file to search. You can also type `lat, lng` coordinates.
- **Road network:** use "Load road network (GeoJSON)" to upload a road file. The page sends it to the server at `/api/graph`.
- **Boundary outline:** after you load a file, the page draws a purple outline around the outer edge of your data and dims the map outside it.
  - If the file contains a polygon (for example a campus or city boundary), the outline traces that polygon's outer edge. Overlapping polygons are merged into one shape.
  - If the file has no polygon, the outline is the convex hull of all the points. That is a rough shape and can cover empty land.
  - To turn the dimming off, set `DIM_OUTSIDE` to `false` in the "Outline of the loaded network" section of the page's script.
- **Routing and benchmark:** the page calls `/api/route` and `/api/benchmark`. These need the C++ server.
- **Internet needed:** the map tiles, place search, and the Leaflet, MapLibre and Turf.js libraries all load from the internet.

The page has to be opened through the server (for example `http://localhost:8080`), not by double-clicking the file, or its `/api/` calls have nowhere to go. *(Server details to be added.)*

## Getting the data for any place

You need two things from [Overture Maps](https://overturemaps.org/): the roads (and optionally named places), and, if you want a clean boundary outline, the one polygon that covers your area. The steps below work for any place Overture covers.

### Step 1: Roads and places

#### Option 1: Overture Explorer (no install)

1. Go to <https://explore.overturemaps.org/>.
2. Pan and zoom to the area you want. Keep it small, since the server will have to load all of it.
3. In the **Inspect** tab, turn on only the layers you need. For roads, that is **Transportation** (`segment`). For named places, add **Places**.
4. Use **Download Visible**. You get one GeoJSON file per layer, plus a `metadata.json` that records the box and release.

#### Option 2: command line (needs Python)

```
pip install overturemaps
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=segment -o segments.geojson
```

`--type=place` downloads places. `WEST,SOUTH,EAST,NORTH` is the box around your area.

If `pip` is "not recognized", Python is not installed or not on your PATH. On Windows, try `py -m pip install overturemaps`, or install Python from python.org and tick "Add python.exe to PATH".

### Step 2: The boundary polygon (optional, for a clean outline)

The outline needs one polygon for your area, such as a campus, a town, or a park. Skip this step if a rough convex-hull outline is fine.

1. **Find the polygon's ID.** In Overture Explorer, click the shaded area you want (a campus, for example). The page address changes to something like `...?feature=base.land_use.06ea2b60-d724-34ca-bdc1-40ed716559c9#...`. The long code after `base.land_use.` is the ID. Copy it.
2. **Download the land-use layer.** In the **Inspect** tab, expand **Base**, leave only **Land Use** checked, zoom so your whole area is on screen, and use **Download Visible**.
   - This file holds *every* land-use polygon in view (hundreds of them: grass, pitches, residential lots, and so on). Do not load it into the page directly, or every polygon gets outlined and the result looks like a pile of shapes.
3. **Pull out the one polygon and add it to your roads file** (see Step 3).

Other kinds of areas, such as a town or county, live in the **Divisions** layer instead of Base. The same idea applies: click the area to get its ID, then download that layer.

### Step 3: Combine into one file

The page loads a single file, so put the roads, places, and the one boundary polygon together. This Python script does it. Edit the three file names and the ID at the top:

```python
import json

ID = "PASTE-THE-ID-HERE"
roads_file = "uri_roads_and_places.geojson"   # your roads (and places) file
land_file  = "land_use.geojson"                # the Land Use download from Step 2
out_file   = "network_with_boundary.geojson"

roads = json.load(open(roads_file))
land  = json.load(open(land_file))

shape = [f for f in land["features"] if f.get("id") == ID]
assert shape, "ID not found in the land-use file. Check the ID and the file."

roads["features"] += shape
json.dump(roads, open(out_file, "w"))
print(len(roads["features"]), "features written to", out_file)
```

Load the result with "Load road network (GeoJSON)". The page finds the polygon and outlines it.

Heads up: the routing server has not been written yet, so it is untested whether it will ignore the polygon feature. If it errors on it, the planned fix is a separate "Load boundary" file box that never sends the polygon to the server.

### Converting for routing

The raw Overture `segment` file is not ready to use for routing, so the sample file was converted:

- **Kept:** only drivable roads, meaning the classes `motorway`, `trunk`, `primary`, `secondary`, `tertiary`, `residential`, `unclassified`, `service`, and `living_street`.
- **Removed:** footpaths, steps, cycleways, ferry lines, and roads closed to cars.
- **Added** simple properties on each road:

| Property | Meaning |
|---|---|
| `highway` and `class` | Road class (for example `residential`). |
| `name` | Street name, when it has one. |
| `maxspeed` | Speed limit, when known. `"25 mph"` is miles per hour, and a bare number is km/h. |
| `oneway` | `"yes"` when you can only drive in the direction the line is drawn. |
| `overture_id` | The road's Overture ID. |

- **Places** (included in the sample files) are points with `name`, `kind`, and, when known, `category` and `address`.

In the included sample file only 8 of the 265 roads have a speed limit and 14 are one-way, so travel time may fall back to distance for most roads. The conversion script is not in the repo yet, so for a new area the raw `segment` download from Step 1 still needs this conversion before it will work for routing. *(To add.)*

## Credits and licenses

The map and data come from open projects. Keep their credits in any published version, and check each project's license page for the exact terms.

- Road data: [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, through Overture Maps.
- Places: Overture Maps (from Meta).
- Land-use boundary: Overture Maps.
- Map tiles: [OpenFreeMap](https://openfreemap.org/), © [OpenMapTiles](https://www.openmaptiles.org/), data from OpenStreetMap.
- Place search: Nominatim (OpenStreetMap). Please keep requests to about one per second.
- Libraries: Leaflet, MapLibre GL JS, the MapLibre GL Leaflet plugin, and Turf.js.

## To do

- [ ] Write the C++ server (k-d tree, brute-force search, route finding, benchmark)
- [ ] Add a build file and run instructions
- [ ] Add the road-data conversion script
- [ ] Document the `/api/graph`, `/api/route`, and `/api/benchmark` formats
- [ ] Check that the server ignores the boundary polygon, or add a separate "Load boundary" file box