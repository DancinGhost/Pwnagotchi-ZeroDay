#pragma once

// A QWidget that renders the portable pet UiFrame pixel-for-pixel, exactly like the device
// screen: it runs the same immediate-mode painter onto an RGB565 software surface, converts
// to RGB888, and blits a nearest-neighbour-scaled QImage. No Qt drawing of the pet itself —
// the hardware-identical renderer is the single source of truth.

#include <cstring>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QWidget>

#include "zd/render/painter.hpp"
#include "zd/render/surface_rgb565.hpp"
#include "zd/ui_model.hpp"

namespace zd {
namespace linux_gui {

class PetWidget : public QWidget {
    Q_OBJECT
public:
    explicit PetWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(360, 200);
    }

    void setFrame(const UiFrame& frame) {
        render::Rgb565Surface surf(frame.dims.width, frame.dims.height);
        render::paintUiFrame(surf, frame);
        const std::vector<uint8_t> rgb = surf.toRgb888Buffer();
        QImage img(frame.dims.width, frame.dims.height, QImage::Format_RGB888);
        for (int y = 0; y < frame.dims.height; ++y) {
            std::memcpy(img.scanLine(y),
                        rgb.data() + static_cast<std::size_t>(y) * frame.dims.width * 3u,
                        static_cast<std::size_t>(frame.dims.width) * 3u);
        }
        image_ = img;  // deep copy semantics via implicit sharing + scanLine writes above
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (image_.isNull()) {
            return;
        }
        // Preserve aspect ratio, integer-ish scaling, crisp pixels.
        const QSize target = image_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect dst(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2),
                        target);
        p.drawImage(dst, image_);
    }

private:
    QImage image_;
};

}  // namespace linux_gui
}  // namespace zd
