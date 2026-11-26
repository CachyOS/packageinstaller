// SPDX-FileCopyrightText: 2019 Marco Martin <mart@kde.org>
// SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later
#include "wheelscroll.hpp"

#include <QGuiApplication>
#include <QQmlInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QWheelEvent>

#include <algorithm>   // for max, min, fold_left
#include <cmath>       // for abs, sqrt, round, lround
#include <functional>  // for plus
#include <ranges>      // for views

namespace {

// Kirigami's numbers: a coast runs 2.5 times the finger speed and slows down at 4000 px/s² times that
constexpr qreal kSpeedFactor  = 2.5;
constexpr qreal kDeceleration = 4000 * kSpeedFactor;
constexpr qsizetype kSamples  = 6;
// fingers that rested this long before lifting do not fling
constexpr quint64 kRestMs = 50;
constexpr qreal kLineStep = 40;
constexpr int kGlideMs    = 140;

}  // namespace

namespace wheelscroll {

auto velocity(std::span<const Sample> samples, quint64 endTime) noexcept -> qreal {
    if (samples.size() < 3 || endTime > samples.back().time + kRestMs) {
        return 0;
    }
    // the first movement only marks when the timing starts
    const auto moved   = std::ranges::fold_left(samples | std::views::drop(1) | std::views::transform(&Sample::delta), 0.0, std::plus{});
    const auto elapsed = std::max<quint64>(samples.back().time - samples.front().time, 1);
    return moved * 1000 / static_cast<qreal>(elapsed);
}

auto coast(qreal from, qreal velocity, qreal top, qreal bottom) noexcept -> Coast {
    const qreal time     = std::abs(velocity) / kDeceleration;
    const qreal end      = from + velocity * time / 2;
    const qreal to       = std::max(top, std::min(end, bottom));
    const qreal progress = end == from ? 0 : std::clamp((to - from) / (end - from), 0.0, 1.0);
    if (progress == 0) {
        return {from, 0};
    }
    // a stop at the edge plays only the start of the OutQuad curve t(2 - t)
    return {to, static_cast<int>(std::lround(time * (1 - std::sqrt(1 - progress)) * 1000))};
}

auto aligned(qreal y, qreal devicePixelRatio) noexcept -> qreal {
    return devicePixelRatio > 0 ? std::round(y * devicePixelRatio) / devicePixelRatio : y;
}

}  // namespace wheelscroll

WheelScroll::WheelScroll(QObject* parent) : QObject(parent) {
    m_glide.setEasingCurve(QEasingCurve::OutCubic);
    m_coast.setEasingCurve(QEasingCurve::OutQuad);
    for (auto* animation : {&m_glide, &m_coast}) {
        connect(animation, &QVariantAnimation::valueChanged, this,
            [this, animation](const QVariant& value) { follow(*animation, value.toReal()); });
    }
    connect(&m_coast, &QAbstractAnimation::stateChanged, this, [this](QAbstractAnimation::State state) {
        if (state == QAbstractAnimation::Stopped && m_window) {
            m_window->removeEventFilter(this);
            m_window = nullptr;
        }
    });
}

void WheelScroll::componentComplete() {
    // a non-item object declared inside a Flickable gets the Flickable as its parent
    auto* view = qobject_cast<QQuickItem*>(parent());
    if (view == nullptr || !view->inherits("QQuickFlickable")) {
        qmlWarning(this) << "WheelScroll must be declared inside a Flickable";
        return;
    }
    m_view = view;
    m_view->installEventFilter(this);
    // a page covered by another stays where it was left
    connect(m_view, &QQuickItem::visibleChanged, this, [this] {
        if (m_view && !m_view->isVisible()) {
            stop();
        }
    });
}

bool WheelScroll::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_view && event->type() == QEvent::Wheel) {
        return wheel(static_cast<QWheelEvent*>(event));
    }
    if (watched == m_window && event->type() == QEvent::TouchBegin) {
        stop();
    } else if (watched == m_window && event->type() == QEvent::MouseButtonPress) {
        stop();
        // the click that catches the page does not also open what is under it
        event->accept();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

bool WheelScroll::wheel(QWheelEvent* event) {
    const auto phase = event->phase();
    if (phase == Qt::ScrollEnd) {
        if (m_samples.isEmpty()) {
            return false;
        }
        const auto speed = wheelscroll::velocity(m_samples, event->timestamp());
        m_samples.clear();
        if (speed != 0) {
            startCoast(speed * kSpeedFactor);
        }
        event->accept();
        return true;
    }
    if (bottom() <= top()) {
        return false;
    }
    const QPoint angle = event->angleDelta();
    // X11 libinput gives a mouse wheel its angle as a pixel distance as well
    const QPoint pixels = event->pixelDelta() == angle ? QPoint{} : event->pixelDelta();

    // Fingers on a touchpad come with a phase and exact distances. Every one of their events is
    // taken, or the Flickable follows part of the swipe itself and jumps back.
    if (phase != Qt::NoScrollPhase && (pixels.y() != 0 || angle.y() == 0)) {
        stop();
        if (phase == Qt::ScrollBegin || phase == Qt::ScrollMomentum) {
            // momentum comes from the system (macOS), which coasts by itself
            m_samples.clear();
        }
        if (pixels.y() != 0) {
            if (phase != Qt::ScrollMomentum) {
                if (m_samples.size() == kSamples) {
                    m_samples.removeFirst();
                }
                m_samples.append({-static_cast<qreal>(pixels.y()), event->timestamp()});
            }
            scrollTo(contentY() - pixels.y());
        }
        event->accept();
        return true;
    }

    // sideways movement belongs to whatever is under the pointer
    if ((event->modifiers() & Qt::ShiftModifier) || std::abs(angle.x()) > std::abs(angle.y())) {
        return false;
    }
    m_samples.clear();
    const qreal step = angle.y() / 120.0 * QGuiApplication::styleHints()->wheelScrollLines() * kLineStep;
    const qreal from = m_glide.state() == QAbstractAnimation::Running ? m_glide.endValue().toReal() : contentY();
    animate(m_glide, std::max(top(), std::min(from - step, bottom())), kGlideMs);
    event->accept();
    return true;
}

void WheelScroll::startCoast(qreal velocity) {
    const auto [to, duration] = wheelscroll::coast(contentY(), velocity, top(), bottom());
    animate(m_coast, to, duration);
    if (m_coast.state() == QAbstractAnimation::Running && m_view->window() != nullptr) {
        m_window = m_view->window();
        m_window->installEventFilter(this);
    }
}

void WheelScroll::animate(QVariantAnimation& animation, qreal to, int duration) {
    stop();
    m_written = contentY();
    if (duration <= 0 || std::abs(to - m_written) < 0.5) {
        return;
    }
    animation.setStartValue(m_written);
    animation.setEndValue(to);
    animation.setDuration(duration);
    animation.start();
}

void WheelScroll::follow(QVariantAnimation& animation, qreal y) {
    // a drag, a key or a jump to the top moved the page
    if (!m_view || std::abs(contentY() - m_written) > 0.5) {
        animation.stop();
        return;
    }
    scrollTo(y);
}

void WheelScroll::scrollTo(qreal y) {
    const auto* window = m_view->window();
    const qreal ratio  = window != nullptr ? window->effectiveDevicePixelRatio() : qGuiApp->devicePixelRatio();
    m_view->setProperty("contentY", std::max(top(), std::min(wheelscroll::aligned(y, ratio), bottom())));
    m_written = contentY();
}

void WheelScroll::stop() {
    m_glide.stop();
    m_coast.stop();
}

qreal WheelScroll::contentY() const {
    return m_view->property("contentY").toReal();
}

qreal WheelScroll::top() const {
    return m_view->property("originY").toReal() - m_view->property("topMargin").toReal();
}

qreal WheelScroll::bottom() const {
    return m_view->property("originY").toReal() + m_view->property("contentHeight").toReal()
        + m_view->property("bottomMargin").toReal() - m_view->height();
}
