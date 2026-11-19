// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.h"
#include "ui.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QPointer>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace sb {
Editor::Editor(Api &a,Game g,QString platform,QWidget *parent) : QDialog(parent),api(a),game(std::move(g)) {
    setWindowTitle(text("ChooseCategory")); setWindowIcon(QIcon(":/streamboard.ico"));
    setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal); resize(580,480);
    auto *layout = new QVBoxLayout(this);
    auto *gameLabel = new QLabel(game.name,this); gameLabel->setTextFormat(Qt::PlainText); gameLabel->setWordWrap(true); layout->addWidget(gameLabel);
    auto *form = new QFormLayout;
    name = new QLineEdit(game.name,this); name->setMaxLength(140);
    form->addRow(text("GameName"),name); name->setEnabled(game.source == "exe");
    platforms = new QComboBox(this); platforms->addItem("Twitch","twitch"); platforms->addItem("VK Play Live","vkplaylive");
    if(!platform.isEmpty()) { platforms->setCurrentIndex(platforms->findData(platform)); platforms->setEnabled(false); }
    form->addRow(text("Platform"),platforms); layout->addLayout(form);
    auto *searchRow = new QHBoxLayout;
    query = new QLineEdit(game.name,this); query->setMaxLength(100); searchRow->addWidget(query);
    searchButton = new QPushButton(text("Search"),this); searchRow->addWidget(searchButton); layout->addLayout(searchRow);
    items = new QListWidget(this); layout->addWidget(items);
    message = new QLabel(text("SearchHint"),this); message->setWordWrap(true); message->setTextFormat(Qt::PlainText); layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel,this);
    buttons->button(QDialogButtonBox::Cancel)->setText(text("Cancel"));
    saveButton = buttons->addButton(text("Save"),QDialogButtonBox::AcceptRole); saveButton->setEnabled(false); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&Editor::reject);
    connect(searchButton,&QPushButton::clicked,this,&Editor::search);
    connect(query,&QLineEdit::returnPressed,this,&Editor::search);
    connect(platforms,&QComboBox::currentIndexChanged,this,[this] { ++searchRevision; items->clear(); saveButton->setEnabled(false); });
    connect(items,&QListWidget::currentRowChanged,this,[this](int row) { saveButton->setEnabled(row >= 0 && !saving); });
    connect(saveButton,&QPushButton::clicked,this,&Editor::save);
}
void Editor::reject() { if(!saving) QDialog::reject(); }
void Editor::search() {
    if(saving || query->text().trimmed().isEmpty()) return;
    const auto revision = ++searchRevision;
    QUrlQuery params; params.addQueryItem("platform_id",platforms->currentData().toString()); params.addQueryItem("query",query->text().trimmed());
    searchButton->setEnabled(false); items->clear(); message->setText(text("Loading"));
    const QPointer<Editor> guard(this);
    api.request("GET","categories?" + params.toString(QUrl::FullyEncoded),{},[guard,revision](Response response) {
        if(!guard || guard->searchRevision != revision) return;
        guard->searchButton->setEnabled(true);
        if(!response.ok()) { guard->message->setText(errorText(response.error)); return; }
        const auto categories = response.body["items"].toArray();
        for(const auto &v : categories) {
            const auto category = v.toObject();
            if(category["id"].toString().isEmpty() || category["name"].toString().isEmpty()) continue;
            auto *item = new QListWidgetItem(category["name"].toString(),guard->items); item->setData(Qt::UserRole,category);
        }
        guard->message->setText(guard->items->count() ? text("SelectResult") : text("NoResults"));
    });
}
void Editor::save() {
    auto *item = items->currentItem(); if(!item || saving) return;
    const auto category = item->data(Qt::UserRole).toJsonObject();
    Game selected = game;
    if(selected.source == "exe") { selected.name = name->text().trimmed(); selected.needsName = false; }
    if(selected.name.isEmpty()) { message->setText(text("NameRequired")); return; }
    QJsonObject selectedCategory{{"id",category["id"]},{"name",category["name"]},{"type",category["type"].toString("game")}};
    if(category["image_url"].isString()) selectedCategory.insert("image_url",category["image_url"]);
    saving = true; saveButton->setEnabled(false); searchButton->setEnabled(false); items->setEnabled(false); name->setEnabled(false);
    message->setText(text("Saving")); const QPointer<Editor> guard(this);
    api.request("PUT","mappings",{{"game",selected.json()},{"platform_id",platforms->currentData().toString()},{"category",selectedCategory}},
        [guard,selected](Response response) {
            if(!guard) return;
            guard->saving = false;
            if(!response.ok()) {
                guard->saveButton->setEnabled(true); guard->searchButton->setEnabled(true); guard->items->setEnabled(true);
                guard->name->setEnabled(guard->game.source == "exe"); guard->message->setText(errorText(response.error)); return;
            }
            emit guard->saved(selected,response.body); guard->accept();
        });
}
}
