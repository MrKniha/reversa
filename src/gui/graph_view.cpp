#include "gui/graph_view.hpp"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rv::gui {

namespace {
constexpr double H_GAP = 40, V_GAP = 60;

void arrow(QGraphicsScene* sc, QPointF tip, QPointF from, const QPen& pen) {
    QLineF l(from, tip);
    double ang = std::atan2(-l.dy(), l.dx());
    QPointF a = tip - QPointF(std::cos(ang - 0.4) * 9, -std::sin(ang - 0.4) * 9);
    QPointF b = tip - QPointF(std::cos(ang + 0.4) * 9, -std::sin(ang + 0.4) * 9);
    sc->addPolygon(QPolygonF({tip, a, b}), QPen(pen.color()), QBrush(pen.color()));
}
}  // namespace

GraphView::GraphView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void GraphView::set_message(const QString& text, const QString& color) {
    scene()->clear();
    nodes_ = 0;
    auto* t = scene()->addText(text);
    t->setDefaultTextColor(QColor(color));
    t->setTextWidth(420);
    resetTransform();
}

void GraphView::set_graph(const std::vector<GraphNode>& nodes, const std::vector<GraphEdge>& edges, Addr focus,
                          const QString& edge_color, const QString& text_color) {
    QGraphicsScene* sc = scene();
    sc->clear();
    resetTransform();
    nodes_ = nodes.size();
    if (nodes.empty()) return;

    std::map<Addr, size_t> idx;
    for (size_t i = 0; i < nodes.size(); i++) idx[nodes[i].id] = i;
    std::vector<std::vector<size_t>> out(nodes.size()), in(nodes.size());
    std::vector<bool> back(edges.size(), false);
    for (size_t e = 0; e < edges.size(); e++) {
        if (!idx.count(edges[e].from) || !idx.count(edges[e].to)) continue;
        out[idx[edges[e].from]].push_back(e);
    }
    // DFS from the first node (entry/root), then from any unvisited node: edges to a node on the
    // current DFS stack are back edges. Edges marked `back` by the caller are kept as such.
    std::vector<int> state(nodes.size(), 0);  // 0 new, 1 on stack, 2 done
    for (size_t root = 0; root < nodes.size(); root++) {
        if (state[root]) continue;
        std::vector<std::pair<size_t, size_t>> stack{{root, 0}};
        state[root] = 1;
        while (!stack.empty()) {
            auto& [n, k] = stack.back();
            if (k < out[n].size()) {
                size_t e = out[n][k++];
                size_t m = idx[edges[e].to];
                if (state[m] == 1 || edges[e].back) back[e] = true;
                else if (state[m] == 0) {
                    state[m] = 1;
                    stack.push_back({m, 0});
                }
            } else {
                state[n] = 2;
                stack.pop_back();
            }
        }
    }
    // Longest-path ranks over the DAG of forward edges (Kahn order).
    std::vector<int> indeg(nodes.size(), 0), rank(nodes.size(), 0);
    for (size_t e = 0; e < edges.size(); e++)
        if (idx.count(edges[e].from) && idx.count(edges[e].to) && !back[e]) {
            indeg[idx[edges[e].to]]++;
            in[idx[edges[e].to]].push_back(idx[edges[e].from]);
        }
    std::vector<size_t> queue;
    for (size_t i = 0; i < nodes.size(); i++)
        if (!indeg[i]) queue.push_back(i);
    for (size_t qi = 0; qi < queue.size(); qi++) {
        size_t n = queue[qi];
        for (size_t e : out[n]) {
            if (back[e]) continue;
            size_t m = idx[edges[e].to];
            rank[m] = std::max(rank[m], rank[n] + 1);
            if (--indeg[m] == 0) queue.push_back(m);
        }
    }
    int max_rank = *std::max_element(rank.begin(), rank.end());
    std::vector<std::vector<size_t>> rows(size_t(max_rank) + 1);
    for (size_t i = 0; i < nodes.size(); i++) rows[size_t(rank[i])].push_back(i);

    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(font().pointSizeF() * 0.9);
    std::vector<QGraphicsTextItem*> texts(nodes.size());
    std::vector<QSizeF> size(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        auto* t = new QGraphicsTextItem();
        QFont f = mono;
        f.setBold(nodes[i].bold);
        t->setFont(f);
        t->setPlainText(nodes[i].text);
        t->setDefaultTextColor(QColor(text_color));
        texts[i] = t;
        size[i] = t->boundingRect().size() + QSizeF(8, 4);
    }
    // Rank by rank: order by barycenter of the predecessors' x, then place left to right.
    std::vector<double> x(nodes.size(), 0), y(nodes.size(), 0);
    double cur_y = 0, width = 0;
    std::vector<double> row_w(rows.size(), 0);
    for (size_t r = 0; r < rows.size(); r++) {
        auto& row = rows[r];
        if (r > 0) {
            std::vector<std::pair<double, size_t>> key;
            for (size_t n : row) {
                double sum = 0;
                int cnt = 0;
                for (size_t p : in[n]) sum += x[p] + size[p].width() / 2, cnt++;
                key.push_back({cnt ? sum / cnt : 1e18, n});
            }
            std::stable_sort(key.begin(), key.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (size_t k = 0; k < row.size(); k++) row[k] = key[k].second;
        }
        double cx = 0, h = 0;
        for (size_t n : row) {
            x[n] = cx;
            y[n] = cur_y;
            cx += size[n].width() + H_GAP;
            h = std::max(h, size[n].height());
        }
        row_w[r] = cx - H_GAP;
        width = std::max(width, row_w[r]);
        cur_y += h + V_GAP;
    }
    for (size_t r = 0; r < rows.size(); r++)
        for (size_t n : rows[r]) x[n] += (width - row_w[r]) / 2;  // center each row

    QGraphicsItem* focus_item = nullptr;
    for (size_t i = 0; i < nodes.size(); i++) {
        QPen pen{QColor(nodes[i].border)};
        pen.setWidthF(nodes[i].id == focus ? 2.5 : 1.2);
        auto* box = sc->addRect(QRectF(x[i], y[i], size[i].width(), size[i].height()), pen, QBrush(QColor(nodes[i].fill)));
        box->setData(0, QVariant::fromValue<qulonglong>(nodes[i].id));
        texts[i]->setParentItem(box);
        texts[i]->setPos(x[i] + 4, y[i] + 2);
        texts[i]->setData(0, QVariant::fromValue<qulonglong>(nodes[i].id));
        box->setToolTip(QStringLiteral("0x%1").arg(nodes[i].id, 0, 16));
        if (nodes[i].id == focus) focus_item = box;
    }
    for (size_t e = 0; e < edges.size(); e++) {
        if (!idx.count(edges[e].from) || !idx.count(edges[e].to)) continue;
        size_t a = idx[edges[e].from], b = idx[edges[e].to];
        QPen pen{QColor(edges[e].color.isEmpty() ? edge_color : edges[e].color)};
        pen.setWidthF(1.3);
        QPainterPath path;
        QPointF from(x[a] + size[a].width() / 2, y[a] + size[a].height());
        QPointF to(x[b] + size[b].width() / 2, y[b]);
        QPointF before;
        if (back[e] || rank[b] <= rank[a]) {
            pen.setStyle(Qt::DashLine);
            double side = std::max(x[a] + size[a].width(), x[b] + size[b].width()) + 25;
            from = QPointF(x[a] + size[a].width(), y[a] + size[a].height() / 2);
            to = QPointF(x[b] + size[b].width(), y[b] + size[b].height() / 2);
            path.moveTo(from);
            path.cubicTo(QPointF(side + 30, from.y()), QPointF(side + 30, to.y()), to);
            before = QPointF(side, to.y());
        } else {
            path.moveTo(from);
            double mid = (from.y() + to.y()) / 2;
            path.cubicTo(QPointF(from.x(), mid), QPointF(to.x(), mid), to);
            before = QPointF(to.x(), mid);
        }
        sc->addPath(path, pen);
        arrow(sc, to, before, pen);
    }
    sc->setSceneRect(sc->itemsBoundingRect().adjusted(-30, -30, 30, 30));
    if (focus_item && nodes.size() > 30) centerOn(focus_item);
    else fit();
}

void GraphView::fit() {
    if (!scene()->items().isEmpty()) {
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        if (transform().m11() > 1.0) resetTransform();  // never blow small graphs up
    }
}

void GraphView::wheelEvent(QWheelEvent* e) {
    double f = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    scale(f, f);
}

std::optional<Addr> GraphView::node_at(const QPoint& pos) const {
    for (auto* it : items(pos)) {
        QVariant v = it->data(0);
        if (v.isValid()) return Addr(v.toULongLong());
    }
    return std::nullopt;
}

void GraphView::mousePressEvent(QMouseEvent* e) {
    if (auto id = node_at(e->pos())) Q_EMIT nodeSelected(*id);
    QGraphicsView::mousePressEvent(e);
}

void GraphView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (auto id = node_at(e->pos())) Q_EMIT nodeActivated(*id);
    QGraphicsView::mouseDoubleClickEvent(e);
}

}  // namespace rv::gui
