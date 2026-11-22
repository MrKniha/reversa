// Zoomable graph canvas for the control flow graph and the call graph.
//
// Layout: layered drawing (Sugiyama style, simplified): back edges are ignored for ranking, nodes
// get the longest-path rank from the roots, one barycenter pass orders each rank to reduce
// crossings. Good enough for functions up to a few thousand blocks.
#pragma once

#include <QGraphicsView>
#include <QString>
#include <optional>
#include <vector>

#include "core/model.hpp"

namespace rv::gui {

struct GraphNode {
    Addr id = 0;
    QString text;         // monospace, may contain several lines
    QString border;       // color
    QString fill;         // color
    bool bold = false;
};
struct GraphEdge {
    Addr from = 0, to = 0;
    QString color;
    bool back = false;  // loop edge; drawn dashed and ignored by the ranking
};

class GraphView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(QWidget* parent = nullptr);
    void set_graph(const std::vector<GraphNode>& nodes, const std::vector<GraphEdge>& edges, Addr focus,
                   const QString& edge_color, const QString& text_color);
    void set_message(const QString& text, const QString& color);
    void fit();
    size_t node_count() const { return nodes_; }

Q_SIGNALS:
    void nodeActivated(Addr id);  // double click
    void nodeSelected(Addr id);   // single click

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    std::optional<Addr> node_at(const QPoint& pos) const;
    size_t nodes_ = 0;
};

}  // namespace rv::gui
