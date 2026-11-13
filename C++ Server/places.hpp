// places.hpp - nearest-PLACE search (coffee shops, hospitals, bus stops, ...).
//
// Same idea as the road-node snap, but over the named places in the file:
// one k-d tree per category (plus one for "any place"), compared against a
// brute-force scan of the same set. Reuses KdTree and Node from graph.hpp.
#pragma once

#include "graph.hpp"
#include "geo.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>

class PlaceIndex {
public:
    struct Place { std::string name, category; double lat = 0, lng = 0; };

    std::vector<Place> places;
    double buildMs = 0.0;

    // Category "" means any place.
    void build(std::vector<Place> p) {
        places = std::move(p);
        groups_.clear();
        double ref = 0;
        for (auto& x : places) ref += x.lat;
        if (!places.empty()) ref /= (double)places.size();
        proj_ = geo::Projector(ref);

        for (size_t i = 0; i < places.size(); i++) {
            add("", (int)i);
            if (!places[i].category.empty()) add(places[i].category, (int)i);
        }
        auto t0 = std::chrono::high_resolution_clock::now();
        for (auto& kv : groups_) kv.second->tree.build(kv.second->nodes);
        buildMs = std::chrono::duration<double, std::milli>(
                      std::chrono::high_resolution_clock::now() - t0).count();
    }

    size_t groupSize(const std::string& cat) const {
        auto it = groups_.find(cat);
        return it == groups_.end() ? 0 : it->second->ids.size();
    }

    // Places to ignore when searching FROM a point that is itself a place: anything
    // within sameSpotM of it, plus anything with the same name within sameNameM
    // (the same building listed twice). Computed once per request, outside the timing.
    std::vector<int> excludedAt(double lat, double lng, const std::string& startName,
                                double sameSpotM, double sameNameM) const {
        std::vector<int> out;
        for (size_t i = 0; i < places.size(); i++) {
            double d = geo::haversineMeters(lat, lng, places[i].lat, places[i].lng);
            if (d <= sameSpotM || (d <= sameNameM && sameName(places[i].name, startName)))
                out.push_back((int)i);
        }
        return out;
    }

    // Both return an index into `places`, or -1 if nothing qualifies. Places listed
    // in `excluded` are ignored (so starting AT a place finds the next-nearest one).
    int nearestKd(const std::string& cat, double lat, double lng, const std::vector<int>& excluded) const {
        auto it = groups_.find(cat);
        if (it == groups_.end()) return -1;
        const Group& g = *it->second;
        std::function<bool(int)> skip = [&](int local) { return isExcluded(excluded, g.ids[local]); };
        int i = g.tree.nearest(proj_.x(lng), proj_.y(lat), excluded.empty() ? nullptr : &skip);
        return i < 0 ? -1 : g.ids[i];
    }

    int nearestBrute(const std::string& cat, double lat, double lng, const std::vector<int>& excluded) const {
        auto it = groups_.find(cat);
        if (it == groups_.end()) return -1;
        const Group& g = *it->second;
        double qx = proj_.x(lng), qy = proj_.y(lat);
        int best = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < g.nodes.size(); i++) {
            double dx = g.nodes[i].px - qx, dy = g.nodes[i].py - qy;
            double d2 = dx * dx + dy * dy;
            if (d2 < bestD2 && !isExcluded(excluded, g.ids[i])) { bestD2 = d2; best = g.ids[i]; }
        }
        return best;
    }

private:
    // KdTree keeps a pointer to `nodes`, so a Group must never be moved or
    // copied after build(); they live behind unique_ptr for that reason.
    struct Group { std::vector<Node> nodes; std::vector<int> ids; KdTree tree; };
    std::unordered_map<std::string, std::unique_ptr<Group>> groups_;
    geo::Projector proj_;

    static bool isExcluded(const std::vector<int>& excluded, int id) {
        return std::find(excluded.begin(), excluded.end(), id) != excluded.end();
    }

    static std::string lower(std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // Same name, ignoring case; also accepts a start label like "Tyler Hall, Kingston".
    static bool sameName(const std::string& placeName, const std::string& startName) {
        if (placeName.empty() || startName.empty()) return false;
        std::string a = lower(placeName), b = lower(startName);
        return a == b || b.compare(0, a.size() + 1, a + ",") == 0;
    }

    void add(const std::string& key, int idx) {
        auto& g = groups_[key];
        if (!g) g.reset(new Group());
        Node n;
        n.lat = places[idx].lat;
        n.lng = places[idx].lng;
        n.px = proj_.x(n.lng);
        n.py = proj_.y(n.lat);
        g->nodes.push_back(n);
        g->ids.push_back(idx);
    }
};