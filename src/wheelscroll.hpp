// SPDX-FileCopyrightText: 2019 Marco Martin <mart@kde.org>
// SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-or-later
//
// Touchpad inertia and pixel rounding follow Kirigami's WheelHandler.
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

#include <span>  // for span

class QQuickItem;
class QQuickWindow;
class QWheelEvent;

namespace wheelscroll {

/// One finger movement: the contentY change and the event time in ms.
struct Sample {
    qreal delta;
    quint64 time;
};

/// Where a coast ends and how long it takes in ms.
struct Coast {
    qreal to;
    int duration;
};

/// Finger speed in contentY px/s over `samples`, 0 when there are too few or the fingers
/// rested before they lifted at `endTime`.
[[nodiscard]] auto velocity(std::span<const Sample> samples, quint64 endTime) noexcept -> qreal;

/// Coast from `from` at `velocity` with constant deceleration, stopping inside [top, bottom].
[[nodiscard]] auto coast(qreal from, qreal velocity, qreal top, qreal bottom) noexcept -> Coast;

/// `y` rounded to a device pixel, so text is not drawn between pixels.
[[nodiscard]] auto aligned(qreal y, qreal devicePixelRatio) noexcept -> qreal;

}  // namespace wheelscroll

/// Wheel and touchpad scrolling for the Flickable (or ListView, GridView) it is declared in.
/// A wheel notch glides `wheelScrollLines` lines, touchpad fingers move the page 1:1 and it
/// coasts after a swipe, since Wayland sends no momentum events.
class WheelScroll : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

 public:
    explicit WheelScroll(QObject* parent = nullptr);

    void classBegin() override { }
    void componentComplete() override;

 protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

 private:
    bool wheel(QWheelEvent* event);
    void startCoast(qreal velocity);
    void animate(QVariantAnimation& animation, qreal to, int duration);
    void follow(QVariantAnimation& animation, qreal y);
    void scrollTo(qreal y);
    void stop();

    [[nodiscard]] qreal contentY() const;
    [[nodiscard]] qreal top() const;
    [[nodiscard]] qreal bottom() const;

    QPointer<QQuickItem> m_view;
    // only watched while coasting: a press there stops the page
    QPointer<QQuickWindow> m_window;
    QVariantAnimation m_glide;
    QVariantAnimation m_coast;
    QList<wheelscroll::Sample> m_samples;
    // what was written last: a different contentY means something else moved the page
    qreal m_written = 0;
};
