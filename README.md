# Romulus Router

Romulus Router is a route finder that runs on a map. You give it a map of roads and footpaths (a file, or just the name of a place), click a start and an end point, and it draws the quickest walking route (plus a few alternatives).

The project also answers a question from our data-structures class: **how much faster is a k-d tree than a plain scan when finding the nearest road to a point?** The page can run both methods and show you the difference.

It has two parts:

- **A web page** (`Map Viewer.html`) that shows the map and the controls.
- **A C++ server** (in `server/`) that does the real work: building the road graph, finding the nearest road, finding routes, and timing everything.

> **Never used a terminal or GeoJSON before?** Follow [Quick start](#quick-start-5-minutes) exactly as written. You can learn the rest later.

---

## Contents

1. [What you need](#what-you-need)
2. [Quick start (5 minutes)](#quick-start-5-minutes)
3. [Using the page](#using-the-page)
4. [Use your own area](#use-your-own-area)
5. [Troubleshooting](#troubleshooting)
6. [What's in this repo](#whats-in-this-repo)
7. [How it works](#how-it-works)
8. [API reference](#api-reference)
9. [Glossary](#glossary)
10. [Credits and licenses](#credits-and-licenses)

---

## What you need

| You need | Why | How to check or get it |
|---|---|---|
| **macOS or Linux** (on Windows, use [WSL](https://learn.microsoft.com/windows/wsl/install)) | The server uses POSIX sockets, which Windows does not have natively. | |
| **A C++17 compiler** (`g++` or `clang++`) and **`make`** | To build the server. | Run `g++ --version` and `make --version`. If either is missing: on macOS run `xcode-select --install`; on Ubuntu/Debian run `sudo apt install build-essential`. |
| **A web browser** | To use the page. | Any modern browser. |
| **An internet connection** | The map tiles, place search and page libraries load from the internet. Loading an area by name (see [Use your own area](#use-your-own-area)) also downloads its roads from OpenStreetMap. | |
| **Python 3** *(optional)* | To run the small check script in [Option B](#option-b-build-a-file-from-overture-maps). | Run `python3 --version`. |

There is nothing else to install. The server uses only the C++ standard library.

---

## Quick start (5 minutes)

This runs the project on the included University of Rhode Island (URI) campus data.

**1. Get the code.** Clone the repository (or download it as a ZIP and unzip it), then open a terminal in the project folder.

**2. Build the server.**

```
cd server
make
```

This creates a program called `romulus_router`. It takes a few seconds. If you prefer CMake: `cmake -B build && cmake --build build`.

If you have built the project before, run `make clean && make` so you don't keep using an old build.

**3. Start the server.** Keep it in the `server` folder and run:

```
./romulus_router 8080 --html "../Map Viewer.html"
```

You should see:

```
Romulus Router server listening on http://127.0.0.1:8080
```

Leave this terminal window open. The server stops if you close it (or press `Ctrl+C`).

**4. Open the page.** In your browser, go to <http://localhost:8080>.

**5. Load the sample map.** Click **Load road network (.geojson)** in the left panel and choose `uri_roads_paths_places_boundary.geojson` from the project folder. The status line will tell you the network is ready (it also says how many footpath lines were loaded), and a purple outline will appear around the URI campus.

You're done. Continue with [Using the page](#using-the-page) to find your first route.

> **No file handy?** Skip step 5 and type a place name (for example `University of Rhode Island`) into the search box under the upload button, then click **Load**. See [Use your own area](#use-your-own-area).

> **Important:** always open the page through the server (`http://localhost:8080`). Double-clicking `Map Viewer.html` will show a map, but routing will not work because the page cannot reach the server.

**Shortcut:** `make run` (in the `server` folder) builds and starts the server in one step. Use `make run PORT=8081` to pick a different port.

---

## Using the page

1. **Load a map** (see step 5 above, or search for a place). You need to do this every time you restart the server, because the server keeps the roads in memory only.
2. **Choose a start point.** Click the **Start point** box and type at least 3 letters of a place (for example, a building name), then click a suggestion. You can also type coordinates like `41.4859, -71.5265`. Suggestions come from the places in your loaded data and from OpenStreetMap's Nominatim search, and only places inside the purple outline are offered.
3. **Choose an end point** the same way.
4. **Pick how the nearest road is found.** The two buttons are:
   - **K-d tree**: the fast method (the default).
   - **Alternative**: the brute-force method, which checks every road point one by one.
5. **Click Find route.** The quickest walking route is drawn on the map, and any alternatives are listed so you can click between them. The route you select is drawn thick and solid, and the others are thinner and faded. Each route shows its distance and walking time.
6. **Click Compare** to run *both* methods on the same trip and open a screen of charts that compare their speed. It also runs a benchmark of many random lookups and confirms the two methods always pick the same road point.
7. **Clear both points** starts over with new points.

What the page also does for you:

- **Walking routes use footpaths.** Routes follow roads *and* footpaths, paths and steps, at walking speed, so they can cut across a quad instead of going around it. This needs a map that contains footpaths (see [Use your own area](#use-your-own-area)); the roads-only sample has none. The page always asks for walking routes. Driving is available through the [API](#api-reference).
- **Timings change a little every time.** The microseconds shown on the Compare screen are measured live, so they vary from click to click. The nodes, route shape, distance and time stay the same. Don't worry if the numbers move.
- **Purple outline:** after you load a map, the page outlines the area your data covers and dims the map outside it. If your file has a boundary polygon, the outline follows it. If not, the outline is a rough shape around all the points. A map loaded by place name gets a clean rectangle showing exactly the area that was loaded. To turn the dimming off, set `DIM_OUTSIDE` to `false` in the "Outline of the loaded network" section of the page's script.

---

## Use your own area

There are two ways to get a map of another place. **Option A is the easiest** and needs no files.

### Option A: load a place by name (no files)

Under the upload button there is a search box, **Or search a place to load from OpenStreetMap**.

1. Type a place name, such as `University of Rhode Island` or `Providence College`, and press **Enter** or click **Load**.
2. If several places match, a short list appears. Click the one you meant. A single match loads right away.
3. The page downloads the roads, footpaths and named buildings in that area from OpenStreetMap, sends them to the server, and zooms to the result. A purple rectangle shows the area that was loaded.

Good to know:

- **Size limit.** The area is capped at about **2 miles across**, because a whole city is too much for the free OpenStreetMap servers and for the server's 64 MB request limit. A small place such as a campus loads its own outline (at least about half a mile across). A big place such as Boston loads only its **central 2 miles**. To load a different part, search for a specific neighborhood, campus or street instead.
- **What is loaded.** Roads, footpaths, paths, steps and cycleways (private and no-access ways are skipped), plus named buildings and amenities, which become searchable places. Roads that run beyond the area are trimmed at its edge.
- **It needs the internet**, and it can be slow or fail when the free OpenStreetMap servers are busy. The page tries three servers in turn.
- **It is not saved.** OpenStreetMap data is edited by volunteers all the time, so the same search can give slightly different results on different days. If you need identical data every time (for example for repeatable benchmark numbers), use a file (Option B, or the included samples).

### Option B: build a file from Overture Maps

This option makes a **GeoJSON file** in the same shape as the samples, using [Overture Maps](https://overturemaps.org/) data (free and covers most of the world). Use it when you want a file you can keep and reuse.

#### What the finished file looks like

It is one GeoJSON `FeatureCollection` with three kinds of feature, in this order:

| Part | Count in the footpath sample | Shape | What it is used for |
|---|---|---|---|
| Roads and footpaths | 876 (347 roads + 529 footpaths) | `LineString` | The routing graph, and the lines on the map. |
| Places | 260 | `Point` | Named places you can search for in the Start and End boxes. |
| Boundary | 1 | `Polygon` | The purple outline around your area. |

Only the roads are required for routing. The footpaths, places and boundary are optional extras. Without footpaths, walking routes follow roads only.

#### Step 1: Download the data from Overture Maps

You need up to three downloads: **transportation** (roads and footpaths), **places** (optional), and a **land-use layer** (optional, for a clean outline).

**Option 1: in your browser (no install).**

1. Go to <https://explore.overturemaps.org/>.
2. Pan and zoom to your area. **Keep it small** (a campus or a small town), since the server loads all of it into memory.
3. Open the **Inspect** tab and do this:
   - **Transportation** (`segment`) for roads and footpaths.
   - **Places** for named places.
   - Keep the preselected layers turned on
4. Click **Download Visible**. You get one GeoJSON file per layer plus a `metadata.json`.
5. For the boundary, find your polygon's ID first (see [Step 2](#step-2-optional-pick-the-boundary-polygon)), then turn off the other layers, expand **Base**, leave only **Land Use** checked, zoom so your whole area is on screen, and click **Download Visible** again.

**Option 2: command line (needs Python).**

```
pip install overturemaps
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=segment -o segments.geojson
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=place -o places.geojson
```

`WEST,SOUTH,EAST,NORTH` are the four edges of a box around your area, as longitude/latitude numbers. If `pip` is "not recognized", Python is not installed or not on your PATH. On Windows try `py -m pip install overturemaps`, or install Python from python.org and tick "Add python.exe to PATH".

> The raw downloads are **not** ready to use. Overture's properties are much more detailed than the router needs. Step 3 turns them into the simple format below.

#### Step 2 (optional): Pick the boundary polygon

The outline needs **one** polygon for your area, such as a campus, town, or park. Skip this step if a rough outline is fine.

1. In Overture Explorer, click the shaded area you want. The page address changes to something like `...?feature=base.land_use.06ea2b60-d724-34ca-bdc1-40ed716559c9#...`.
2. The long code after `base.land_use.` is the polygon's **ID**. Copy it. (This example is the URI campus polygon used in the sample files.)

Do **not** load the whole land-use download into the page. It contains hundreds of polygons (grass, pitches, lots...) and you would get a pile of outlines. You only want the one with your ID.

Other kinds of areas, such as a town or county, live in the **Divisions** layer instead of Base. The idea is the same: click the area, copy its ID, download that layer.

#### Step 3: Combine the files into one

Use the following method.

##### Easiest: ask Claude to do it

1. Go to [claude.ai](https://claude.ai) and start a new chat.
2. Upload your downloaded files (roads, places, and the Land Use file).
3. Paste this message. **Replace `PASTE-THE-ID-HERE` with your polygon ID from Step 2** (or delete the Boundary bullet if you skipped Step 2):

   > I downloaded these files from Overture Maps: a Transportation (segment) file, a Places file, and a Base > Land Use file. Please combine them into one GeoJSON file for my Romulus Router project, in the same format as `uri_roads_paths_places_boundary.geojson`:
   >
   > - **Roads and footpaths:** keep the drivable `LineString` segments (classes `motorway`, `trunk`, `primary`, `secondary`, `tertiary`, `residential`, `unclassified`, `service`, `living_street`) **and** the walking paths (classes `footway`, `pedestrian`, `path`, `steps`, `cycleway`). Remove ferries and roads closed to cars. Give each segment only these properties: `highway` and `class` (the same class value), `overture_id`, `name` (only if it has one), `maxspeed` (only if known, as `"25 mph"` for miles per hour or a bare number for km/h), and `oneway: "yes"` (only for one-way roads).
   > - **Places:** keep them as `Point` features with only `name`, `kind` set to `"place"`, and `address` (only if known).
   > - **Boundary:** from the Land Use file, take only the single polygon with ID `PASTE-THE-ID-HERE` and keep it exactly as downloaded, including its `id`.
   > - Put the features in this order: roads and footpaths, places, then the boundary polygon. Save it as `network_with_boundary.geojson`, and tell me how many of each feature are in it.

   For a driving-only file, delete the "and the walking paths" part of the first bullet.

4. Download the file Claude gives you. Check that the counts it reports look sensible for your area (for example, hundreds of roads for a campus, not zero).

#### Step 4: Check the file, then load it

Optional but recommended. This prints how many of each feature your file has:

```python
import json, collections
d = json.load(open("your_file.geojson"))
print(collections.Counter(f["geometry"]["type"] for f in d["features"]))
print(collections.Counter(f["properties"].get("class") for f in d["features"] if f["geometry"]["type"] == "LineString"))
```

For `uri_roads_paths_places_boundary.geojson` you should see `LineString: 876`, `Point: 260`, `Polygon: 1`, and classes such as `footway: 489`. For the roads-only `uri_roads_places_boundary.geojson` you should see `LineString: 265`, `Point: 217`, `Polygon: 1`. If there are no `LineString`s, there is nothing to route on.

Start the server as in the [Quick start](#quick-start-5-minutes), then use **Load road network (.geojson)** to load your file. The server ignores points and polygons when it builds the road graph, so leaving the places and boundary in the file is safe. When it skips a polygon, it says so in the status line. If your file has footpaths, the status line also says how many footpath lines were loaded.

---

## Troubleshooting

| What you see | What it means and how to fix it |
|---|---|
| `make: command not found` or `g++: command not found` | The build tools are missing. macOS: `xcode-select --install`. Ubuntu/Debian: `sudo apt install build-essential`. |
| `Failed to bind 127.0.0.1:8080` | Something else is already using port 8080. Start the server on another port, for example `./romulus_router 8081 --html "../Map Viewer.html"`, and open `http://localhost:8081`. |
| Browser says it can't connect to `localhost:8080` | The server isn't running. Start it again and leave its terminal open. |
| The page shows "Could not open the HTML page at: ..." | The `--html` path is wrong. Run the command from inside the `server` folder, with `"../Map Viewer.html"` exactly as shown (keep the quotes, because the file name has a space). |
| The map is blank or has no streets | The map tiles load from the internet. Check your connection. |
| Loading a file or finding a route does nothing | You probably opened the page by double-clicking the HTML file. Open `http://localhost:8080` instead. |
| "No road network loaded yet. Upload a GeoJSON file first." | The server has no roads. Load a file or search for a place. You must reload every time you restart the server. |
| "The file is not valid JSON" | The file isn't valid GeoJSON. Re-download it, or re-run the combine step. |
| "No route exists between these points on the loaded network." | The two points snapped onto roads or paths that aren't connected to each other (for example a separate parking lot, or a footpath that doesn't join the road). Choose different points. |
| Routes follow roads and never use footpaths | Check the status line after loading: it should say "Loaded N footpath line(s)". If it doesn't, the file has no footpaths (the roads-only sample doesn't), or you are running an old server build, so run `make clean && make` and restart. |
| "Could not download from OpenStreetMap..." | The free OpenStreetMap servers were busy, slow or unreachable. Check your connection, wait a minute, or search a smaller place. Dense downtowns can be too big even at 2 miles. |
| "No place found for ..." | OpenStreetMap's search didn't recognise the name. Try a more specific or differently spelled name. |
| A big city only loaded part of the city | That is expected. Areas are capped at about 2 miles across. Search a specific neighborhood to load another part. |
| The timings on the Compare screen change every click | They are measured live, so they vary a little each time. The route and distances don't change. |
| Search suggestions don't appear | Type at least 3 characters, and wait a second between searches. Place search needs the internet. |

---

## What's in this repo

```
Map Viewer.html                          the web page
uri_roads_paths_places_boundary.geojson  sample data: URI campus, roads + footpaths
uri_roads_places_boundary.geojson        sample data: URI campus, roads only
server/
  main.cpp                               the server: endpoints, routing, benchmark
  graph.hpp                              road graph, k-d tree, brute force, routing
  geo.hpp                                distance, speed and walking helper functions
  json.hpp                               a small built-in JSON reader/writer
  http.hpp                               a small built-in web server
  Makefile, CMakeLists.txt               two ways to build
```

Both samples cover the University of Rhode Island campus.

- **`uri_roads_paths_places_boundary.geojson`** (recommended): 876 lines (347 drivable roads and 529 footpaths: 489 footways, 25 paths, 12 steps and 3 cycleways), 260 named places and the campus boundary polygon, 1,137 features in total. It builds a graph of 4,404 nodes and 10,226 edges.
- **`uri_roads_places_boundary.geojson`**: 265 drivable roads, 217 named places and the boundary polygon, 483 features in total. It builds a graph of 1,333 nodes and 2,940 edges. It has no footpaths, so walking routes follow roads only.

The roads-only file was converted from Overture Maps, release `2026-10-21.0`.

---

## How it works

In plain words: when you load a map, the server turns it into a **graph**, which is a network of points (where roads and paths meet or bend) joined by lines (road and path pieces). When you click a spot on the map, the server finds the **nearest point on that network**, then searches the graph for the quickest way between your two points.

| Part | What it does |
|---|---|
| **Graph builder** | Turns the loaded GeoJSON into a routing graph. Each `LineString` vertex becomes a node (endpoints that coincide are merged into one intersection); consecutive vertices become edges. Edge length is the great-circle (haversine) distance. Each edge stores a driving time (from the road's `maxspeed`, falling back to a default speed for its road class), a walking time, and flags saying whether cars and walkers may use it. `oneway: yes` roads are one-way for cars only. Polygons and points are ignored for routing. |
| **k-d tree** | A balanced 2-D k-d tree over the nodes (built once and timed). It finds the nearest node in O(log n) time on average, which is how a clicked point is snapped onto the network. |
| **Brute force** | The O(n) baseline: check every node. It always returns the same node as the k-d tree, so the two can be compared directly. |
| **Routing** | The route search over travel time finds the quickest route. Alternatives come from re-running it with the previous route's edges penalised, then removing duplicates and ranking the results. |
| **Benchmark** | Runs many random nearest-node queries through both methods, reports total and average time and the speedup, and confirms the two methods agree on every query. |

The page does all the drawing. Every graph, k-d tree, snapping, routing and timing calculation happens in C++, so the comparison is measured fairly. When you load a place by name, the page downloads the data from OpenStreetMap and converts it to GeoJSON, then sends it to the server exactly as if you had uploaded a file.

### Walking and driving

The server can route two ways, chosen by the `mode` setting of `/api/route`:

| Mode | Roads | Footpaths, paths, steps, cycleways | One-way streets | Speed |
|---|---|---|---|---|
| **walk** (the default, and what the page uses) | Allowed, except motorways | Allowed | Ignored (walkers can go either way) | About 5 km/h (3 km/h on steps) |
| **drive** | Allowed | Not allowed | Respected | The road's `maxspeed`, or a default for its road class |

Known limitations:

- Footpaths only connect where two lines share an exact coordinate. If a path stops just short of a road in the data, the router can't cross there.
- Clicked points snap to the nearest node of any kind. In drive mode that can be a footpath node cars can't reach, which gives "No route exists". The page doesn't offer drive mode, so this only affects direct API calls.
- Walking speed is flat. Hills and surfaces aren't taken into account.

**Command line options** (all optional): `./romulus_router [port] [--host H] [--html PATH]`. The port defaults to `8080`, the host to `127.0.0.1` (this computer only), and the HTML path to `Map Viewer.html`.

**Libraries used by the page** (loaded from the internet, nothing to install): Leaflet, MapLibre GL JS with its Leaflet plugin, and Turf.js. The map style is OpenFreeMap's "liberty" style.

---

## API reference

You don't need this to use the project. It is here for anyone who wants to call the server directly.

| Endpoint | Request | Response |
|---|---|---|
| `GET /` | none | The `Map Viewer.html` page. |
| `POST /api/graph` | raw GeoJSON text | `{ nodes, edges, kdTreeBuildMs, speedInfoFound, warnings[] }` (warnings mention ignored polygons and the number of footpath lines loaded) |
| `POST /api/route` | `{ start:{lat,lng}, end:{lat,lng}, method, alternatives, repeat, mode }`. `mode` is `"walk"` (default) or `"drive"`. | per-method `run` objects with `snappedStart/End`, `routes[]`, and timings; `method: "both"` adds `lookupSpeedup` and `sameResult` |
| `POST /api/benchmark` | `{ queries, seed }` | `{ queries, kdTreeTotalMs, kdTreeAvgMicros, bruteTotalMs, bruteAvgMicros, speedup, mismatches }` |

---

## Glossary

| Term | Meaning |
|---|---|
| **GeoJSON** | A text file format for map data. Roads and footpaths are `LineString`s, places are `Point`s, areas are `Polygon`s. |
| **Overture Maps** | A free, open map dataset that our sample road and place data comes from. |
| **OpenStreetMap (OSM)** | A free map of the world edited by volunteers. The page can download its roads and footpaths for a place you search. |
| **Overpass API** | A free service that returns OpenStreetMap data for a chosen area. The page uses it when you load a place by name. |
| **Nominatim** | OpenStreetMap's place-name search. It turns a name like "Providence College" into a location. |
| **Graph / node / edge** | A network. Nodes are points (intersections or bends in a road or path); edges are the pieces joining them. |
| **Snapping** | Moving the spot you clicked to the nearest point on the road network. |
| **k-d tree** | A data structure that splits points into regions so the nearest one can be found without checking all of them. |
| **Brute force** | Checking every possibility one by one. Simple, but slow on large data. |
| **Haversine distance** | The distance between two latitude/longitude points over the Earth's surface. |
| **Benchmark** | A timing test: run the same work many times and measure how long it takes. |

---

## Credits and licenses

The map and data come from open projects.

- Road data: [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, through Overture Maps (the sample files) or directly through the Overpass API (maps loaded by place name). OpenStreetMap data is available under the [ODbL](https://opendatacommons.org/licenses/odbl/).
- Places: Overture Maps (from Meta), and OpenStreetMap for maps loaded by place name.
- Land-use boundary: Overture Maps.
- Map tiles: [OpenFreeMap](https://openfreemap.org/), © [OpenMapTiles](https://www.openmaptiles.org/), data from OpenStreetMap.
- Place search: [Nominatim](https://nominatim.openstreetmap.org/) (OpenStreetMap). Please keep requests to about one per second.
- Road download: the [Overpass API](https://overpass-api.de/). Please don't load areas in rapid succession.
- Libraries: Leaflet, MapLibre GL JS, the MapLibre GL Leaflet plugin, and Turf.js.