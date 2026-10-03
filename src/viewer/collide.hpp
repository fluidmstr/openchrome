// Walk-mode collision straight from the render meshes: instances are bucketed in a 32 m grid, rays are tested
// against the LOD0 triangles of nearby instances in their local space. Stand-in until the PhysX blobs are decoded.
#pragma once
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/sobj.hpp"

struct ColliderInput {
    const std::vector<float>* verts;      // 6 floats per vertex, xyz first
    const std::vector<uint32_t>* indices;
    const std::vector<oc::Instance>* instances;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> ranges;  // per type: (first index, count) runs of LOD0
    std::vector<int32_t> vertexOffset;                                // per type
    std::vector<char> solid;                                          // per type: takes part in collision
};

class Collider {
public:
    explicit Collider(ColliderInput in) : in_(std::move(in)) {
        size_t nt = in_.ranges.size();
        center_.assign(nt, glm::vec3(0));
        radius_.assign(nt, 0.0f);
        for (size_t t = 0; t < nt; t++) {
            glm::vec3 lo(1e30f), hi(-1e30f);
            bool any = false;
            for (auto [f, c] : in_.ranges[t])
                for (uint32_t i = f; i < f + c; i++) { glm::vec3 p = vert(t, i); lo = glm::min(lo, p); hi = glm::max(hi, p); any = true; }
            if (!any) continue;
            center_[t] = (lo + hi) * 0.5f;
            radius_[t] = glm::length(hi - lo) * 0.5f;
        }
        const auto& inst = *in_.instances;
        stamp_.assign(inst.size(), 0);
        for (size_t i = 0; i < inst.size(); i++) {
            uint16_t t = inst[i].type;
            if (t >= nt || !in_.solid[t] || radius_[t] <= 0.0f) continue;
            glm::vec3 c = world(inst[i], center_[t]);
            float r = radius_[t] * std::max({std::fabs(inst[i].scale[0]), std::fabs(inst[i].scale[1]), std::fabs(inst[i].scale[2])});
            for (int x = cellOf(c.x - r); x <= cellOf(c.x + r); x++)
                for (int z = cellOf(c.z - r); z <= cellOf(c.z + r); z++) grid_[key(x, z)].push_back((uint32_t)i);
        }
    }

    // Nearest hit of the segment o + t*d, t in [0, maxT] (d normalised); n is the world normal of the hit triangle.
    bool ray(glm::vec3 o, glm::vec3 d, float maxT, float& tOut, glm::vec3& n) {
        stampNow_++;
        glm::vec3 e = o + d * maxT;
        float best = maxT;
        bool hit = false;
        for (int x = cellOf(std::min(o.x, e.x)); x <= cellOf(std::max(o.x, e.x)); x++)
            for (int z = cellOf(std::min(o.z, e.z)); z <= cellOf(std::max(o.z, e.z)); z++) {
                auto it = grid_.find(key(x, z));
                if (it == grid_.end()) continue;
                for (uint32_t i : it->second) {
                    if (stamp_[i] == stampNow_) continue;
                    stamp_[i] = stampNow_;
                    hit |= rayInstance((*in_.instances)[i], o, d, best, n);
                }
            }
        tOut = best;
        return hit;
    }

private:
    static int cellOf(float v) { return (int)std::floor(v / 32.0f); }
    static int64_t key(int x, int z) { return ((int64_t)x << 32) ^ (uint32_t)z; }

    glm::vec3 vert(size_t t, uint32_t idx) const {
        size_t v = (size_t)(in_.vertexOffset[t] + (int32_t)(*in_.indices)[idx]) * 6;
        const float* p = in_.verts->data() + v;
        return {p[0], p[1], p[2]};
    }
    static glm::vec4 quat(const oc::Instance& in) {
        glm::vec4 q(in.quat[0] / 32767.0f, in.quat[1] / 32767.0f, in.quat[2] / 32767.0f, in.quat[3] / 32767.0f);
        return q / std::max(glm::length(q), 1e-6f);
    }
    static glm::vec3 rot(glm::vec4 q, glm::vec3 v) { glm::vec3 u(q.x, q.y, q.z); return v + 2.0f * glm::cross(u, glm::cross(u, v) + q.w * v); }
    static glm::vec3 unrot(glm::vec4 q, glm::vec3 v) { return rot(glm::vec4(-q.x, -q.y, -q.z, q.w), v); }
    static glm::vec3 scaleOf(const oc::Instance& in) { return {in.scale[0], in.scale[1], in.scale[2]}; }
    static glm::vec3 world(const oc::Instance& in, glm::vec3 l) { return rot(quat(in), l * scaleOf(in)) + glm::vec3(in.pos[0], in.pos[1], in.pos[2]); }

    bool rayInstance(const oc::Instance& in, glm::vec3 o, glm::vec3 d, float& best, glm::vec3& nOut) {
        size_t t = in.type;
        glm::vec4 q = quat(in);
        glm::vec3 s = scaleOf(in), p(in.pos[0], in.pos[1], in.pos[2]);
        auto rc = [](float v) { return 1.0f / (std::fabs(v) < 1e-4f ? 1e-4f : v); };
        glm::vec3 inv(rc(s.x), rc(s.y), rc(s.z));
        glm::vec3 lo = unrot(q, o - p) * inv, ld = unrot(q, d) * inv;  // same parameter t as in world space
        // bounding sphere reject in local space
        glm::vec3 oc_ = lo - center_[t];
        float a = glm::dot(ld, ld), b = glm::dot(oc_, ld), c = glm::dot(oc_, oc_) - radius_[t] * radius_[t];
        float disc = b * b - a * c;
        if (disc < 0.0f) return false;
        float sq = std::sqrt(disc), t0 = (-b - sq) / a, t1 = (-b + sq) / a;
        if (t1 < 0.0f || t0 > best) return false;
        bool hit = false;
        glm::vec3 bestN(0, 1, 0);
        for (auto [f, cnt] : in_.ranges[t])
            for (uint32_t i = f; i < f + cnt; i += 3) {
                glm::vec3 v0 = vert(t, i), e1 = vert(t, i + 1) - v0, e2 = vert(t, i + 2) - v0;
                glm::vec3 pv = glm::cross(ld, e2);
                float det = glm::dot(e1, pv);
                if (std::fabs(det) < 1e-9f) continue;
                float id = 1.0f / det;
                glm::vec3 tv = lo - v0;
                float u = glm::dot(tv, pv) * id;
                if (u < 0.0f || u > 1.0f) continue;
                glm::vec3 qv = glm::cross(tv, e1);
                float v = glm::dot(ld, qv) * id;
                if (v < 0.0f || u + v > 1.0f) continue;
                float tt = glm::dot(e2, qv) * id;
                if (tt < 0.0f || tt >= best) continue;
                best = tt;
                bestN = glm::cross(e1, e2);
                hit = true;
            }
        if (hit) {
            glm::vec3 nl = bestN * inv;  // inverse transpose of scale, then rotate
            glm::vec3 nw = rot(q, nl);
            float len = glm::length(nw);
            nOut = len > 0.0f ? nw / len : glm::vec3(0, 1, 0);
            if (glm::dot(nOut, d) > 0.0f) nOut = -nOut;
        }
        return hit;
    }

    ColliderInput in_;
    std::vector<glm::vec3> center_;
    std::vector<float> radius_;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    std::vector<uint32_t> stamp_;
    uint32_t stampNow_ = 0;
};
