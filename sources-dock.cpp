

#include "sources-dock.hpp"

#include <QMenu>
#include <QMessageBox>
#include <QToolBar>
#include <QVBoxLayout>

#include "obs-module.h"
#include "vertical-canvas.hpp"



CanvasSourcesDock::CanvasSourcesDock(CanvasDock *canvas_dock, QWidget *parent)
	: QDockWidget(parent), canvasDock(canvas_dock)
{
	const auto sourcesName = canvasDock->objectName() + "Sources";
	setObjectName(sourcesName);
	const auto sourcesTitle = canvasDock->windowTitle() + " " +
				  QString::fromUtf8(obs_module_text("Sources"));
	setWindowTitle(sourcesTitle);

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	sourceList = new SourceTree(canvas_dock, this);
	sourceList->setSizePolicy(QSizePolicy::Preferred,
				  QSizePolicy::Expanding);
	sourceList->setFrameShape(QFrame::NoFrame);
	sourceList->setFrameShadow(QFrame::Plain);
	sourceList->setSelectionMode(QAbstractItemView::SingleSelection);
	sourceList->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(sourceList, &SourceTree::customContextMenuRequested,
		[this] {
			ShowSourcesContextMenu(GetCurrentSceneItem());
		});

	mainLayout->addWidget(sourceList, 1);

	auto toolbar = new QToolBar();
	toolbar->setObjectName(QStringLiteral("scenesToolbar"));
	toolbar->setIconSize(QSize(16, 16));
	toolbar->setFloatable(false);
	auto a = toolbar->addAction(
		QIcon(QString::fromUtf8(":/res/images/plus.svg")),
		QString::fromUtf8(obs_module_text("Add")), [this] {
			const auto menu =
				canvasDock->CreateAddSourcePopupMenu();
			menu->exec(QCursor::pos());
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("addIconSmall")));

	a = toolbar->addAction(
		QIcon(":/res/images/minus.svg"),
		QString::fromUtf8(obs_module_text("Remove")), [this] {
			std::vector<OBSSceneItem> items;
			obs_scene_enum_items(canvasDock->scene, remove_items,
					     &items);
			if (!items.size())
				return;
			/* ------------------------------------- */
			/* confirm action with user              */

			bool confirmed = false;

			if (items.size() > 1) {
				QString text =
					QString::fromUtf8(
						obs_module_text(
							"ConfirmRemove.TextMultiple"))
						.arg(QString::number(
							items.size()));

				QMessageBox remove_items(this);
				remove_items.setText(text);
				QPushButton *Yes = remove_items.addButton(
					QString::fromUtf8(
						obs_module_text("Yes")),
					QMessageBox::YesRole);
				remove_items.setDefaultButton(Yes);
				remove_items.addButton(
					QString::fromUtf8(
						obs_module_text("No")),
					QMessageBox::NoRole);
				remove_items.setIcon(QMessageBox::Question);
				remove_items.setWindowTitle(
					QString::fromUtf8(obs_module_text(
						"ConfirmRemove.Title")));
				remove_items.exec();

				confirmed = Yes == remove_items.clickedButton();
			} else {
				OBSSceneItem &item = items[0];
				obs_source_t *source =
					obs_sceneitem_get_source(item);
				if (source) {
					const char *name =
						obs_source_get_name(source);

					QString text =
						QString::fromUtf8(
							obs_module_text(
								"ConfirmRemove.Text"))
							.arg(QString::fromUtf8(
								name));

					QMessageBox remove_source(this);
					remove_source.setText(text);
					QPushButton *Yes =
						remove_source.addButton(
							QString::fromUtf8(
								obs_module_text(
									"Yes")),
							QMessageBox::YesRole);
					remove_source.setDefaultButton(Yes);
					remove_source.addButton(
						QString::fromUtf8(
							obs_module_text("No")),
						QMessageBox::NoRole);
					remove_source.setIcon(
						QMessageBox::Question);
					remove_source.setWindowTitle(
						QString::fromUtf8(obs_module_text(
							"ConfirmRemove.Title")));
					remove_source.exec();
					confirmed =
						Yes ==
						remove_source.clickedButton();
				}
			}
			if (!confirmed)
				return;

			/* ----------------------------------------------- */
			/* remove items                                    */

			for (auto &item : items)
				obs_sceneitem_remove(item);
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("removeIconSmall")));
	toolbar->addSeparator();
	a = toolbar->addAction(
		QIcon(":/res/images/filter.svg"),
		QString::fromUtf8(obs_module_text("Filters")), [this] {
			auto item = GetCurrentSceneItem();
			auto source = obs_sceneitem_get_source(item);
			if (source)
				obs_frontend_open_source_filters(source);
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("filtersIcon")));

	a = toolbar->addAction(
		QIcon(":/settings/images/settings/general.svg"),
		QString::fromUtf8(obs_module_text("Properties")), [this] {
			auto item = GetCurrentSceneItem();
			auto source = obs_sceneitem_get_source(item);
			if (source)
				obs_frontend_open_source_properties(source);
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("propertiesIconSmall")));

	toolbar->addSeparator();
	a = toolbar->addAction(
		QIcon(":/res/images/up.svg"),
		QString::fromUtf8(obs_module_text("Up")), [this] {
			auto item = GetCurrentSceneItem();
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);
		});
	toolbar->widgetForAction(a)->setProperty(
		"themeID", QVariant(QString::fromUtf8("upArrowIconSmall")));
	a = toolbar->addAction(
		QIcon(":/res/images/down.svg"),
		QString::fromUtf8(obs_module_text("Down")), [this] {
			auto item = GetCurrentSceneItem();
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);
		});
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

CanvasSourcesDock::~CanvasSourcesDock() {}

void CanvasSourcesDock::ShowSourcesContextMenu(obs_sceneitem_t * item)
{
	auto menu = new QMenu(this);

	menu->addMenu(canvasDock->CreateAddSourcePopupMenu());
	if(item) {
		canvasDock->AddSceneItemMenuItems(menu, item);
	}
	menu->exec(QCursor::pos());
}

bool CanvasSourcesDock::remove_items(obs_scene_t *, obs_sceneitem_t *item,
				     void *param)
{
	std::vector<OBSSceneItem> &items =
		*reinterpret_cast<std::vector<OBSSceneItem> *>(param);

	if (obs_sceneitem_selected(item)) {
		items.emplace_back(item);
	} else if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, remove_items, &items);
	}
	return true;
}

obs_sceneitem_t *CanvasSourcesDock::GetCurrentSceneItem()
{
	return sourceList->Get(GetTopSelectedSourceItem());
}

int CanvasSourcesDock::GetTopSelectedSourceItem()
{
	QModelIndexList selectedItems =
		sourceList->selectionModel()->selectedIndexes();
	return selectedItems.count() ? selectedItems[0].row() : -1;
}
