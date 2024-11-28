

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

void CanvasScenesDock::ShowScenesContextMenu(QListWidgetItem *widget_item)
{
	auto menu = QMenu(this);
	auto a = menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.GridMode")),
				[this](bool checked) { SetGridMode(checked); });
	a->setCheckable(true);
	a->setChecked(IsGridMode());
	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Add")), [this] { canvasDock->AddScene(); });
	if (!widget_item) {
		menu.exec(QCursor::pos());
		return;
	}
	menu.addSeparator();
	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Duplicate")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		canvasDock->AddScene(item->text());
	});
	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Remove")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		canvasDock->RemoveScene(item->text());
	});
	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Rename")), [this] {
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
			if (!NameDialog::AskForName(this, QString::fromUtf8(obs_module_text("SceneName")), name)) {
				break;
			}
			s = obs_get_source_by_name(name.c_str());
			if (s)
				continue;
			obs_source_set_name(source, name.c_str());
		} while (s);
		obs_source_release(source);
	});
	auto orderMenu = menu.addMenu(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order")));
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveUp")),
			     [this] { ChangeSceneIndex(true, -1, 0); });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveDown")),
			     [this] { ChangeSceneIndex(true, 1, sceneList->count() - 1); });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveToTop")),
			     [this] { ChangeSceneIndex(false, 0, 0); });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveToBottom")),
			     [this] { ChangeSceneIndex(false, 1, sceneList->count() - 1); });

	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Screenshot.Scene")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		auto s = obs_get_source_by_name(item->text().toUtf8().constData());
		if (s) {
			obs_frontend_take_source_screenshot(s);
			obs_source_release(s);
		}
	});
	menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Filters")), [this] {
		auto item = sceneList->currentItem();
		if (!item)
			return;
		auto s = obs_get_source_by_name(item->text().toUtf8().constData());
		if (s) {
			obs_frontend_open_source_filters(s);
			obs_source_release(s);
		}
	});

	auto tom = menu.addMenu(QString::fromUtf8(obs_frontend_get_locale_string("TransitionOverride")));
	std::string scene_name = widget_item->text().toUtf8().constData();
	OBSSourceAutoRelease scene_source = obs_get_source_by_name(scene_name.c_str());
	OBSDataAutoRelease private_settings = obs_source_get_private_settings(scene_source);
	obs_data_set_default_int(private_settings, "transition_duration", 300);
	const char *curTransition = obs_data_get_string(private_settings, "transition");
	int curDuration = (int)obs_data_get_int(private_settings, "transition_duration");

	QSpinBox *duration = new QSpinBox(tom);
	duration->setMinimum(50);
	duration->setSuffix(" ms");
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(curDuration);

	connect(duration, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged, [scene_name](int dur) {
		OBSSourceAutoRelease source = obs_get_source_by_name(scene_name.c_str());
		OBSDataAutoRelease ps = obs_source_get_private_settings(source);

		obs_data_set_int(ps, "transition_duration", dur);
	});

	auto action = tom->addAction(QString::fromUtf8(obs_frontend_get_locale_string("None")));
	action->setCheckable(true);
	action->setChecked(!curTransition || !strlen(curTransition));
	connect(action, &QAction::triggered, [scene_name] {
		OBSSourceAutoRelease source = obs_get_source_by_name(scene_name.c_str());
		OBSDataAutoRelease ps = obs_source_get_private_settings(source);
		obs_data_set_string(ps, "transition", "");
	});

	for (auto t : canvasDock->transitions) {
		const char *name = obs_source_get_name(t);
		bool match = (name && curTransition && strcmp(name, curTransition) == 0);

		if (!name || !*name)
			name = obs_frontend_get_locale_string("None");

		auto a2 = tom->addAction(QString::fromUtf8(name));
		a2->setCheckable(true);
		a2->setChecked(match);
		connect(a, &QAction::triggered, [scene_name, a2] {
			OBSSourceAutoRelease source = obs_get_source_by_name(scene_name.c_str());
			OBSDataAutoRelease ps = obs_source_get_private_settings(source);
			obs_data_set_string(ps, "transition", a2->text().toUtf8().constData());
		});
	}

	QWidgetAction *durationAction = new QWidgetAction(tom);
	durationAction->setDefaultWidget(duration);

	tom->addSeparator();
	tom->addAction(durationAction);

	auto linkedScenesMenu = menu.addMenu(QString::fromUtf8(obs_module_text("LinkedScenes")));
	connect(linkedScenesMenu, &QMenu::aboutToShow, [linkedScenesMenu, this] {
		linkedScenesMenu->clear();
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *src = scenes.sources.array[i];
			obs_data_t *settings = obs_source_get_settings(src);
			if (!obs_data_get_bool(settings, "custom_size")) {
				auto name = QString::fromUtf8(obs_source_get_name(src));
				auto *checkBox = new QCheckBox(name, linkedScenesMenu);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
				connect(checkBox, &QCheckBox::checkStateChanged, [this, src, checkBox] {
#else
				connect(checkBox, &QCheckBox::stateChanged, [this, src, checkBox] {
#endif
					canvasDock->SetLinkedScene(src,
								   checkBox->isChecked() ? sceneList->currentItem()->text() : "");
				});
				auto *checkableAction = new QWidgetAction(linkedScenesMenu);
				checkableAction->setDefaultWidget(checkBox);
				linkedScenesMenu->addAction(checkableAction);

				auto c = obs_data_get_array(settings, "canvas");
				if (c) {
					const auto count = obs_data_array_count(c);

					for (size_t j = 0; j < count; j++) {
						auto item = obs_data_array_item(c, j);
						if (!item)
							continue;
						if (obs_data_get_int(item, "width") == canvasDock->canvas_width &&
						    obs_data_get_int(item, "height") == canvasDock->canvas_height) {
							auto sn = QString::fromUtf8(obs_data_get_string(item, "scene"));
							if (sn == sceneList->currentItem()->text()) {
								checkBox->setChecked(true);
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
		menu.addAction(QString::fromUtf8(obs_module_text("OnMainCanvas")), [this] {
			auto item = sceneList->currentItem();
			if (!item)
				return;
			auto s = obs_get_source_by_name(item->text().toUtf8().constData());
			if (!s)
				return;

			if (obs_frontend_preview_program_mode_active())
				obs_frontend_set_current_preview_scene(s);
			else
				obs_frontend_set_current_scene(s);
			obs_source_release(s);
		});
	}
	a = menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("ShowInMultiview")), [scene_name](bool checked) {
		OBSSourceAutoRelease source = obs_get_source_by_name(scene_name.c_str());
		OBSDataAutoRelease ps = obs_source_get_private_settings(source);
		obs_data_set_bool(ps, "show_in_multiview", checked);
	});
	a->setCheckable(true);
	obs_data_set_default_bool(private_settings, "show_in_multiview", true);
	a->setChecked(obs_data_get_bool(private_settings, "show_in_multiview"));
	menu.exec(QCursor::pos());
}

CanvasScenesDock::CanvasScenesDock(CanvasDock *canvas_dock, QWidget *parent) : QFrame(parent), canvasDock(canvas_dock)
{
	setMinimumWidth(100);
	setMinimumHeight(50);

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	sceneList = new QListWidget();
	sceneList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	sceneList->setFrameShape(QFrame::NoFrame);
	sceneList->setFrameShadow(QFrame::Plain);
	sceneList->setSelectionMode(QAbstractItemView::SingleSelection);
	sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sceneList, &QListWidget::customContextMenuRequested,
		[this](const QPoint &pos) { ShowScenesContextMenu(sceneList->itemAt(pos)); });

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

	QAction *renameAction = new QAction(sceneList);
#ifdef __APPLE__
	renameAction->setShortcut({Qt::Key_Return});
#else
	renameAction->setShortcut({Qt::Key_F2});
#endif
	renameAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(renameAction, &QAction::triggered, [this]() {
		const auto item = sceneList->currentItem();
		if (!item)
			return;
		obs_source_t *source = obs_get_source_by_name(item->text().toUtf8().constData());
		if (!source)
			return;
		std::string name = obs_source_get_name(source);
		obs_source_t *s = nullptr;
		do {
			obs_source_release(s);
			if (!NameDialog::AskForName(this, QString::fromUtf8(obs_module_text("SceneName")), name)) {
				break;
			}
			s = obs_get_source_by_name(name.c_str());
			if (s)
				continue;
			obs_source_set_name(source, name.c_str());
		} while (s);
		obs_source_release(source);
	});
	sceneList->addAction(renameAction);

	mainLayout->addWidget(sceneList, 1);

	auto toolbar = new QToolBar();
	toolbar->setObjectName(QStringLiteral("scenesToolbar"));
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setFloatable(false);
	auto a = toolbar->addAction(QIcon(QString::fromUtf8(":/res/images/plus.svg")),
				    QString::fromUtf8(obs_frontend_get_locale_string("Add")), [this] { canvasDock->AddScene(); });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("addIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-plus");

	a = toolbar->addAction(QIcon(":/res/images/minus.svg"), QString::fromUtf8(obs_frontend_get_locale_string("RemoveScene")),
			       [this] {
				       auto item = sceneList->currentItem();
				       if (!item)
					       return;
				       canvasDock->RemoveScene(item->text());
			       });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("removeIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-minus");
	a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
#ifdef __APPLE__
	a->setShortcut({Qt::Key_Backspace});
#else
	a->setShortcut({Qt::Key_Delete});
#endif
	sceneList->addAction(a);
	toolbar->addSeparator();
	a = toolbar->addAction(QIcon(":/res/images/filter.svg"), QString::fromUtf8(obs_frontend_get_locale_string("SceneFilters")),
			       [this] {
				       auto item = sceneList->currentItem();
				       if (!item)
					       return;
				       auto s = obs_get_source_by_name(item->text().toUtf8().constData());
				       if (!s)
					       return;
				       obs_frontend_open_source_filters(s);
				       obs_source_release(s);
			       });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("filtersIcon")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-filter");
	toolbar->addSeparator();
	a = toolbar->addAction(QIcon(":/res/images/up.svg"), QString::fromUtf8(obs_frontend_get_locale_string("MoveSceneUp")),
			       [this] { ChangeSceneIndex(true, -1, 0); });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("upArrowIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-up");
	a = toolbar->addAction(QIcon(":/res/images/down.svg"), QString::fromUtf8(obs_frontend_get_locale_string("MoveSceneDown")),
			       [this] { ChangeSceneIndex(true, 1, sceneList->count() - 1); });
	toolbar->widgetForAction(a)->setProperty("themeID", QVariant(QString::fromUtf8("downArrowIconSmall")));
	toolbar->widgetForAction(a)->setProperty("class", "icon-down");
	mainLayout->addWidget(toolbar, 0);

	setObjectName(QStringLiteral("contextContainer"));
	setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	setLayout(mainLayout);
}

void CanvasScenesDock::ChangeSceneIndex(bool relative, int offset, int invalidIdx)
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
