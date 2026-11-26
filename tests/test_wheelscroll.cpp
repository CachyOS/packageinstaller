// SPDX-License-Identifier: GPL-3.0-or-later
#include "wheelscroll.hpp"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTest>
#include <QWheelEvent>

#include <memory>  // for unique_ptr

using namespace Qt::StringLiterals;
using wheelscroll::Sample;

class TestWheelScroll : public QObject {
    Q_OBJECT

 private slots:
    void initTestCase() { qmlRegisterType<WheelScroll>("Test", 1, 0, "WheelScroll"); }

    void velocity_data() {
        QTest::addColumn<QList<Sample>>("samples");
        QTest::addColumn<quint64>("end");
        QTest::addColumn<qreal>("expected");
        const QList<Sample> steady{{20, 10}, {20, 20}, {20, 30}, {20, 40}, {20, 50}, {20, 60}};
        QTest::newRow("steady swipe") << steady << quint64{65} << qreal{2000};
        QTest::newRow("upwards") << QList<Sample>{{-5, 100}, {-10, 110}, {-10, 120}} << quint64{120} << qreal{-1000};
        QTest::newRow("too few") << QList<Sample>{{20, 10}, {20, 20}} << quint64{20} << qreal{0};
        QTest::newRow("rested before lifting") << steady << quint64{111} << qreal{0};
        QTest::newRow("same timestamp") << QList<Sample>{{1, 5}, {1, 5}, {1, 5}} << quint64{5} << qreal{2000};
    }

    void velocity() {
        QFETCH(QList<Sample>, samples);
        QFETCH(quint64, end);
        QFETCH(qreal, expected);
        QCOMPARE(wheelscroll::velocity(samples, end), expected);
    }

    void coast_data() {
        QTest::addColumn<qreal>("from");
        QTest::addColumn<qreal>("velocity");
        QTest::addColumn<qreal>("bottom");
        QTest::addColumn<qreal>("to");
        QTest::addColumn<int>("duration");
        // 10000 px/s stops after 1 s and 5000 px
        QTest::newRow("free") << qreal{0} << qreal{10000} << qreal{8000} << qreal{5000} << 1000;
        QTest::newRow("upwards") << qreal{6000} << qreal{-10000} << qreal{8000} << qreal{1000} << 1000;
        QTest::newRow("cut short at the bottom") << qreal{0} << qreal{10000} << qreal{3750} << qreal{3750} << 500;
        QTest::newRow("already at the edge") << qreal{8000} << qreal{10000} << qreal{8000} << qreal{8000} << 0;
        QTest::newRow("still") << qreal{100} << qreal{0} << qreal{8000} << qreal{100} << 0;
    }

    void coast() {
        QFETCH(qreal, from);
        QFETCH(qreal, velocity);
        QFETCH(qreal, bottom);
        QFETCH(qreal, to);
        QFETCH(int, duration);
        const auto result = wheelscroll::coast(from, velocity, 0, bottom);
        QCOMPARE(result.to, to);
        QCOMPARE(result.duration, duration);
    }

    void aligned_data() {
        QTest::addColumn<qreal>("y");
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("whole pixels") << qreal{10.3} << qreal{1} << qreal{10};
        QTest::newRow("fractional scale") << qreal{10.3} << qreal{1.5} << qreal{10};
        QTest::newRow("hidpi") << qreal{10.4} << qreal{2} << qreal{10.5};
        QTest::newRow("no ratio") << qreal{10.3} << qreal{0} << qreal{10.3};
    }

    void aligned() {
        QFETCH(qreal, y);
        QFETCH(qreal, ratio);
        QFETCH(qreal, expected);
        QCOMPARE(wheelscroll::aligned(y, ratio), expected);
    }

    void swipeCoasts() {
        const auto view = flickable();
        swipe(view.get(), 65);
        // 6 movements of 20 px, 10 ms apart: 2000 px/s, coasting at 5000 px/s for 0.5 s and 1250 px
        QCOMPARE(view->property("contentY").toReal(), qreal{120});
        QTRY_COMPARE(view->property("contentY").toReal(), qreal{1370});
    }

    void restingFingersDoNotFling() {
        const auto view = flickable();
        swipe(view.get(), 200);
        QTest::qWait(100);
        QCOMPARE(view->property("contentY").toReal(), qreal{120});
    }

    void coastStopsAtTheBottom() {
        const auto view = flickable();
        view->setProperty("contentY", 1700);
        swipe(view.get(), 65);
        QCOMPARE(view->property("contentY").toReal(), qreal{1820});
        QTRY_COMPARE(view->property("contentY").toReal(), qreal{1900});
    }

    void clickCatchesTheCoast() {
        QQuickWindow window;
        window.resize(100, 100);
        const auto view = flickable();
        view->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* area = view->findChild<QObject*>(u"area"_s);
        QTest::mouseClick(&window, Qt::LeftButton, {}, {50, 50});
        QCOMPARE(area->property("clicks").toInt(), 1);

        swipe(view.get(), 65);
        QTRY_VERIFY(view->property("contentY").toReal() > 200);
        QTest::mouseClick(&window, Qt::LeftButton, {}, {50, 50});
        const qreal caught = view->property("contentY").toReal();
        QTest::qWait(100);
        QCOMPARE(view->property("contentY").toReal(), caught);
        QCOMPARE(area->property("clicks").toInt(), 1);
    }

    void otherMovementStopsTheCoast() {
        const auto view = flickable();
        swipe(view.get(), 65);
        QTRY_VERIFY(view->property("contentY").toReal() > 200);
        view->setProperty("contentY", 0);
        QTest::qWait(100);
        QCOMPARE(view->property("contentY").toReal(), qreal{0});
    }

    void newSwipeStopsTheCoast() {
        const auto view = flickable();
        swipe(view.get(), 65);
        QTRY_VERIFY(view->property("contentY").toReal() > 200);
        send(view.get(), {}, Qt::ScrollBegin, 1000);
        const qreal caught = view->property("contentY").toReal();
        QTest::qWait(100);
        QCOMPARE(view->property("contentY").toReal(), caught);
    }

    void wheelGlides() {
        const auto view   = flickable();
        const qreal notch = QGuiApplication::styleHints()->wheelScrollLines() * 40.0;
        send(view.get(), {}, Qt::NoScrollPhase, 0, {0, -120});
        send(view.get(), {}, Qt::NoScrollPhase, 10, {0, -120});
        QTRY_COMPARE(view->property("contentY").toReal(), 2 * notch);
        send(view.get(), {}, Qt::NoScrollPhase, 500, {0, 120});
        QTRY_COMPARE(view->property("contentY").toReal(), notch);
    }

    void x11WheelIsNotAFinger() {
        const auto view   = flickable();
        const qreal notch = QGuiApplication::styleHints()->wheelScrollLines() * 40.0;
        // libinput on X11 reports the angle as the pixel distance too
        send(view.get(), {0, -120}, Qt::ScrollUpdate, 0, {0, -120});
        QTRY_COMPARE(view->property("contentY").toReal(), notch);
    }

 private:
    std::unique_ptr<QQuickItem> flickable() {
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick\nimport Test\nFlickable {\n"
                          "    width: 100; height: 100; contentHeight: 2000\n"
                          "    WheelScroll {}\n"
                          "    MouseArea { objectName: \"area\"; property int clicks: 0; width: 100; height: 2000; onClicked: clicks++ }\n"
                          "}",
            {});
        std::unique_ptr<QQuickItem> view(qobject_cast<QQuickItem*>(component.create()));
        if (view == nullptr) {
            qFatal("%s", qPrintable(component.errorString()));
        }
        return view;
    }

    // Begin, then six 20 px movements 10 ms apart, and the fingers lifting at `end`
    static void swipe(QQuickItem* view, quint64 end) {
        send(view, {}, Qt::ScrollBegin, 0);
        for (quint64 time = 10; time <= 60; time += 10) {
            send(view, {0, -20}, Qt::ScrollUpdate, time);
        }
        send(view, {}, Qt::ScrollEnd, end);
    }

    static void send(QQuickItem* view, QPoint pixels, Qt::ScrollPhase phase, quint64 time, QPoint angle = {}) {
        if (angle.isNull()) {
            angle = pixels * 2;
        }
        QWheelEvent event({50, 50}, {50, 50}, pixels, angle, Qt::NoButton, Qt::NoModifier, phase, false);
        event.setTimestamp(time);
        QCoreApplication::sendEvent(view, &event);
    }

    QQmlEngine m_engine;
};

QTEST_MAIN(TestWheelScroll)
#include "test_wheelscroll.moc"
