#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace evo {

struct Vec2 {
    float x = 0, y = 0;
};

// A uniform grid over the wrap-around world, for neighbour lookups. It is rebuilt every tick
// with a counting sort, so the items of one cell sit next to each other in memory (with a
// copy of their positions) and a lookup reads contiguous arrays instead of chasing pointers.
class SpatialGrid {
public:
    void init(float width, float height, float cellSize) {
        width_ = width;
        height_ = height;
        cols_ = std::max(1, static_cast<int>(width / cellSize));
        rows_ = std::max(1, static_cast<int>(height / cellSize));
        cellW_ = width / cols_;
        cellH_ = height / rows_;
        start_.assign(cols_ * rows_ + 1, 0);
    }

    // `pos(i)` gives the position of item i, inside the world.
    template <typename GetPos>
    void build(int count, GetPos&& pos) {
        cellOfItem_.resize(count);
        std::fill(start_.begin(), start_.end(), 0);
        for (int i = 0; i < count; ++i) {
            Vec2 p = pos(i);
            int cx = std::min(cols_ - 1, static_cast<int>(p.x / cellW_));
            int cy = std::min(rows_ - 1, static_cast<int>(p.y / cellH_));
            cellOfItem_[i] = cy * cols_ + cx;
            ++start_[cellOfItem_[i] + 1];
        }
        for (size_t c = 1; c < start_.size(); ++c) start_[c] += start_[c - 1];
        fill_.assign(start_.begin(), start_.end() - 1);
        items_.resize(count);
        positions_.resize(count);
        for (int i = 0; i < count; ++i) {
            int k = fill_[cellOfItem_[i]]++;
            items_[k] = i;
            positions_[k] = pos(i);
        }
    }

    // Calls f(item, dx, dy) for every item in the cells that overlap the square of half-size
    // `radius` around p, where (dx, dy) is the shortest offset from p to the item (as of the
    // last build). Callers do their own exact distance test.
    template <typename F>
    void query(Vec2 p, float radius, F&& f) const {
        int x0 = static_cast<int>(std::floor((p.x - radius) / cellW_)), x1 = static_cast<int>(std::floor((p.x + radius) / cellW_));
        int y0 = static_cast<int>(std::floor((p.y - radius) / cellH_)), y1 = static_cast<int>(std::floor((p.y + radius) / cellH_));
        // in a world smaller than the search, visit each cell once and wrap each offset instead
        const bool wrapX = x1 - x0 + 1 > cols_, wrapY = y1 - y0 + 1 > rows_;
        if (wrapX) x0 = 0, x1 = cols_ - 1;
        if (wrapY) y0 = 0, y1 = rows_ - 1;
        // walk the cells with running wrapped indices, so the loop has no divisions
        const int wx0 = ((x0 % cols_) + cols_) % cols_;
        const float ox0 = width_ * static_cast<float>((x0 - wx0) / cols_);
        int wy = ((y0 % rows_) + rows_) % rows_;
        float oy = height_ * static_cast<float>((y0 - wy) / rows_);
        for (int gy = y0; gy <= y1; ++gy) {
            const int* rowStart = &start_[wy * cols_];
            int wx = wx0;
            float ox = ox0;
            for (int gx = x0; gx <= x1; ++gx) {
                for (int k = rowStart[wx], end = rowStart[wx + 1]; k < end; ++k) {
                    float dx = positions_[k].x + ox - p.x, dy = positions_[k].y + oy - p.y;
                    if (wrapX) dx -= width_ * std::round(dx / width_);
                    if (wrapY) dy -= height_ * std::round(dy / height_);
                    f(items_[k], dx, dy);
                }
                if (++wx == cols_) {
                    wx = 0;
                    ox += width_;
                }
            }
            if (++wy == rows_) {
                wy = 0;
                oy += height_;
            }
        }
    }

private:
    float width_ = 1.f, height_ = 1.f, cellW_ = 1.f, cellH_ = 1.f;
    int cols_ = 1, rows_ = 1;
    std::vector<int> start_, fill_, cellOfItem_, items_;
    std::vector<Vec2> positions_;
};

}  // namespace evo
