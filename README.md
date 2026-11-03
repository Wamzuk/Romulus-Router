# Romulus Router

Romulus Router is a route finder that runs on a map. You give it a file of roads, click a start and an end point, and it draws the fastest route (plus a few alternatives).

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
| **An internet connection** | The map tiles, place search and page libraries load from the internet. | |
| **Python 3** *(optional)* | To run the small check script in [Use your own area](#use-your-own-area). | Run `python3 --version`. |

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

**5. Load the sample roads.** Click **Load road network (.geojson)** in the left panel and choose `uri_roads_places_boundary.geojson` from the project folder. The status line will tell you the network is ready, and a purple outline will appear around the URI campus.

You're done. Continue with [Using the page](#using-the-page) to find your first route.

> **Important:** always open the page through the server (`http://localhost:8080`). Double-clicking `Map Viewer.html` will show a map, but routing will not work because the page cannot reach the server.

**Shortcut:** `make run` (in the `server` folder) builds and starts the server in one step. Use `make run PORT=8081` to pick a different port.

---

## Using the page

1. **Load a road file** (see step 5 above). You need to do this every time you restart the server, because the server keeps the roads in memory only.
2. **Choose a start point.** Click the **Start point** box and type at least 3 letters of a place (for example, a building name), then click a suggestion. You can also type coordinates like `41.4859, -71.5265`.
3. **Choose an end point** the same way.
4. **Pick how the nearest road is found.** The two buttons are:
   - **K-d tree**: the fast method (the default).
   - **Alternative**: the brute-force method, which checks every road point one by one.
5. **Click Find route.** The fastest route is drawn on the map, and any alternatives are listed so you can click between them. Each route shows its distance and travel time.
6. **Click Compare** to run *both* methods on the same trip and open a screen of charts that compare their speed. It also runs a benchmark of many random lookups and confirms the two methods always pick the same road point.
7. **Clear both points** starts over with new points.

What the page also does for you:

- **Place search** works even before you load a file. It uses OpenStreetMap's Nominatim service (please don't search more than about once per second).
- **Purple outline:** after you load a file, the page outlines the area your data covers and dims the map outside it. If your file has a boundary polygon, the outline follows it. If not, the outline is a rough shape around all the points. To turn the dimming off, set `DIM_OUTSIDE` to `false` in the "Outline of the loaded network" section of the page's script.

---

## Use your own area

The sample file only covers the URI campus. To use another place, you need a **GeoJSON file** in the same shape as the sample. This section shows how to make one from [Overture Maps](https://overturemaps.org/) data (free and covers most of the world).

### What the finished file looks like

It is one GeoJSON `FeatureCollection` with three kinds of feature, in this order:

| Part | Count in the sample | Shape | What it is used for |
|---|---|---|---|
| Roads | 265 | `LineString` | The routing graph, and the road lines on the map. |
| Places | 217 | `Point` | Named places you can search for in the Start and End boxes. |
| Boundary | 1 | `Polygon` | The purple outline around your area. |

Only the roads are required for routing. The places and the boundary are optional extras.

### Step 1: Download the data from Overture Maps

You need up to three downloads: **roads**, **places** (optional), and a **land-use layer** (optional, for a clean outline).

**Option A: in your browser (no install).**

1. Go to <https://explore.overturemaps.org/>.
2. Pan and zoom to your area. **Keep it small** (a campus or a small town), since the server loads all of it into memory.
3. Open the **Inspect** tab and do this:
   - **Transportation** (`segment`) for roads.
   - **Places** for named places.
   - Keep the preselected layers turned on
4. Click **Download Visible**. You get one GeoJSON file per layer plus a `metadata.json`.
5. For the boundary, find your polygon's ID first (see [Step 2](#step-2-optional-pick-the-boundary-polygon)), then turn off the other layers, expand **Base**, leave only **Land Use** checked, zoom so your whole area is on screen, and click **Download Visible** again.

**Option B: command line (needs Python).**

```
pip install overturemaps
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=segment -o segments.geojson
overturemaps download --bbox=WEST,SOUTH,EAST,NORTH -f geojson --type=place -o places.geojson
```

`WEST,SOUTH,EAST,NORTH` are the four edges of a box around your area, as longitude/latitude numbers. If `pip` is "not recognized", Python is not installed or not on your PATH. On Windows try `py -m pip install overturemaps`, or install Python from python.org and tick "Add python.exe to PATH".

> The raw downloads are **not** ready to use. Overture's properties are much more detailed than the router needs. Step 3 turns them into the simple format below.

### Step 2 (optional): Pick the boundary polygon

The outline needs **one** polygon for your area, such as a campus, town, or park. Skip this step if a rough outline is fine.

1. In Overture Explorer, click the shaded area you want. The page address changes to something like `...?feature=base.land_use.06ea2b60-d724-34ca-bdc1-40ed716559c9#...`.
2. The long code after `base.land_use.` is the polygon's **ID**. Copy it. (This example is the URI campus polygon used in the sample file.)

Do **not** load the whole land-use download into the page. It contains hundreds of polygons (grass, pitches, lots...) and you would get a pile of outlines. You only want the one with your ID.

Other kinds of areas, such as a town or county, live in the **Divisions** layer instead of Base. The idea is the same: click the area, copy its ID, download that layer.

### Step 3: Combine the files into one

Use the following method.

#### Easiest: ask Claude to do it

1. Go to [claude.ai](https://claude.ai) and start a new chat.
2. Upload your downloaded files (roads, places, and the Land Use file).
3. Paste this message. **Replace `PASTE-THE-ID-HERE` with your polygon ID from Step 2** (or delete the Boundary bullet if you skipped Step 2):

   > I downloaded these files from Overture Maps: a Transportation (segment) file, a Places file, and a Base > Land Use file. Please combine them into one GeoJSON file for my Romulus Router project, in the same format as `uri_roads_places_boundary.geojson`:
   >
   > - **Roads:** keep only drivable `LineString` segments (classes `motorway`, `trunk`, `primary`, `secondary`, `tertiary`, `residential`, `unclassified`, `service`, `living_street`). Remove footpaths, steps, cycleways, ferries and roads closed to cars. Give each road only these properties: `highway` and `class` (the same road class), `overture_id`, `name` (only if it has one), `maxspeed` (only if known, as `"25 mph"` for miles per hour or a bare number for km/h), and `oneway: "yes"` (only for one-way roads).
   > - **Places:** keep them as `Point` features with only `name`, `kind` set to `"place"`, and `address` (only if known).
   > - **Boundary:** from the Land Use file, take only the single polygon with ID `PASTE-THE-ID-HERE` and keep it exactly as downloaded, including its `id`.
   > - Put the features in this order: roads, places, then the boundary polygon. Save it as `network_with_boundary.geojson`, and tell me how many of each feature are in it.

4. Download the file Claude gives you. Check that the counts it reports look sensible for your area (for example, hundreds of roads for a campus, not zero).

### Step 4: Check the file, then load it

Optional but recommended. This prints how many of each feature your file has:

```python
import json, collections
d = json.load(open("your_file.geojson"))
print(collections.Counter(f["geometry"]["type"] for f in d["features"]))
```

For the sample file you should see `LineString: 265`, `Point: 217`, `Polygon: 1`. If there are no `LineString`s, there is nothing to route on.

Start the server as in the [Quick start](#quick-start-5-minutes), then use **Load road network (.geojson)** to load your file. The server ignores points and polygons when it builds the road graph, so leaving the places and boundary in the file is safe. When it skips a polygon, it says so in the status line.

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
| "No road network loaded yet. Upload a GeoJSON file first." | The server has no roads. Load a file. You must reload it every time you restart the server. |
| "The file is not valid JSON" | The file isn't valid GeoJSON. Re-download it, or re-run the combine step. |
| "No route exists between these points on the loaded network." | The two points snapped onto roads that aren't connected (for example a separate parking lot). Choose different points. |
| Search suggestions don't appear | Type at least 3 characters, and wait a second between searches. Place search needs the internet. |

---

## What's in this repo

```
Map Viewer.html                      the web page
uri_roads_places_boundary.geojson    sample data: URI campus
server/
  main.cpp                           the server: endpoints, routing, benchmark
  graph.hpp                          road graph, k-d tree, brute force, Dijkstra
  geo.hpp                            distance and speed helper functions
  json.hpp                           a small built-in JSON reader/writer
  http.hpp                           a small built-in web server
  Makefile, CMakeLists.txt           two ways to build
```

The sample data covers the University of Rhode Island campus: 265 drivable roads, 217 named places, and the campus boundary polygon (483 features in total). It was converted from Overture Maps, release `2026-10-21.0`.

---

## How it works

In plain words: when you upload roads, the server turns them into a **graph**, which is a network of points (where roads meet or bend) joined by lines (road pieces). When you click a spot on the map, the server finds the **nearest point on that network**, then searches the graph for the fastest way between your two points.

| Part | What it does |
|---|---|
| **Graph builder** | Turns the uploaded GeoJSON into a routing graph. Each `LineString` vertex becomes a node (endpoints that coincide are merged into one intersection); consecutive vertices become edges. Edge length is the great-circle (haversine) distance. Travel time uses the road's `maxspeed`, falling back to a default speed for its road class. `oneway: yes` roads get a single directed edge. Polygons and points are ignored for routing. |
| **k-d tree** | A balanced 2-D k-d tree over the nodes (built once and timed). It finds the nearest node in O(log n) time on average, which is how a clicked point is snapped onto the network. |
| **Brute force** | The O(n) baseline: check every node. It always returns the same node as the k-d tree, so the two can be compared directly. |
| **Routing** | Dijkstra's algorithm over travel time finds the fastest route. Alternatives come from re-running it with the previous route's edges penalised, then removing duplicates and ranking the results. |
| **Benchmark** | Runs many random nearest-node queries through both methods, reports total and average time and the speedup, and confirms the two methods agree on every query. |

The page does all the drawing. Every graph, k-d tree, snapping, routing and timing calculation happens in C++, so the comparison is measured fairly.

**Command line options** (all optional): `./romulus_router [port] [--host H] [--html PATH]`. The port defaults to `8080`, the host to `127.0.0.1` (this computer only), and the HTML path to `Map Viewer.html`.

**Libraries used by the page** (loaded from the internet, nothing to install): Leaflet, MapLibre GL JS with its Leaflet plugin, and Turf.js. The map style is OpenFreeMap's "liberty" style.

---

## API reference

You don't need this to use the project. It is here for anyone who wants to call the server directly.

| Endpoint | Request | Response |
|---|---|---|
| `GET /` | none | The `Map Viewer.html` page. |
| `POST /api/graph` | raw GeoJSON text | `{ nodes, edges, kdTreeBuildMs, speedInfoFound, warnings[] }` |
| `POST /api/route` | `{ start:{lat,lng}, end:{lat,lng}, method, alternatives, repeat }` | per-method `run` objects with `snappedStart/End`, `routes[]`, and timings; `method: "both"` adds `lookupSpeedup` and `sameResult` |
| `POST /api/benchmark` | `{ queries, seed }` | `{ queries, kdTreeTotalMs, kdTreeAvgMicros, bruteTotalMs, bruteAvgMicros, speedup, mismatches }` |

---

## Glossary

| Term | Meaning |
|---|---|
| **GeoJSON** | A text file format for map data. Roads are `LineString`s, places are `Point`s, areas are `Polygon`s. |
| **Overture Maps** | A free, open map dataset that our road and place data comes from. |
| **Graph / node / edge** | A network. Nodes are points (intersections or bends in a road); edges are the road pieces joining them. |
| **Snapping** | Moving the spot you clicked to the nearest point on the road network. |
| **k-d tree** | A data structure that splits points into regions so the nearest one can be found without checking all of them. |
| **Brute force** | Checking every possibility one by one. Simple, but slow on large data. |
| **Dijkstra's algorithm** | A classic way to find the cheapest path through a graph. Here "cheapest" means shortest travel time. |
| **Haversine distance** | The distance between two latitude/longitude points over the Earth's surface. |
| **Benchmark** | A timing test: run the same work many times and measure how long it takes. |

---

## Credits and licenses

The map and data come from open projects.

- Road data: [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, through Overture Maps.
- Places: Overture Maps (from Meta).
- Land-use boundary: Overture Maps.
- Map tiles: [OpenFreeMap](https://openfreemap.org/), © [OpenMapTiles](https://www.openmaptiles.org/), data from OpenStreetMap.
- Place search: [Nominatim](https://nominatim.openstreetmap.org/) (OpenStreetMap). Please keep requests to about one per second.
- Libraries: Leaflet, MapLibre GL JS, the MapLibre GL Leaflet plugin, and Turf.js.