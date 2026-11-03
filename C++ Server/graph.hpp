// graph.hpp - the road network graph, the k-d tree and brute-force nearest-node
// search, and Dijkstra-based routing with alternatives.
//
// This file is the core of the project. It holds:
//   * Graph       - nodes (intersections / road endpoints) and directed edges.
//   * KdTree      - a 2-D k-d tree over the nodes for O(log n) nearest search.
//   * bruteNearest- the O(n) baseline the k-d tree is compared against.
//   * route()     - Dijkstra shortest path plus simple alternative routes.
#pragma once

#include "json.hpp"
#include "geo.hpp"

#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <limits>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <stdexcept>

struct Node {
    double lat;
    double lng;
    // Projected coordinates used by both nearest-search methods (see Projector).
    double px;
    double py;
};

struct Edge {
    int to;          // destination node index
    double length;   // metres
    double time;     // seconds (length / effective speed)
};

// Result of a nearest-node query. Both k-d tree and brute force fill this in.
struct NearestResult {
    int nodeId = -1;
    double distMeters = 0.0; // true haversine distance from query point to node
};

// ----------------------------------------------------------------------------
// k-d tree over node projected coordinates (2-D: x, y).
// ----------------------------------------------------------------------------
class KdTree {
public:
    void build(const std::vector<Node>& nodes) {
        nodes_ = &nodes;
        idx_.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); i++) idx_[i] = static_cast<int>(i);
        tree_.assign(nodes.size(), KdNode{});
        if (!nodes.empty()) {
            size_t pos = 0;
            root_ = buildRec(0, idx_.size(), 0, pos);
        } else {
            root_ = -1;
        }
    }

    // Nearest node to a projected query point (qx, qy). Returns node index.
    int nearest(double qx, double qy) const {
        if (root_ < 0) return -1;
        best_ = -1;
        bestDist2_ = std::numeric_limits<double>::infinity();
        searchRec(root_, qx, qy, 0);
        return best_;
    }

private:
    struct KdNode { int point = -1; int left = -1; int right = -1; };

    const std::vector<Node>* nodes_ = nullptr;
    std::vector<int> idx_;
    std::vector<KdNode> tree_;
    int root_ = -1;
    mutable int best_ = -1;
    mutable double bestDist2_ = 0.0;

    double coord(int nodeIndex, int axis) const {
        const Node& n = (*nodes_)[nodeIndex];
        return axis == 0 ? n.px : n.py;
    }

    // Build recursively by alternating the split axis and using nth_element
    // (median split) so the tree stays balanced -> O(log n) average lookups.
    int buildRec(size_t lo, size_t hi, int depth, size_t& pos) {
        if (lo >= hi) return -1;
        int axis = depth % 2;
        size_t mid = lo + (hi - lo) / 2;
        std::nth_element(idx_.begin() + lo, idx_.begin() + mid, idx_.begin() + hi,
            [&](int a, int b) { return coord(a, axis) < coord(b, axis); });

        int self = static_cast<int>(pos++);
        tree_[self].point = idx_[mid];
        tree_[self].left = buildRec(lo, mid, depth + 1, pos);
        tree_[self].right = buildRec(mid + 1, hi, depth + 1, pos);
        return self;
    }

    void searchRec(int node, double qx, double qy, int depth) const {
        if (node < 0) return;
        const KdNode& kn = tree_[node];
        int p = kn.point;
        double dx = (*nodes_)[p].px - qx;
        double dy = (*nodes_)[p].py - qy;
        double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2_) { bestDist2_ = d2; best_ = p; }

        int axis = depth % 2;
        double diff = (axis == 0 ? qx - (*nodes_)[p].px : qy - (*nodes_)[p].py);
        int near = diff < 0 ? kn.left : kn.right;
        int far  = diff < 0 ? kn.right : kn.left;

        searchRec(near, qx, qy, depth + 1);
        // Only cross to the far side if the splitting plane is closer than the
        // best distance found so far -- this pruning is what makes it fast.
        if (diff * diff < bestDist2_) searchRec(far, qx, qy, depth + 1);
    }
};

// ----------------------------------------------------------------------------
// The road network.
// ----------------------------------------------------------------------------
class Graph {
public:
    std::vector<Node> nodes;
    std::vector<std::vector<Edge>> adj; // adjacency list, parallel to nodes
    KdTree kdtree;
    geo::Projector projector;
    double kdTreeBuildMs = 0.0;
    bool speedInfoFound = false; // true if any road carried an explicit maxspeed
    std::vector<std::string> warnings;

    size_t nodeCount() const { return nodes.size(); }
    size_t edgeCount() const {
        size_t e = 0;
        for (auto& a : adj) e += a.size();
        return e;
    }

    // Build the graph from parsed GeoJSON. Throws std::runtime_error on bad input.
    void build(const json::Value& geojson) {
        nodes.clear();
        adj.clear();
        warnings.clear();
        speedInfoFound = false;

        const json::Value* feats = geojson.find("features");
        std::vector<const json::Value*> featureList;
        if (geojson.asString() == "" && geojson.find("type")) {
            std::string t = geojson.find("type")->asString();
            if (t == "FeatureCollection") {
                if (!feats || !feats->isArray()) throw std::runtime_error("FeatureCollection has no features array.");
                for (auto& f : *feats->arr) featureList.push_back(&f);
            } else if (t == "Feature") {
                featureList.push_back(&geojson);
            } else {
                // A bare geometry.
                featureList.push_back(&geojson);
            }
        } else {
            throw std::runtime_error("Input is not valid GeoJSON (missing \"type\").");
        }

        // Compute a reference latitude for the projection from the first coords.
        double refLat = pickReferenceLat(featureList);
        projector = geo::Projector(refLat);

        // Coordinate -> node index. We quantise coordinates to ~1e-7 degrees
        // (about 1 cm) so endpoints that are meant to be the same intersection
        // merge into one node.
        std::unordered_map<std::string, int> nodeOf;

        int lineCount = 0, polyIgnored = 0, pointIgnored = 0;

        for (const json::Value* f : featureList) {
            const json::Value* geom = nullptr;
            const json::Value* props = nullptr;
            if (f->find("type") && f->find("type")->asString() == "Feature") {
                geom = f->find("geometry");
                props = f->find("properties");
            } else {
                geom = f; // bare geometry
            }
            if (!geom || geom->isNull()) continue;
            std::string gtype = geom->find("type") ? geom->find("type")->asString() : "";

            if (gtype == "LineString") {
                addLineString(geom, props, nodeOf);
                lineCount++;
            } else if (gtype == "MultiLineString") {
                const json::Value* coords = geom->find("coordinates");
                if (coords && coords->isArray()) {
                    for (auto& part : *coords->arr) {
                        addLineFromCoords(part, props, nodeOf);
                        lineCount++;
                    }
                }
            } else if (gtype == "Polygon" || gtype == "MultiPolygon") {
                polyIgnored++;
            } else if (gtype == "Point" || gtype == "MultiPoint") {
                pointIgnored++;
            }
        }

        if (nodes.empty() || lineCount == 0) {
            throw std::runtime_error("No road lines (LineString features) found in the file.");
        }

        if (!speedInfoFound) {
            warnings.push_back("No maxspeed tags found; travel times use per-class default speeds.");
        }
        if (polyIgnored > 0) {
            warnings.push_back("Ignored " + std::to_string(polyIgnored) +
                               " polygon feature" + (polyIgnored == 1 ? "" : "s") + " (boundary).");
        }

        // Build the k-d tree and time it (the one-off cost the proposal measures).
        auto t0 = std::chrono::high_resolution_clock::now();
        kdtree.build(nodes);
        auto t1 = std::chrono::high_resolution_clock::now();
        kdTreeBuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    // -- nearest-node search, two ways (they must always agree) --
    NearestResult nearestKd(double lat, double lng) const {
        double qx = projector.x(lng), qy = projector.y(lat);
        int id = kdtree.nearest(qx, qy);
        return makeResult(id, lat, lng);
    }

    NearestResult nearestBrute(double lat, double lng) const {
        double qx = projector.x(lng), qy = projector.y(lat);
        int best = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < nodes.size(); i++) {
            double dx = nodes[i].px - qx;
            double dy = nodes[i].py - qy;
            double d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
        }
        return makeResult(best, lat, lng);
    }

    // -- Dijkstra shortest path by travel time (seconds) --
    // Returns the ordered list of node indices, or empty if unreachable.
    // `blocked` edges (by "from,to" packed key) are skipped -- used to generate
    // alternatives.
    struct PathResult {
        std::vector<int> nodes;
        double distanceMeters = 0.0;
        double travelSeconds = 0.0;
        bool found = false;
    };

    PathResult dijkstra(int src, int dst,
                        const std::vector<double>* edgePenalty = nullptr) const {
        PathResult out;
        if (src < 0 || dst < 0 || src >= (int)nodes.size() || dst >= (int)nodes.size())
            return out;

        const double INF = std::numeric_limits<double>::infinity();
        std::vector<double> dist(nodes.size(), INF);
        std::vector<int> prev(nodes.size(), -1);
        std::vector<int> prevEdge(nodes.size(), -1);

        using QItem = std::pair<double, int>; // (cost, node)
        std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
        dist[src] = 0.0;
        pq.push({0.0, src});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u]) continue;
            if (u == dst) break;
            const auto& edges = adj[u];
            for (size_t ei = 0; ei < edges.size(); ei++) {
                const Edge& e = edges[ei];
                double w = e.time;
                if (edgePenalty) w *= (*edgePenalty)[edgeGlobalId(u, ei)];
                double nd = d + w;
                if (nd < dist[e.to]) {
                    dist[e.to] = nd;
                    prev[e.to] = u;
                    prevEdge[e.to] = static_cast<int>(ei);
                    pq.push({nd, e.to});
                }
            }
        }

        if (dist[dst] == INF) return out;

        // Reconstruct the path and sum the REAL (unpenalised) length/time.
        std::vector<int> path;
        for (int v = dst; v != -1; v = prev[v]) path.push_back(v);
        std::reverse(path.begin(), path.end());
        out.nodes = path;
        out.found = true;
        for (int v = dst; prev[v] != -1; v = prev[v]) {
            const Edge& e = adj[prev[v]][prevEdge[v]];
            out.distanceMeters += e.length;
            out.travelSeconds += e.time;
        }
        return out;
    }

    // Generate up to `k` routes: the optimum plus alternatives found by
    // penalising the edges of routes already returned (a simple, robust
    // alternative-routes heuristic). Alternatives that are too similar to an
    // existing route, or that duplicate it exactly, are dropped.
    std::vector<PathResult> routes(int src, int dst, int k) const {
        std::vector<PathResult> results;
        if (k < 1) k = 1;

        // Per-edge multiplier, 1.0 = no penalty.
        std::vector<double> penalty(totalEdges_, 1.0);

        for (int attempt = 0; attempt < k * 4 && (int)results.size() < k; attempt++) {
            PathResult p = dijkstra(src, dst, results.empty() ? nullptr : &penalty);
            if (!p.found) break;

            // Deduplicate: skip if identical node sequence to an existing route.
            bool duplicate = false;
            for (auto& r : results) {
                if (r.nodes == p.nodes) { duplicate = true; break; }
            }
            if (!duplicate) {
                // Reject near-identical alternatives (>80% shared edges with the
                // best route) so the list offers genuinely different options.
                if (results.empty() || overlapFraction(results[0], p) < 0.8) {
                    results.push_back(p);
                }
            }

            if ((int)results.size() >= k) break;

            // Penalise the edges of the route we just accepted (or the best
            // route on the first alternative pass) to push the next search away.
            const PathResult& toPenalise = duplicate ? results.back() : p;
            penaliseEdges(toPenalise, penalty, 1.6);
        }

        // Sort by travel time so rank 1 is always the fastest.
        std::sort(results.begin(), results.end(),
                  [](const PathResult& a, const PathResult& b) {
                      return a.travelSeconds < b.travelSeconds;
                  });
        return results;
    }

    const Node& node(int i) const { return nodes[i]; }

private:
    // Each edge gets a stable global id so penalties can be addressed by it.
    std::vector<std::vector<int>> edgeId_; // edgeId_[u][ei] -> global id
    size_t totalEdges_ = 0;

    size_t edgeGlobalId(int u, size_t ei) const { return edgeId_[u][ei]; }

    static double overlapFraction(const PathResult& a, const PathResult& b) {
        // Fraction of b's directed edges that also appear in a.
        if (b.nodes.size() < 2) return 0.0;
        std::unordered_map<long long, int> aEdges;
        for (size_t i = 0; i + 1 < a.nodes.size(); i++) {
            long long key = (long long)a.nodes[i] * 2147483647LL + a.nodes[i + 1];
            aEdges[key]++;
        }
        int shared = 0, total = 0;
        for (size_t i = 0; i + 1 < b.nodes.size(); i++) {
            long long key = (long long)b.nodes[i] * 2147483647LL + b.nodes[i + 1];
            total++;
            if (aEdges.count(key)) shared++;
        }
        return total ? (double)shared / total : 0.0;
    }

    void penaliseEdges(const PathResult& p, std::vector<double>& penalty, double factor) const {
        for (size_t i = 0; i + 1 < p.nodes.size(); i++) {
            int u = p.nodes[i], v = p.nodes[i + 1];
            const auto& edges = adj[u];
            for (size_t ei = 0; ei < edges.size(); ei++) {
                if (edges[ei].to == v) penalty[edgeId_[u][ei]] *= factor;
            }
        }
    }

    NearestResult makeResult(int id, double lat, double lng) const {
        NearestResult r;
        r.nodeId = id;
        if (id >= 0) {
            r.distMeters = geo::haversineMeters(lat, lng, nodes[id].lat, nodes[id].lng);
        }
        return r;
    }

    double pickReferenceLat(const std::vector<const json::Value*>& feats) const {
        for (const json::Value* f : feats) {
            const json::Value* geom = (f->find("type") && f->find("type")->asString() == "Feature")
                                          ? f->find("geometry") : f;
            if (!geom || geom->isNull()) continue;
            const json::Value* coords = geom->find("coordinates");
            if (!coords) continue;
            double lat;
            if (firstLat(*coords, lat)) return lat;
        }
        return 0.0;
    }

    static bool firstLat(const json::Value& coords, double& outLat) {
        if (coords.isArray() && coords.arr && !coords.arr->empty()) {
            const json::Value& first = (*coords.arr)[0];
            if (first.isNumber()) {
                // This array is [lng, lat].
                if (coords.arr->size() >= 2) { outLat = (*coords.arr)[1].asNumber(); return true; }
                return false;
            }
            return firstLat(first, outLat);
        }
        return false;
    }

    // Quantise a coordinate pair to a dictionary key.
    static std::string keyOf(double lng, double lat) {
        long long qlng = llround(lng * 1e7);
        long long qlat = llround(lat * 1e7);
        return std::to_string(qlng) + "," + std::to_string(qlat);
    }

    int getOrCreateNode(double lng, double lat, std::unordered_map<std::string, int>& nodeOf) {
        std::string key = keyOf(lng, lat);
        auto it = nodeOf.find(key);
        if (it != nodeOf.end()) return it->second;
        int id = static_cast<int>(nodes.size());
        Node n;
        n.lat = lat;
        n.lng = lng;
        n.px = projector.x(lng);
        n.py = projector.y(lat);
        nodes.push_back(n);
        adj.emplace_back();
        edgeId_.emplace_back();
        nodeOf[key] = id;
        return id;
    }

    void addLineString(const json::Value* geom, const json::Value* props,
                       std::unordered_map<std::string, int>& nodeOf) {
        const json::Value* coords = geom->find("coordinates");
        if (!coords || !coords->isArray()) return;
        addLineFromCoords(*coords, props, nodeOf);
    }

    void addLineFromCoords(const json::Value& coords, const json::Value* props,
                           std::unordered_map<std::string, int>& nodeOf) {
        if (!coords.isArray() || !coords.arr || coords.arr->size() < 2) return;

        // Figure out the edge weights for this road.
        std::string roadClass;
        if (props) {
            const json::Value* cls = props->find("class");
            if (!cls) cls = props->find("highway");
            if (cls) roadClass = cls->asString();
        }
        bool oneway = false;
        double speedMps = 0.0;
        if (props) {
            const json::Value* ow = props->find("oneway");
            if (ow && ow->asString() == "yes") oneway = true;
            const json::Value* ms = props->find("maxspeed");
            if (ms) {
                std::string sval = ms->isString() ? ms->asString()
                                                  : (ms->isNumber() ? std::to_string((long)ms->asNumber()) : "");
                speedMps = geo::parseSpeedMps(sval);
                if (speedMps > 0) speedInfoFound = true;
            }
        }
        if (speedMps <= 0) speedMps = geo::defaultSpeedMps(roadClass);

        // Walk the vertices, connecting consecutive points with edges.
        int prevId = -1;
        double prevLng = 0, prevLat = 0;
        for (auto& pt : *coords.arr) {
            if (!pt.isArray() || !pt.arr || pt.arr->size() < 2) continue;
            double lng = (*pt.arr)[0].asNumber();
            double lat = (*pt.arr)[1].asNumber();
            int id = getOrCreateNode(lng, lat, nodeOf);
            if (prevId != -1 && id != prevId) {
                double len = geo::haversineMeters(prevLat, prevLng, lat, lng);
                double t = len / speedMps;
                addEdge(prevId, id, len, t);
                if (!oneway) addEdge(id, prevId, len, t);
            }
            prevId = id;
            prevLng = lng;
            prevLat = lat;
        }
    }

    void addEdge(int from, int to, double length, double time) {
        adj[from].push_back(Edge{to, length, time});
        edgeId_[from].push_back(static_cast<int>(totalEdges_++));
    }
};
