// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
#include <cstdio>
int runCoreTests(int,char **);
int runApiTests(int,char **);
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    qInstallMessageHandler([](QtMsgType,const QMessageLogContext &,const QString &message) {
        const auto bytes = message.toUtf8(); std::fprintf(stderr,"%s\n",bytes.constData());
    });
    return runCoreTests(argc,argv) | runApiTests(argc,argv);
}
