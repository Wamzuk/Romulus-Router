# Romulus Router

A map-based route finder for the University of Rhode Island area. The web page lets you search for a start and end point, load a road network (GeoJSON), and find routes. A C++ server will do the nearest-road search (k-d tree vs. brute force) and the route finding.

> **Status: work in progress.** The web page is done. The C++ server is not written yet, so finding routes and running the benchmark do not work yet.

## What's in this repo

| File | What it is |
|---|---|
| `Map_Viewer.html` | The web page: map, place search, road-network upload, and the route and benchmark controls. |
| `uri_roads.geojson` | Drivable roads around the URI campus, converted from Overture Maps (265 roads). |
| `uri_roads_and_places.geojson` | The same roads plus 217 named places (482 features in total). |

Not written yet: the C++ server, its build file, and the libraries it will use.

## The web page (`Map_Viewer.html`)

- **Map:** OpenFreeMap vector tiles (the "liberty" style), drawn with MapLibre GL through the Leaflet plugin.
- **Place search:** type at least 3 characters in the Start or End box. Suggestions come from [Nominatim](https://nominatim.openstreetmap.org/) (OpenStreetMap's place search), so you do not need to load a file to search. You can also type `lat, lng` coordinates.
- **Road network:** use "Load road network (GeoJSON)" to upload one of the road files. The page sends it to the server at `/api/graph`.
- **Routing and benchmark:** the page calls `/api/route` and `/api/benchmark`. These need the C++ server.
- **Internet needed:** the map tiles, place search, and the Leaflet and MapLibre libraries all load from the internet.

The page has to be opened through the server (for example `http://localhost:8080`), not by double-clicking the file, or its `/api/` calls have nowhere to go. *(Server details to be added.)*

## Getting the road data

The road files come from [Overture Maps](https://overturemaps.org/) (release `2026-10-21.0`).

### Option 1: Overture Explorer (no install)

1. Go to <https://explore.overturemaps.org/>.
2. Pan and zoom to the area you want. Keep it small (the URI test area was about 1.5 km wide).
3. Turn on only the layers you need. For roads, that is the transportation layer (`segment`). For named places, add `place`.
4. Use **Download Visible**. You get one GeoJSON file per layer, plus a `metadata.json` that records the box and release.

### Option 2: command line

```
pip install overturemaps
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=segment -o segments.geojson
```

`--type=place` downloads places. The URI test area used the box `-71.539,41.483,-71.519,41.493`.

### Converting to the files used here

The raw Overture `segment` file is not ready to use for routing, so it was converted:

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

- **Places** (in `uri_roads_and_places.geojson` only) are points with `name`, `kind`, and, when known, `category` and `address`.

Only 8 of the 265 roads have a speed limit and 14 are one-way, so travel time falls back to distance for most roads. The conversion script is not in the repo yet. *(To add.)*

## Credits and licenses

The map and data come from open projects. Keep their credits in any published version, and check each project's license page for the exact terms.

- Road data: [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, through Overture Maps.
- Places: Overture Maps (from Meta).
- Map tiles: [OpenFreeMap](https://openfreemap.org/), © [OpenMapTiles](https://www.openmaptiles.org/), data from OpenStreetMap.
- Place search: Nominatim (OpenStreetMap). Please keep requests to about one per second.
- Libraries: Leaflet, MapLibre GL JS, and the MapLibre GL Leaflet plugin.

## To do

- [ ] Write the C++ server (k-d tree, brute-force search, route finding, benchmark)
- [ ] Add a build file and run instructions
- [ ] Add the road-data conversion script
- [ ] Document the `/api/graph`, `/api/route`, and `/api/benchmark` formats