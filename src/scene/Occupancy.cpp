#include "Occupancy.h"

#include <algorithm>
#include <cmath>

void Occupancy::reset(double width, double height)
{
    m_width = width;
    m_height = height;
    m_columns = std::max(1, int(std::ceil(width)));
    m_rows = std::max(1, int(std::ceil(height)));
    m_cells.assign(std::size_t(m_columns) * std::size_t(m_rows), 0u);
    m_dirty = true;
}

void Occupancy::ensureHeight(double height)
{
    if (height <= m_height)
        return;
    m_height = height;
    m_rows = std::max(1, int(std::ceil(height)));
    m_cells.resize(std::size_t(m_columns) * std::size_t(m_rows), 0u);
    m_dirty = true;
}

void Occupancy::clear()
{
    std::fill(m_cells.begin(), m_cells.end(), 0u);
    m_dirty = true;
}

int Occupancy::cells(const QRectF &box, int *x0, int *y0, int *x1, int *y1) const
{
    *x0 = std::clamp(int(std::floor(box.left())), 0, m_columns - 1);
    *y0 = std::clamp(int(std::floor(box.top())), 0, m_rows - 1);
    *x1 = std::clamp(int(std::ceil(box.right())) - 1, 0, m_columns - 1);
    *y1 = std::clamp(int(std::ceil(box.bottom())) - 1, 0, m_rows - 1);
    *x1 = std::max(*x0, *x1);
    *y1 = std::max(*y0, *y1);
    return (*x1 - *x0 + 1) * (*y1 - *y0 + 1);
}

void Occupancy::markBox(const QRectF &box)
{
    if (box.isNull())
        return;
    int x0, y0, x1, y1;
    cells(box, &x0, &y0, &x1, &y1);
    for (int y = y0; y <= y1; ++y)
        std::fill(m_cells.begin() + std::size_t(y) * m_columns + x0,
                  m_cells.begin() + std::size_t(y) * m_columns + x1 + 1, std::uint8_t(1));
    m_dirty = true;
}

void Occupancy::markPath(const Polyline &line, double thickness)
{
    const double half = std::max(0.5, thickness / 2.0);
    for (std::size_t i = 1; i < line.size(); ++i) {
        const QPointF a = line[i - 1], b = line[i];
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int steps = std::max(1, int(std::ceil(length)));
        for (int s = 0; s <= steps; ++s) {
            const QPointF p = a + (b - a) * (double(s) / steps);
            markBox(QRectF(p.x() - half, p.y() - half, 2 * half, 2 * half));
        }
    }
}

void Occupancy::buildSums() const
{
    m_sums.assign(std::size_t(m_columns + 1) * std::size_t(m_rows + 1), 0);
    for (int y = 0; y < m_rows; ++y) {
        int rowSum = 0;
        const std::uint8_t *row = m_cells.data() + std::size_t(y) * m_columns;
        int *out = m_sums.data() + std::size_t(y + 1) * (m_columns + 1);
        const int *above = m_sums.data() + std::size_t(y) * (m_columns + 1);
        for (int x = 0; x < m_columns; ++x) {
            rowSum += row[x];
            out[x + 1] = above[x + 1] + rowSum;
        }
    }
    m_dirty = false;
}

bool Occupancy::isFree(const QRectF &box) const
{
    if (m_cells.empty())
        return true;
    if (m_dirty)
        buildSums();
    int x0, y0, x1, y1;
    cells(box, &x0, &y0, &x1, &y1);
    const int stride = m_columns + 1;
    const int total = m_sums[std::size_t(y1 + 1) * stride + x1 + 1]
                    - m_sums[std::size_t(y0) * stride + x1 + 1]
                    - m_sums[std::size_t(y1 + 1) * stride + x0]
                    + m_sums[std::size_t(y0) * stride + x0];
    return total == 0;
}
