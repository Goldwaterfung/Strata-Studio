// src/Presentation/views/playlist/waveform/TileRenderWorker.cpp
#include "TileRenderWorker.h"
#include <QPainter>
#include <QPen>
#include <QColor>
#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#include <pthread.h>
#endif

#include <QDebug>

namespace presentation::views {

TileRenderWorker::TileRenderWorker(bridge::IWaveformCacheProvider* waveformCache, QObject* parent)
    : QObject(parent)
    , waveformCache_(waveformCache)
{
    static bool registered = false;
    if (!registered) {
        qRegisterMetaType<presentation::views::TileKey>("presentation::views::TileKey");
        registered = true;
    }
    initShimmer();
}

TileRenderWorker::~TileRenderWorker() {
    stop();
}

void TileRenderWorker::start() {
    if (running_.exchange(true)) {
        return;
    }
    renderThread_ = std::thread(&TileRenderWorker::renderLoop, this);
}

void TileRenderWorker::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
}

bool TileRenderWorker::enqueue(const TileRequest& req) {
    return requestQueue_.push(req);
}

const QPixmap& TileRenderWorker::getShimmerPixmap() {
    if (!shimmerPixmapInitialized_) {
        shimmerPixmap_ = QPixmap::fromImage(shimmer_);
        shimmerPixmapInitialized_ = true;
    }
    return shimmerPixmap_;
}

void TileRenderWorker::renderLoop() {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif

    TileRequest req;
    while (running_.load(std::memory_order_relaxed)) {
        if (requestQueue_.pop(req)) {
            renderTile(req);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void TileRenderWorker::initShimmer() {
    shimmer_ = QImage(256, 1024, QImage::Format_ARGB32_Premultiplied);
    shimmer_.fill(Qt::transparent);
}

void TileRenderWorker::renderTile(const TileRequest& req) {
    if (!waveformCache_) {
        return;
    }

    const MediaID mediaHandle = MediaID::fromRaw(req.key.mediaId);
    bridge::WaveformSegment segment = waveformCache_->getPeakDataForViewport(
        mediaHandle, req.fileStartFrame, req.fileEndFrame, req.pixelWidth);

    if (!segment.isLoaded || !segment.peaks) {
        waveformCache_->requestWaveformLoad(mediaHandle);
        emit tileRendered(req.key, QImage());
        return;
    }

    // Render into local QImage (avoiding cross-thread data races on scratch buffers)
    QImage image(static_cast<int>(req.pixelWidth), static_cast<int>(req.pixelHeight), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, false);

    const double midY = static_cast<double>(req.pixelHeight) * 0.5;
    const double halfH = static_cast<double>(req.pixelHeight) * 0.45;  // leave 5% margin
    const double xStep = static_cast<double>(req.pixelWidth) / static_cast<double>(req.pixelWidth); // Always 1.0

    const QColor waveColor = QColor::fromRgba(req.key.colorARGB);
    QPen wavePen(waveColor, std::max(1.0, xStep));
    p.setPen(wavePen);

    for (uint32_t i = 0; i < req.pixelWidth && i < segment.sampleCount; ++i) {
        const double x = static_cast<double>(i) * xStep;
        const double maxY = midY - static_cast<double>(segment.peaks[i].maxVal) * halfH;
        const double minY = midY - static_cast<double>(segment.peaks[i].minVal) * halfH;
        p.drawLine(QPointF(x, maxY), QPointF(x, minY));
    }
    p.end();

    emit tileRendered(req.key, image);
}

} // namespace presentation::views
