

#include "scenes-dock.hpp"

#include <QMenu>
#include <QToolBar>
#include <QWidgetAction>

#include "name-dialog.hpp"
#include "obs-module.h"
#include "vertical-canvas.hpp"

void CanvasScenesDock::SetGridMode(bool checked)
{
	if (checked) {
		sceneList->setResizeMode(QListView::Adjust);
		sceneList->setViewMode(QListView::IconMode);
		sceneList->setUniformItemSizes(true);
		sceneList->setStyleSheet("*{padding: 0; margin: 0;}");
	} else {
		sceneList->setViewMode(QListView::ListMode);
		sceneList->setResizeMode(QListView::Fixed);
		sceneList->setStyleSheet("");
	}
}

bool CanvasScenesDock::IsGridMode()
{
	return sceneList->viewMode() == QListView::IconMode;
}

void CanvasScenesDock::ShowScenesContextMenu(QListWidgetItem *item)
{
	auto menu = QMenu(this);
	auto a =
		menu.addAction(QString::fromUtf8(obs_module_text("GridMode")),
				[this](bool checked) { SetGridMode(checked); });
	a->setCheckable(true);
	a->setChecked(IsGridMode());
	menu.addAction(QString::fromUtf8(obs_module_text("Add")),
			[this] { canvasDock->AddScene(); });
	if (!item) {
		menu.exec(QCursor::pos());
		return;
	}
	menu.addSeparator();
	menu.addAction(QString::fromUtf8(obs_module_text("Duplicate")),
			[this] {
				const auto item = sceneList->currentItem();
				if (!item)
					return;
				canvasDock->AddScene(item->text());
			});
	menu.addAction(QString::fromUtf8(obs_module_text("Remove")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		canvasDock->RemoveScene(item->text());
	});
	menu.addAction(QString::fromUtf8(obs_module_text("Rename")), [this] {
		const auto item = sceneList->currentItem();
		if (!item)
			return;
		std::string name = item->text().toUtf8().constData();
		obs_source_t *source = obs_get_source_by_name(name.c_str());
		if (!source)
			return;
		obs_source_t *s = nullptr;
		do {
			obs_source_release(s);
			if (!NameDialog::AskForName(this, name)) {
				break;
			}
			s = obs_get_source_by_name(name.c_str());
			if (s)
				continue;
			obs_source_set_name(source, name.c_str());
		} while (s);
		obs_source_release(source);
	});
	auto orderMenu =
		menu.addMenu(QString::fromUtf8(obs_module_text("Order")));
	orderMenu->addAction(QString::fromUtf8(obs_module_text("Up")),
			     [this] { ChangeSceneIndex(true, -1, 0); });
	orderMenu->addAction(
		QString::fromUtf8(obs_module_text("Down")),
		[this] { ChangeSceneIndex(true, 1, sceneList->count() - 1); });
	orderMenu->addAction(QString::fromUtf8(obs_module_text("Top")),
			     [this] { ChangeSceneIndex(false, 0, 0); });
	orderMenu->addAction(
		QString::fromUtf8(obs_module_text("Bottom")),
		[this] { ChangeSceneIndex(false, 1, sceneList->count() - 1); });

	menu.addAction(QString::fromUtf8(obs_module_text("Screenshot")),
			[this] {
				auto item = sceneList->currentItem();
				if (!item)
					return;
				auto s = obs_get_source_by_name(
					item->text().toUtf8().constData());
				if (s) {
					obs_frontend_take_source_screenshot(s);
					obs_source_release(s);
				}
			});
	menu.addAction(QString::fromUtf8(obs_module_text("Filters")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		auto s = obs_get_source_by_name(
			item->text().toUtf8().constData());
		if (s) {
			obs_frontend_open_source_filters(s);
			obs_source_release(s);
		}
	});

	auto linkedScenesMenu = menu.addMenu(
		QString::fromUtf8(obs_module_text("LinkedScenes")));
	connect(linkedScenesMenu, &QMenu::aboutToShow, [linkedScenesMenu, this] {
		linkedScenesMenu->clear();
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *src = scenes.sources.array[i];
			obs_data_t *settings = obs_source_get_settings(src);
			if (!obs_data_get_bool(settings, "custom_size")) {
				auto name = QString::fromUtf8(
					obs_source_get_name(src));
				auto *checkBox =
					new QCheckBox(name, linkedScenesMenu);
				connect(checkBox, &QCheckBox::stateChanged,
					[this, src, checkBox] {
						canvasDock->SetLinkedScene(
							src,
							checkBox->isChecked()
								? sceneList
									  ->currentItem()
									  ->text()
								: "");
					});
				auto *checkableAction =
					new QWidgetAction(linkedScenesMenu);
				checkableAction->setDefaultWidget(checkBox);
				linkedScenesMenu->addAction(checkableAction);

				auto c = obs_data_get_array(settings, "canvas");
				if (c) {
					const auto count =
						obs_data_array_count(c);

					for (size_t i = 0; i < count; i++) {
						auto item = obs_data_array_item(
							c, i);
						if (!item)
							continue;
						if (obs_data_get_int(item,
								     "width") ==
							    canvasDock
								    ->canvas_width &&
						    obs_data_get_int(
							    item, "height") ==
							    canvasDock
								    ->canvas_height) {
							auto sn = QString::fromUtf8(
								obs_data_get_string(
									item,
									"scene"));
							if (sn ==
							    sceneList
								    ->currentItem()
								    ->text()) {
								checkBox->setChecked(
									true);
							}
						}
						obs_data_release(item);
					}

					obs_data_array_release(c);
				}
			}
			obs_data_release(settings);
		}
		obs_frontend_source_list_free(&scenes);
	});
	if (canvasDock->hideScenes) {
		menu.addAction(
			QString::fromUtf8(obs_module_text("OnMainCanvas")),
			[this] {
				auto item = sceneList->currentItem();
				if (!item)
					return;
				auto s = obs_get_source_by_name(
					item->text().toUtf8().constData());
				if (!s)
					return;

				if (obs_frontend_preview_program_mode_active())
					obs_frontend_set_current_preview_scene(
						s);
				else
					obs_frontend_set_current_scene(s);
				obs_source_release(s);
			});
	}
	menu.exec(QCursor::pos());
}

CanvasScenesDock::CanvasScenesDock(CanvasDock *canvas_dock, QWidget *parent)
	: QDockWidget(parent), canvasDock(canvas_dock)
{
	const auto scenesName = canvasDock->objectName() + "Scenes";
	setObjectName(scenesName);
	const auto scenesTitle = canvasDock->windowTitle() + " " +
				 QString::fromUtf8(obs_module_text("Scenes"));
	setWindowTitle(scenesTitle);

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	sceneList = new QListWidget();
	sceneList->setSizePolicy(QSizePolicy::Preferred,
				 QSizePolicy::Expanding);
	sceneList->setFrameShape(QFrame::NoFrame);
	sceneList->setFrameShadow(QFrame::Plain);
	sceneList->setSelectionMode(QAbstractItemView::SingleSelection);
	sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sceneList, &QListWidget::customContextMenuRequested,
		[this](const QPoint &pos) {
			ShowScenesContextMenu(sceneList->itemAt(pos));
		});

	connect(sceneList, &QListWidget::currentItemChanged, [this]() {
		const auto item = sceneList->currentItem();
		if (!item)
			return;
		canvasDock->SwitchScene(item->text());
		if (!item->isSelected())
			item->setSelected(true);
	});
	connect(sceneList, &QListWidget::itemSelectionChanged, [this] {
		const auto item = sceneList->currentItem();
		if (!item)
			return;
		if (!item->isSelected())
			item->setSelected(true);
	});
	mainLayout->addWidget(sceneList, 1);

	auto toolbar = new QToolBar();
	toolbar->setObjectName(QStringLiteral("scenesToolbar"));
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setFloatable(false);
	auto a = toolbar->addAction(
		QIcon(QString::fromUtf8(":/res/images/plus.svg")),
		QString::fromUtf8(obs_module_text("Add")),
		[this] { canvasDock->AddScene(); });
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("addIconSmall")));

	a = toolbar->addAction(QIcon(":/res/images/minus.svg"),
			       QString::fromUtf8(obs_module_text("Remove")),
			       [this] {
				       auto item = sceneList->currentItem();
				       if (!item)
					       return;
				       canvasDock->RemoveScene(item->text());
			       });
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("removeIconSmall")));
	toolbar->addSeparator();
	a = toolbar->addAction(
		QIcon(":/res/images/filter.svg"),
		QString::fromUtf8(obs_module_text("Filters")), [this] {
			auto item = sceneList->currentItem();
			if (!item)
				return;
			auto s = obs_get_source_by_name(
				item->text().toUtf8().constData());
			if (!s)
				return;
			obs_frontend_open_source_filters(s);
			obs_source_release(s);
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("filtersIcon")));
	toolbar->addSeparator();
	a = toolbar->addAction(QIcon(":/res/images/up.svg"),
			       QString::fromUtf8(obs_module_text("Up")),
			       [this] { ChangeSceneIndex(true, -1, 0); });
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("upArrowIconSmall")));
	a = toolbar->addAction(
		QIcon(":/res/images/down.svg"),
		QString::fromUtf8(obs_module_text("Down")),
		[this] { ChangeSceneIndex(true, 1, sceneList->count() - 1); });
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("downArrowIconSmall")));
	mainLayout->addWidget(toolbar, 0);

	auto *dockWidgetContents = new QWidget;
	dockWidgetContents->setObjectName(QStringLiteral("contextContainer"));
	dockWidgetContents->setContentsMargins(0, 0, 0, 0);
	dockWidgetContents->setLayout(mainLayout);

	setWidget(dockWidgetContents);
	hide();
}

void CanvasScenesDock::ChangeSceneIndex(bool relative, int offset,
					int invalidIdx)
{
	int idx = sceneList->currentRow();
	if (idx < 0)
		return;

	auto canvasItem = sceneList->item(idx);
	if (!canvasItem)
		return;
	auto sl = canvasDock->GetGlobalScenesList();
	int row = -1;
	bool hidden = false;
	bool selected = false;
	for (int i = 0; i < sl->count(); i++) {
		auto item = sl->item(i);
		if (item->text() == canvasItem->text()) {
			row = i;
			hidden = item->isHidden();
			selected = item->isSelected();
			break;
		}
	}
	if (row < 0 || row >= sl->count())
		return;

	sl->blockSignals(true);
	QListWidgetItem *item = sl->takeItem(row);
	if (relative) {
		sl->insertItem(row + offset, item);
	} else if (offset == 0) {
		sl->insertItem(offset, item);
	} else {
		sl->insertItem(sl->count(), item);
	}
	item->setHidden(hidden);
	item->setSelected(selected);
	sl->blockSignals(false);

	if (idx == invalidIdx)
		return;

	sceneList->blockSignals(true);
	item = sceneList->takeItem(idx);
	if (relative) {
		sceneList->insertItem(idx + offset, item);
		sceneList->setCurrentRow(idx + offset);
	} else if (offset == 0) {
		sceneList->insertItem(offset, item);
	} else {
		sceneList->insertItem(sceneList->count(), item);
	}
	item->setSelected(true);
	sceneList->blockSignals(false);
}

CanvasScenesDock::~CanvasScenesDock() {}
