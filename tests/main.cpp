// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
int runCoreTests(int,char **);
int runApiTests(int,char **);
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    return runCoreTests(argc,argv) | runApiTests(argc,argv);
}
