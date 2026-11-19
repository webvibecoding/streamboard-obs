// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "core/api.h"
#include "core/game.h"
#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

namespace sb {
class Editor final : public QDialog {
    Q_OBJECT
public:
    Editor(Api &api,Game game,QString platform,QWidget *parent = nullptr);
signals:
    void saved(sb::Game game,QJsonObject mapping);
protected:
    void reject() override;
private:
    Api &api;
    Game game;
    QComboBox *platforms;
    QLineEdit *name,*query;
    QListWidget *items;
    QLabel *message;
    QPushButton *searchButton,*saveButton;
    bool saving = false;
    quint64 searchRevision = 0;
    void search();
    void save();
};
}
