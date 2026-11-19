// SPDX-License-Identifier: GPL-2.0-or-later
// OBS's pinned Qt distribution omits QtTest. This runner uses only public QtCore APIs.
#pragma once
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMetaMethod>
#include <QThread>
#include <QVariant>
#include <functional>
#include <stdexcept>

namespace QTest {
inline bool waitUntil(const std::function<bool()> &predicate,int timeout = 5000) {
    QElapsedTimer clock; clock.start();
    do {
        if(predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents,10); QThread::msleep(2);
    } while(clock.elapsed() < timeout);
    return predicate();
}
inline void qWait(int milliseconds) {
    QElapsedTimer clock; clock.start();
    waitUntil([&] { return clock.elapsed() >= milliseconds; },milliseconds + 20);
}
inline void check(bool success,const char *expression,const char *file,int line) {
    if(!success) throw std::runtime_error(QString("%1:%2: %3").arg(file).arg(line).arg(expression).toStdString());
}
inline int qExec(QObject *test,int,char **) {
    const auto *meta = test->metaObject(); int passed = 0,failed = 0;
    for(int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const auto method = meta->method(i);
        if(method.methodType() != QMetaMethod::Slot || method.parameterCount()) continue;
        try {
            if(!method.invoke(test,Qt::DirectConnection)) throw std::runtime_error("Could not invoke test");
            ++passed; qInfo().noquote() << "PASS" << meta->className() << method.name();
        } catch(const std::exception &error) { ++failed; qCritical().noquote() << "FAIL" << meta->className() << method.name() << error.what(); }
    }
    qInfo() << meta->className() << "passed:" << passed << "failed:" << failed;
    return failed ? 1 : 0;
}
}
class QSignalSpy : public QObject {
public:
    template<class Sender,class... Args>
    QSignalSpy(Sender *sender,void (Sender::*signal)(Args...)) {
        QObject::connect(sender,signal,this,[this](Args... args) { events.append({QVariant::fromValue(args)...}); });
    }
    qsizetype count() const { return events.size(); }
    const QVariantList &at(qsizetype index) const { return events.at(index); }
    const QVariantList &operator[](qsizetype index) const { return events.at(index); }
private:
    QList<QVariantList> events;
};
#define QVERIFY(expression) QTest::check(bool(expression),#expression,__FILE__,__LINE__)
#define QCOMPARE(actual,expected) QTest::check((actual)==(expected),#actual " == " #expected,__FILE__,__LINE__)
#define QTRY_VERIFY(expression) QVERIFY(QTest::waitUntil([&] { return bool(expression); }))
#define QTRY_COMPARE(actual,expected) QVERIFY(QTest::waitUntil([&] { return (actual)==(expected); }))
#define QTRY_COMPARE_WITH_TIMEOUT(actual,expected,timeout) QVERIFY(QTest::waitUntil([&] { return (actual)==(expected); },timeout))
