// src/Presentation/views/browser_window/BrowserItem.h
#pragma once

#include <QStyledItemDelegate>

namespace presentation::views {

class BrowserItem : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit BrowserItem(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace presentation::views
