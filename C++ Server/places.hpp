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

    // Both return an index into `places`, or -1 if the category is empty/unknown.
    int nearestKd(const std::string& cat, double lat, double lng) const {
        auto it = groups_.find(cat);
        if (it == groups_.end()) return -1;
        int i = it->second->tree.nearest(proj_.x(lng), proj_.y(lat));
        return i < 0 ? -1 : it->second->ids[i];
    }

    int nearestBrute(const std::string& cat, double lat, double lng) const {
        auto it = groups_.find(cat);
        if (it == groups_.end()) return -1;
        const Group& g = *it->second;
        double qx = proj_.x(lng), qy = proj_.y(lat);
        int best = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < g.nodes.size(); i++) {
            double dx = g.nodes[i].px - qx, dy = g.nodes[i].py - qy;
            double d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = g.ids[i]; }
        }
        return best;
    }

private:
    // KdTree keeps a pointer to `nodes`, so a Group must never be moved or
    // copied after build(); they live behind unique_ptr for that reason.
    struct Group { std::vector<Node> nodes; std::vector<int> ids; KdTree tree; };
    std::unordered_map<std::string, std::unique_ptr<Group>> groups_;
    geo::Projector proj_;

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