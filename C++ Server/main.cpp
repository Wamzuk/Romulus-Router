// main.cpp - Romulus Router C++ server.
//
// Serves the Map Viewer web page and implements the four endpoints the page
// calls:
//   GET  /                -> the Map Viewer HTML page
//   POST /api/graph       -> build the road network from uploaded GeoJSON text
//   POST /api/route       -> snap start/end to nodes and find routes
//   POST /api/benchmark   -> compare k-d tree vs brute force over many queries
//
// The web page does all the drawing; every graph, k-d tree, snapping, routing
// and timing calculation happens here so the comparison is measured in C++.

#include "http.hpp"
#include "json.hpp"
#include "geo.hpp"
#include "graph.hpp"

#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <random>
#include <chrono>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
static double microsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// --------------------------------------------------------------------------
// Shared, mutex-guarded graph. A new /api/graph upload replaces it.
// --------------------------------------------------------------------------
static std::mutex g_mutex;
static std::shared_ptr<Graph> g_graph;
static std::string g_htmlPath;

static http::Response jsonResponse(const json::Value& v, int status = 200) {
    http::Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = json::dump(v);
    return r;
}

static http::Response errorResponse(const std::string& msg, int status = 400) {
    json::Value v = json::Value::makeObject();
    v.set("error", msg);
    return jsonResponse(v, status);
}

// Build the JSON object for a snapped point: {nodeId, lat, lng, offsetMeters}.
static json::Value snapJson(const Graph& g, const NearestResult& nr) {
    json::Value v = json::Value::makeObject();
    v.set("nodeId", (double)nr.nodeId);
    v.set("lat", g.node(nr.nodeId).lat);
    v.set("lng", g.node(nr.nodeId).lng);
    v.set("offsetMeters", nr.distMeters);
    return v;
}

// Convert a path (node indices) into a GeoJSON LineString geometry object.
static json::Value pathGeometry(const Graph& g, const std::vector<int>& path) {
    json::Value geom = json::Value::makeObject();
    geom.set("type", "LineString");
    json::Value coords = json::Value::makeArray();
    for (int id : path) {
        json::Value pair = json::Value::makeArray();
        pair.push_back(g.node(id).lng); // GeoJSON is [lng, lat]
        pair.push_back(g.node(id).lat);
        coords.push_back(pair);
    }
    geom.set("coordinates", coords);
    return geom;
}

// Run one full routing query with the chosen nearest-node method and build the
// "run" object the front-end expects.
//
// nearestFn: member pointer selecting k-d tree vs brute force for the snap.
// repeat:    how many times to repeat the lookup/search so timings are stable.
static json::Value runRouting(const Graph& g,
                              double startLat, double startLng,
                              double endLat, double endLng,
                              int alternatives, int repeat,
                              bool useKd) {
    json::Value run = json::Value::makeObject();

    // --- Nearest-node lookup, timed and repeated ---
    NearestResult snapStart, snapEnd;
    if (repeat < 1) repeat = 1;

    auto t0 = Clock::now();
    for (int i = 0; i < repeat; i++) {
        if (useKd) {
            snapStart = g.nearestKd(startLat, startLng);
            snapEnd = g.nearestKd(endLat, endLng);
        } else {
            snapStart = g.nearestBrute(startLat, startLng);
            snapEnd = g.nearestBrute(endLat, endLng);
        }
    }
    double lookupTotalMicros = microsSince(t0);
    // Average per single lookup call (two lookups per repeat).
    double lookupMicrosAvg = lookupTotalMicros / (repeat * 2.0);

    if (snapStart.nodeId < 0 || snapEnd.nodeId < 0) {
        run.set("error", "Could not snap the points to the road network.");
        return run;
    }

    // --- Shared route search with alternatives, timed ---
    auto t1 = Clock::now();
    auto paths = g.routes(snapStart.nodeId, snapEnd.nodeId, alternatives);
    double searchMicros = microsSince(t1);

    if (paths.empty()) {
        run.set("error", "No route exists between these points on the loaded network.");
        return run;
    }

    run.set("snappedStart", snapJson(g, snapStart));
    run.set("snappedEnd", snapJson(g, snapEnd));

    json::Value routes = json::Value::makeArray();
    int rank = 1;
    for (auto& p : paths) {
        json::Value rt = json::Value::makeObject();
        rt.set("rank", (double)rank++);
        rt.set("geometry", pathGeometry(g, p.nodes));
        rt.set("distanceMeters", p.distanceMeters);
        rt.set("travelSeconds", p.travelSeconds);
        routes.push_back(rt);
    }
    run.set("routes", routes);

    run.set("lookupMicrosAvg", lookupMicrosAvg);
    run.set("searchMicros", searchMicros);
    run.set("totalMicros", lookupMicrosAvg + searchMicros);
    return run;
}

// --------------------------------------------------------------------------
// Endpoint handlers
// --------------------------------------------------------------------------

static http::Response handleGraph(const http::Request& req) {
    json::Value parsed;
    try {
        parsed = json::parse(req.body);
    } catch (const std::exception& e) {
        return errorResponse(std::string("The file is not valid JSON: ") + e.what());
    }

    auto graph = std::make_shared<Graph>();
    try {
        graph->build(parsed);
    } catch (const std::exception& e) {
        return errorResponse(e.what());
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_graph = graph;
    }

    json::Value out = json::Value::makeObject();
    out.set("nodes", (double)graph->nodeCount());
    out.set("edges", (double)graph->edgeCount());
    out.set("kdTreeBuildMs", graph->kdTreeBuildMs);
    out.set("speedInfoFound", graph->speedInfoFound);
    json::Value warnings = json::Value::makeArray();
    for (auto& w : graph->warnings) warnings.push_back(w);
    out.set("warnings", warnings);
    return jsonResponse(out);
}

static http::Response handleRoute(const http::Request& req) {
    std::shared_ptr<Graph> graph;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        graph = g_graph;
    }
    if (!graph) return errorResponse("No road network loaded yet. Upload a GeoJSON file first.");

    json::Value body;
    try {
        body = json::parse(req.body);
    } catch (const std::exception& e) {
        return errorResponse(std::string("Bad request body: ") + e.what());
    }

    const json::Value* start = body.find("start");
    const json::Value* end = body.find("end");
    if (!start || !end || !start->find("lat") || !end->find("lat"))
        return errorResponse("Request must include start and end points with lat/lng.");

    double sLat = start->find("lat")->asNumber();
    double sLng = start->find("lng")->asNumber();
    double eLat = end->find("lat")->asNumber();
    double eLng = end->find("lng")->asNumber();

    std::string method = body.find("method") ? body.find("method")->asString("kdtree") : "kdtree";
    int alternatives = body.find("alternatives") ? (int)body.find("alternatives")->asNumber(3) : 3;
    int repeat = body.find("repeat") ? (int)body.find("repeat")->asNumber(1000) : 1000;
    if (alternatives < 1) alternatives = 1;
    if (alternatives > 10) alternatives = 10;

    json::Value out = json::Value::makeObject();
    out.set("speedInfoFound", graph->speedInfoFound);

    auto buildRun = [&](bool useKd) {
        return runRouting(*graph, sLat, sLng, eLat, eLng, alternatives, repeat, useKd);
    };

    if (method == "brute") {
        out.set("brute", buildRun(false));
    } else if (method == "both") {
        json::Value kd = buildRun(true);
        json::Value brute = buildRun(false);
        out.set("kdtree", kd);
        out.set("brute", brute);

        // Lookup speedup = brute lookup time / k-d tree lookup time.
        const json::Value* kdLook = kd.find("lookupMicrosAvg");
        const json::Value* brLook = brute.find("lookupMicrosAvg");
        double speedup = 0.0;
        if (kdLook && brLook && kdLook->asNumber() > 0)
            speedup = brLook->asNumber() / kdLook->asNumber();
        out.set("lookupSpeedup", speedup);

        // sameResult: did both snap to the same nodes and find the same best route?
        bool same = true;
        const json::Value* ks = kd.find("snappedStart");
        const json::Value* bs = brute.find("snappedStart");
        const json::Value* ke = kd.find("snappedEnd");
        const json::Value* be = brute.find("snappedEnd");
        if (ks && bs && ke && be) {
            if (ks->find("nodeId")->asNumber() != bs->find("nodeId")->asNumber()) same = false;
            if (ke->find("nodeId")->asNumber() != be->find("nodeId")->asNumber()) same = false;
        } else {
            same = false;
        }
        out.set("sameResult", same);
    } else {
        out.set("kdtree", buildRun(true));
    }

    return jsonResponse(out);
}

static http::Response handleBenchmark(const http::Request& req) {
    std::shared_ptr<Graph> graph;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        graph = g_graph;
    }
    if (!graph) return errorResponse("No road network loaded yet. Upload a GeoJSON file first.");
    if (graph->nodeCount() == 0) return errorResponse("The loaded network has no nodes.");

    json::Value body;
    try {
        body = req.body.empty() ? json::Value::makeObject() : json::parse(req.body);
    } catch (...) {
        body = json::Value::makeObject();
    }
    int queries = body.find("queries") ? (int)body.find("queries")->asNumber(10000) : 10000;
    unsigned seed = body.find("seed") ? (unsigned)body.find("seed")->asNumber(1) : 1u;
    if (queries < 1) queries = 1;
    if (queries > 2000000) queries = 2000000;

    // Build the set of random query points inside the data's bounding box.
    double minLat = 1e18, maxLat = -1e18, minLng = 1e18, maxLng = -1e18;
    for (auto& n : graph->nodes) {
        minLat = std::min(minLat, n.lat); maxLat = std::max(maxLat, n.lat);
        minLng = std::min(minLng, n.lng); maxLng = std::max(maxLng, n.lng);
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> latDist(minLat, maxLat);
    std::uniform_real_distribution<double> lngDist(minLng, maxLng);

    std::vector<double> qLat(queries), qLng(queries);
    for (int i = 0; i < queries; i++) { qLat[i] = latDist(rng); qLng[i] = lngDist(rng); }

    // --- k-d tree pass ---
    std::vector<int> kdResult(queries);
    auto t0 = Clock::now();
    for (int i = 0; i < queries; i++) {
        kdResult[i] = graph->nearestKd(qLat[i], qLng[i]).nodeId;
    }
    double kdTotalMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    // --- brute-force pass ---
    std::vector<int> bruteResult(queries);
    auto t1 = Clock::now();
    for (int i = 0; i < queries; i++) {
        bruteResult[i] = graph->nearestBrute(qLat[i], qLng[i]).nodeId;
    }
    double bruteTotalMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();

    // The two methods must agree on every query.
    int mismatches = 0;
    for (int i = 0; i < queries; i++) if (kdResult[i] != bruteResult[i]) mismatches++;

    double kdAvgMicros = (kdTotalMs * 1000.0) / queries;
    double bruteAvgMicros = (bruteTotalMs * 1000.0) / queries;
    double speedup = kdTotalMs > 0 ? bruteTotalMs / kdTotalMs : 0.0;

    json::Value out = json::Value::makeObject();
    out.set("queries", (double)queries);
    out.set("nodes", (double)graph->nodeCount());
    out.set("kdTreeTotalMs", kdTotalMs);
    out.set("kdTreeAvgMicros", kdAvgMicros);
    out.set("bruteTotalMs", bruteTotalMs);
    out.set("bruteAvgMicros", bruteAvgMicros);
    out.set("speedup", speedup);
    out.set("mismatches", (double)mismatches);
    out.set("kdTreeBuildMs", graph->kdTreeBuildMs);
    return jsonResponse(out);
}

// Serve the Map Viewer HTML page at "/".
static http::Response serveStatic(const http::Request& req) {
    if (req.path != "/" && req.path != "/index.html" && req.path != "/Map Viewer.html")
        return http::Response::make(404, "text/plain", "Not Found");

    std::ifstream in(g_htmlPath, std::ios::binary);
    if (!in) {
        return http::Response::make(500, "text/plain",
                                    "Could not open the HTML page at: " + g_htmlPath);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    http::Response r;
    r.status = 200;
    r.contentType = "text/html; charset=utf-8";
    r.body = ss.str();
    return r;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8080;
    g_htmlPath = "Map Viewer.html";

    // CLI: [port] [host] [--html path]
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--html" && i + 1 < argc) { g_htmlPath = argv[++i]; }
        else if (a == "--host" && i + 1 < argc) { host = argv[++i]; }
        else if (a == "--port" && i + 1 < argc) { port = std::atoi(argv[++i]); }
        else if (!a.empty() && std::isdigit((unsigned char)a[0])) { port = std::atoi(a.c_str()); }
        else host = a;
    }

    http::Server server;
    server.route("GET", "/", serveStatic);
    server.route("GET", "/index.html", serveStatic);
    server.route("GET", "/Map Viewer.html", serveStatic);
    server.route("POST", "/api/graph", handleGraph);
    server.route("POST", "/api/route", handleRoute);
    server.route("POST", "/api/benchmark", handleBenchmark);
    server.setDefault(serveStatic);

    if (!server.listen(host, port)) {
        std::cerr << "Failed to bind " << host << ":" << port << "\n";
        return 1;
    }

    std::cout << "Romulus Router server listening on http://" << host << ":" << port << "\n";
    std::cout << "Serving page: " << g_htmlPath << "\n";
    std::cout << "Open the URL above in your browser, then load a GeoJSON road network.\n";
    server.run();
    return 0;
}
