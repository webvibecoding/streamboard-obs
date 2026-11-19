// SPDX-License-Identifier: GPL-2.0-or-later
// A harmless process named obs.exe for installer regression tests on disposable CI.
#include <chrono>
#include <thread>
int main() { std::this_thread::sleep_for(std::chrono::minutes(3)); }
