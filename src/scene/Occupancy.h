#pragma once

#include "hand/Polyline.h"

#include <QRectF>

#include <cstdint>
#include <vector>

// Mapa de ocupação do canvas: uma grade de 1 unidade por célula que diz onde já
// há giz. É a "visão espacial" do motor — consultar a grade custa o tamanho da
// área e evita comparar o candidato com todos os elementos, um a um.
//
// As consultas usam uma tabela de somas acumuladas (montada só quando a grade
// muda), então testar se uma caixa está livre é O(1).
class Occupancy
{
public:
    void reset(double width, double height);
    void ensureHeight(double height);
    void clear();

    double width() const { return m_width; }
    double height() const { return m_height; }

    // Caixa inteira (elementos comuns)
    void markBox(const QRectF &box);
    // Só a faixa ao longo do traço (linhas, setas, conexões e traços do mouse):
    // a caixa de um traço diagonal cobre uma área que ele não ocupa
    void markPath(const Polyline &line, double thickness);

    bool isFree(const QRectF &box) const;

private:
    int cells(const QRectF &box, int *x0, int *y0, int *x1, int *y1) const;
    void buildSums() const;

    double m_width = 0.0;
    double m_height = 0.0;
    int m_columns = 0;
    int m_rows = 0;
    std::vector<std::uint8_t> m_cells;
    mutable std::vector<int> m_sums;   // somas acumuladas (columns+1) × (rows+1)
    mutable bool m_dirty = true;
};
