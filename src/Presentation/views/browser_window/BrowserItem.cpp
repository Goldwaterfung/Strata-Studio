// src/Presentation/views/browser_window/BrowserItem.cpp
#include "BrowserItem.h"
#include "../theme.h"
#include <QPainter>
#include <QPainterPath>

namespace presentation::views {

BrowserItem::BrowserItem(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void BrowserItem::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    bool isCategory = index.data(Qt::UserRole + 6).toBool();

    if (isCategory) {
        opt.font = theme::Font::primary(theme::Font::SizeSecondary, QFont::Bold);
        if (index.column() == 0) {
            opt.text = opt.text.toUpper();
        }
        QStyledItemDelegate::paint(painter, opt, index);
        return;
    }

    // Custom painting for non-categories
    painter->save();
    
    // Draw background (selection/hover)
    QStyle::State state = opt.state;
    if (state & QStyle::State_Selected) {
        painter->fillRect(opt.rect, theme::Color::BgControl);
    } else if (state & QStyle::State_MouseOver) {
        QColor hoverColor = theme::Color::BgControl;
        hoverColor.setAlphaF(0.4f);
        painter->fillRect(opt.rect, hoverColor);
    }
    
    if (index.column() == 0) {
        int itemType = index.data(Qt::UserRole + 2).toInt();
        double x = static_cast<double>(opt.rect.x()) + 4.0;
        double y = static_cast<double>(opt.rect.y()) + (static_cast<double>(opt.rect.height()) - 14.0) / 2.0;
        QRectF iconRect(x, y, 14.0, 14.0);
        
        QColor iconColor = theme::Color::TextMuted;
        QPainterPath path;
        
        if (itemType == 0) { // Folder
            path.addRoundedRect(QRectF(iconRect.x(), iconRect.y() + 2.0, 14.0, 10.0), 1.0, 1.0);
            path.addRect(QRectF(iconRect.x(), iconRect.y(), 6.0, 3.0));
        } else if (itemType == 1) { // AudioFile
            path.addEllipse(iconRect.center(), 5.0, 5.0);
            path.addEllipse(iconRect.center(), 2.0, 2.0);
        } else if (itemType == 2) { // MidiFile
            path.addRect(QRectF(iconRect.x() + 2.0, iconRect.y() + 2.0, 10.0, 10.0));
            path.moveTo(iconRect.x() + 4.0, iconRect.y() + 4.0);
            path.lineTo(iconRect.x() + 10.0, iconRect.y() + 10.0);
        } else if (itemType == 6 || itemType == 7) { // Plugin
            path.addRect(QRectF(iconRect.x() + 2.0, iconRect.y() + 4.0, 10.0, 6.0));
            path.addEllipse(QPointF(iconRect.x() + 4.0, iconRect.y() + 7.0), 1.0, 1.0);
            path.addEllipse(QPointF(iconRect.x() + 10.0, iconRect.y() + 7.0), 1.0, 1.0);
        } else {
            path.addRect(QRectF(iconRect.x() + 3.0, iconRect.y() + 2.0, 8.0, 10.0));
        }
        
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(iconColor, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        
        // Draw Text
        double textOffset = (x + 22.0) - static_cast<double>(opt.rect.x());
        QRectF textRect(x + 22.0, static_cast<double>(opt.rect.y()), static_cast<double>(opt.rect.width()) - textOffset, static_cast<double>(opt.rect.height()));
        QColor textColor = theme::Color::TextPrimary;
        uint32_t colorARGB = index.data(Qt::UserRole + 3).toUInt();
        if (colorARGB != 0) {
            textColor = QColor(
                static_cast<int>((colorARGB >> 16) & 0xFF),
                static_cast<int>((colorARGB >> 8) & 0xFF),
                static_cast<int>(colorARGB & 0xFF),
                static_cast<int>((colorARGB >> 24) & 0xFF)
            );
        }
        painter->setPen(textColor);
        painter->setFont(opt.font);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        
    }
    
    painter->restore();
}

} // namespace presentation::views
