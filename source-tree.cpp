#include "source-tree.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <string>

#include <QLabel>
#include <QLineEdit>
#include <QSpacerItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QAccessible>
#include <QMessageBox>

#include <QStylePainter>
#include <QStyleOptionFocusRect>

#include "obs-module.h"
#include "vertical-canvas.hpp"

/* ========================================================================= */

static QIcon GetSceneIcon()
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("sceneIcon").value<QIcon>();
}

static QIcon GetGroupIcon()
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("groupIcon").value<QIcon>();
}

static QIcon GetIconFromType(enum obs_icon_type icon_type)
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	switch (icon_type) {
	case OBS_ICON_TYPE_IMAGE:
		return main_window->property("imageIcon").value<QIcon>();
	case OBS_ICON_TYPE_COLOR:
		return main_window->property("colorIcon").value<QIcon>();
	case OBS_ICON_TYPE_SLIDESHOW:
		return main_window->property("slideshowIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_INPUT:
		return main_window->property("audioInputIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		return main_window->property("audioOutputIcon").value<QIcon>();
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		return main_window->property("desktopCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		return main_window->property("windowCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_GAME_CAPTURE:
		return main_window->property("gameCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_CAMERA:
		return main_window->property("cameraIcon").value<QIcon>();
	case OBS_ICON_TYPE_TEXT:
		return main_window->property("textIcon").value<QIcon>();
	case OBS_ICON_TYPE_MEDIA:
		return main_window->property("mediaIcon").value<QIcon>();
	case OBS_ICON_TYPE_BROWSER:
		return main_window->property("browserIcon").value<QIcon>();
	case OBS_ICON_TYPE_CUSTOM:
		//TODO: Add ability for sources to define custom icons
		return main_window->property("defaultIcon").value<QIcon>();
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return main_window->property("audioProcessOutputIcon").value<QIcon>();
	default:
		return main_window->property("defaultIcon").value<QIcon>();
	}
}

SourceTreeItem::SourceTreeItem(SourceTree *tree_, OBSSceneItem sceneitem_) : tree(tree_), sceneitem(sceneitem_)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setMouseTracking(true);

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	const char *name = obs_source_get_name(source);

	OBSDataAutoRelease privData = obs_sceneitem_get_private_settings(sceneitem);
	int preset = (int)obs_data_get_int(privData, "color-preset");

	if (preset == 1) {
		const char *color = obs_data_get_string(privData, "color");
		std::string col = "background: ";
		col += color;
		setStyleSheet(col.c_str());
	} else if (preset > 1) {
		setStyleSheet("");
		setProperty("bgColor", preset - 1);
	} else {
		setStyleSheet("background: none");
	}

	//OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
	const char *id = obs_source_get_id(source);

	bool sourceVisible = obs_sceneitem_visible(sceneitem);

	if (tree->iconsVisible) {
		QIcon icon;

		if (strcmp(id, "scene") == 0)
			icon = GetSceneIcon();
		else if (strcmp(id, "group") == 0)
			icon = GetGroupIcon();
		else
			icon = GetIconFromType(obs_source_get_icon_type(id));

		QPixmap pixmap = icon.pixmap(QSize(16, 16));

		iconLabel = new QLabel();
		iconLabel->setPixmap(pixmap);
		iconLabel->setEnabled(sourceVisible);
		iconLabel->setStyleSheet("background: none");
	}

	vis = new VisibilityCheckBox();
	vis->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	vis->setChecked(sourceVisible);
	vis->setStyleSheet("background: none");
	vis->setAccessibleName(QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Sources.Visibility")));
	vis->setAccessibleDescription(
		QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Sources.VisibilityDescription")).arg(name));

	lock = new LockedCheckBox();
	lock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	lock->setChecked(obs_sceneitem_locked(sceneitem));
	lock->setStyleSheet("background: none");
	lock->setAccessibleName(QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Sources.Lock")));
	lock->setAccessibleDescription(
		QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Sources.LockDescription")).arg(name));

	label = new QLabel(QString::fromUtf8(name));
	label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	label->setAttribute(Qt::WA_TranslucentBackground);
	label->setEnabled(sourceVisible);

#ifdef __APPLE__
	vis->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	lock->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif

	boxLayout = new QHBoxLayout();

	boxLayout->setContentsMargins(0, 0, 0, 0);
	if (iconLabel) {
		boxLayout->addWidget(iconLabel);
		boxLayout->addSpacing(2);
	}
	boxLayout->addWidget(label);
	boxLayout->addWidget(vis);
	boxLayout->addWidget(lock);
#ifdef __APPLE__
	/* Hack: Fixes a bug where scrollbars would be above the lock icon */
	boxLayout->addSpacing(16);
#endif

	Update(false);

	setLayout(boxLayout);

	/* --------------------------------------------------------- */

	auto setItemVisible = [this](bool val) {
		const bool blocked = blockSignals(true);
		obs_sceneitem_set_visible(sceneitem, val);
		blockSignals(blocked);
	};

	auto setItemLocked = [this](bool checked) {
		const bool blocked = blockSignals(true);
		obs_sceneitem_set_locked(sceneitem, checked);
		blockSignals(blocked);
	};

	connect(vis, &QAbstractButton::clicked, setItemVisible);
	connect(lock, &QAbstractButton::clicked, setItemLocked);
}

SourceTreeItem::~SourceTreeItem()
{
	DisconnectSignals();
}

void SourceTreeItem::paintEvent(QPaintEvent *event)
{
	QStyleOption opt;
	opt.initFrom(this);
	QPainter p(this);
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

	QWidget::paintEvent(event);
}

void SourceTreeItem::DisconnectSignals()
{
	obs_scene_t *scene = obs_sceneitem_get_scene(sceneitem);
	if (scene) {
		obs_source_t *sceneSource = obs_scene_get_source(scene);
		signal_handler_t *signal = obs_source_get_signal_handler(sceneSource);
		signal_handler_disconnect(signal, "remove", removeItem, this);
		signal_handler_disconnect(signal, "item_remove", removeItem, this);
		signal_handler_disconnect(signal, "item_visible", itemVisible, this);
		signal_handler_disconnect(signal, "item_locked", itemLocked, this);
		signal_handler_disconnect(signal, "item_select", itemSelect, this);
		signal_handler_disconnect(signal, "item_deselect", itemDeselect, this);
	}

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (source) {
		signal_handler_t *signal = obs_source_get_signal_handler(source);
		signal_handler_disconnect(signal, "rename", renamed, this);
		signal_handler_disconnect(signal, "remove", removeSource, this);
		signal_handler_disconnect(signal, "reorder", reorderGroup, this);
	}
}

void SourceTreeItem::Clear()
{
	DisconnectSignals();
	sceneitem = nullptr;
}

void SourceTreeItem::removeItem(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	obs_sceneitem_t *curItem = (obs_sceneitem_t *)calldata_ptr(cd, "item");
	obs_scene_t *curScene = (obs_scene_t *)calldata_ptr(cd, "scene");

	if (curItem == this_->sceneitem) {
		QMetaObject::invokeMethod(this_->tree, "Remove", Qt::QueuedConnection, Q_ARG(OBSSceneItem, curItem),
					  Q_ARG(OBSScene, curScene));
		curItem = nullptr;
	}
	if (!curItem)
		QMetaObject::invokeMethod(this_, "Clear", Qt::QueuedConnection);
}

void SourceTreeItem::itemVisible(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	obs_sceneitem_t *curItem = (obs_sceneitem_t *)calldata_ptr(cd, "item");
	bool visible = calldata_bool(cd, "visible");

	if (curItem == this_->sceneitem)
		QMetaObject::invokeMethod(this_, "VisibilityChanged", Qt::QueuedConnection, Q_ARG(bool, visible));
}

void SourceTreeItem::itemLocked(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	obs_sceneitem_t *curItem = (obs_sceneitem_t *)calldata_ptr(cd, "item");
	bool locked = calldata_bool(cd, "locked");

	if (curItem == this_->sceneitem)
		QMetaObject::invokeMethod(this_, "LockedChanged", Qt::QueuedConnection, Q_ARG(bool, locked));
}

void SourceTreeItem::itemSelect(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	obs_sceneitem_t *curItem = (obs_sceneitem_t *)calldata_ptr(cd, "item");

	if (curItem == this_->sceneitem)
		QMetaObject::invokeMethod(this_, "Select", Qt::QueuedConnection);
}

void SourceTreeItem::itemDeselect(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	obs_sceneitem_t *curItem = (obs_sceneitem_t *)calldata_ptr(cd, "item");

	if (curItem == this_->sceneitem)
		QMetaObject::invokeMethod(this_, "Deselect", Qt::QueuedConnection);
}

void SourceTreeItem::reorderGroup(void *data, calldata_t *)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	QMetaObject::invokeMethod(this_->tree, "ReorderItems", Qt::QueuedConnection);
};

void SourceTreeItem::renamed(void *data, calldata_t *cd)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	const char *name = calldata_string(cd, "new_name");

	QMetaObject::invokeMethod(this_, "Renamed", Qt::QueuedConnection, Q_ARG(QString, QString::fromUtf8(name)));
}

void SourceTreeItem::removeSource(void *data, calldata_t *)
{
	SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem *>(data);
	this_->DisconnectSignals();
	this_->sceneitem = nullptr;
	QMetaObject::invokeMethod(this_->tree, "RefreshItems", Qt::QueuedConnection);
}

void SourceTreeItem::ReconnectSignals()
{
	if (!sceneitem)
		return;

	DisconnectSignals();

	obs_scene_t *scene = obs_sceneitem_get_scene(sceneitem);
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	signal_handler_t *signal = obs_source_get_signal_handler(sceneSource);
	signal_handler_connect(signal, "remove", removeItem, this);
	signal_handler_connect(signal, "item_remove", removeItem, this);
	signal_handler_connect(signal, "item_visible", itemVisible, this);
	signal_handler_connect(signal, "item_locked", itemLocked, this);
	signal_handler_connect(signal, "item_select", itemSelect, this);
	signal_handler_connect(signal, "item_deselect", itemDeselect, this);

	if (obs_sceneitem_is_group(sceneitem)) {
		obs_source_t *source = obs_sceneitem_get_source(sceneitem);
		signal = obs_source_get_signal_handler(source);

		signal_handler_connect(signal, "reorder", reorderGroup, this);
	}

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	signal = obs_source_get_signal_handler(source);
	signal_handler_connect(signal, "rename", renamed, this);
	signal_handler_connect(signal, "remove", removeSource, this);
}

void SourceTreeItem::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);

	if (expand) {
		expand->setChecked(!expand->isChecked());
	} else {
		obs_source_t *source = obs_sceneitem_get_source(sceneitem);
		if (source) {
			obs_frontend_open_source_properties(source);
		}
	}
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void SourceTreeItem::enterEvent(QEnterEvent *event)
#else
void SourceTreeItem::enterEvent(QEvent *event)
#endif
{
	QWidget::enterEvent(event);

	//OBSBasicPreview *preview = OBSBasicPreview::Get();

	std::lock_guard<std::mutex> l(tree->canvasDock->selectMutex);
	tree->canvasDock->hoveredPreviewItems.clear();
	tree->canvasDock->hoveredPreviewItems.push_back(sceneitem);
}

void SourceTreeItem::leaveEvent(QEvent *event)
{
	QWidget::leaveEvent(event);

	std::lock_guard<std::mutex> l(tree->canvasDock->selectMutex);
	tree->canvasDock->hoveredPreviewItems.clear();
}

bool SourceTreeItem::IsEditing()
{
	return editor != nullptr;
}

void SourceTreeItem::EnterEditMode()
{
	setFocusPolicy(Qt::StrongFocus);
	int index = boxLayout->indexOf(label);
	boxLayout->removeWidget(label);
	editor = new QLineEdit(label->text());
	editor->setStyleSheet("background: none");
	editor->selectAll();
	editor->installEventFilter(this);
	boxLayout->insertWidget(index, editor);
	setFocusProxy(editor);
}

void SourceTreeItem::ExitEditMode(bool save)
{
	ExitEditModeInternal(save);
}

void SourceTreeItem::ExitEditModeInternal(bool save)
{
	if (!editor) {
		return;
	}

	newName = editor->text().toUtf8().constData();

	setFocusProxy(nullptr);
	int index = boxLayout->indexOf(editor);
	boxLayout->removeWidget(editor);
	delete editor;
	editor = nullptr;
	setFocusPolicy(Qt::NoFocus);
	boxLayout->insertWidget(index, label);
	label->setFocus();

	/* ----------------------------------------- */
	/* check for empty string                    */

	if (!save)
		return;

	if (newName.empty()) {
		QMessageBox::information(tree, QString::fromUtf8(obs_frontend_get_locale_string("NoNameEntered.Title")),
					 QString::fromUtf8(obs_frontend_get_locale_string("NoNameEntered.Text")));
		return;
	}

	/* ----------------------------------------- */
	/* Check for same name                       */

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (newName == obs_source_get_name(source))
		return;

	/* ----------------------------------------- */
	/* check for existing source                 */
	obs_canvas_t *canvas = (obs_source_get_output_flags(source) & OBS_SOURCE_REQUIRES_CANVAS) ? obs_source_get_canvas(source)
												  : nullptr;
	OBSSourceAutoRelease existingSource = canvas ? obs_canvas_get_source_by_name(canvas, newName.c_str())
						     : obs_get_source_by_name(newName.c_str());
	bool exists = !!existingSource;

	if (exists) {
		QMessageBox::information(tree, QString::fromUtf8(obs_frontend_get_locale_string("NameExists.Title")),
					 QString::fromUtf8(obs_frontend_get_locale_string("NameExists.Text")));
		return;
	}

	/* ----------------------------------------- */
	/* rename                                    */

	const bool blocked = blockSignals(true);
	obs_source_set_name(source, newName.c_str());
	label->setText(QString::fromUtf8(newName.c_str()));
	blockSignals(blocked);
}

bool LineEditCanceled(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = reinterpret_cast<QKeyEvent *>(event);
		return keyEvent->key() == Qt::Key_Escape;
	}

	return false;
}

bool LineEditChanged(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = reinterpret_cast<QKeyEvent *>(event);

		switch (keyEvent->key()) {
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
		case Qt::Key_Enter:
		case Qt::Key_Return:
			return true;
		}
	} else if (event->type() == QEvent::FocusOut) {
		return true;
	}

	return false;
}

bool SourceTreeItem::eventFilter(QObject *object, QEvent *event)
{
	if (editor != object)
		return false;

	if (LineEditCanceled(event)) {
		QMetaObject::invokeMethod(this, "ExitEditMode", Qt::QueuedConnection, Q_ARG(bool, false));
		return true;
	}
	if (LineEditChanged(event)) {
		QMetaObject::invokeMethod(this, "ExitEditMode", Qt::QueuedConnection, Q_ARG(bool, true));
		return true;
	}

	return false;
}

void SourceTreeItem::VisibilityChanged(bool visible)
{
	if (iconLabel) {
		iconLabel->setEnabled(visible);
	}
	label->setEnabled(visible);
	vis->setChecked(visible);
}

void SourceTreeItem::LockedChanged(bool locked)
{
	lock->setChecked(locked);
	//OBSBasic::Get()->UpdateEditMenu();
}

void SourceTreeItem::Renamed(const QString &name)
{
	label->setText(name);
}

extern std::list<CanvasDock *> canvas_docks;

void SourceTreeItem::Update(bool force)
{
	if (std::find(canvas_docks.begin(), canvas_docks.end(), tree->canvasDock) == canvas_docks.end())
		return;
	obs_scene_t *scene = tree->canvasDock->scene;
	obs_scene_t *itemScene = obs_sceneitem_get_scene(sceneitem);

	Type newType;

	/* ------------------------------------------------- */
	/* if it's a group item, insert group checkbox       */

	if (obs_sceneitem_is_group(sceneitem)) {
		newType = Type::Group;

		/* ------------------------------------------------- */
		/* if it's a group sub-item                          */

	} else if (itemScene != scene) {
		newType = Type::SubItem;

		/* ------------------------------------------------- */
		/* if it's a regular item                            */

	} else {
		newType = Type::Item;
	}

	/* ------------------------------------------------- */

	if (!force && newType == type) {
		return;
	}

	/* ------------------------------------------------- */

	ReconnectSignals();

	if (spacer) {
		boxLayout->removeItem(spacer);
		delete spacer;
		spacer = nullptr;
	}

	if (type == Type::Group) {
		boxLayout->removeWidget(expand);
		expand->deleteLater();
		expand = nullptr;
	}

	type = newType;

	if (type == Type::SubItem) {
		spacer = new QSpacerItem(16, 1);
		boxLayout->insertItem(0, spacer);

	} else if (type == Type::Group) {
		expand = new SourceTreeSubItemCheckBox();
		expand->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
		expand->setMaximumSize(10, 16);
		expand->setMinimumSize(10, 0);
#ifdef __APPLE__
		expand->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
		boxLayout->insertWidget(0, expand);

		OBSDataAutoRelease private_settings = obs_sceneitem_get_private_settings(sceneitem);
		expand->blockSignals(true);
		expand->setChecked(obs_data_get_bool(private_settings, "collapsed"));
		expand->blockSignals(false);

		connect(expand, &QPushButton::toggled, this, &SourceTreeItem::ExpandClicked);

	} else {
		spacer = new QSpacerItem(3, 1);
		boxLayout->insertItem(0, spacer);
	}
}

void SourceTreeItem::ExpandClicked(bool checked)
{
	OBSDataAutoRelease private_settings = obs_sceneitem_get_private_settings(sceneitem);

	obs_data_set_bool(private_settings, "collapsed", checked);

	if (!checked)
		tree->GetStm()->ExpandGroup(sceneitem);
	else
		tree->GetStm()->CollapseGroup(sceneitem);
}

void SourceTreeItem::Select()
{
	tree->SelectItem(sceneitem, true);
}

void SourceTreeItem::Deselect()
{
	tree->SelectItem(sceneitem, false);
}

/* ========================================================================= */

void SourceTreeModel::OBSFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	SourceTreeModel *stm = reinterpret_cast<SourceTreeModel *>(ptr);

	switch (event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		//stm->SceneChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		if (!stm->items.isEmpty())
			stm->items.clear();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		stm->Clear();
		break;
	default:
		break;
	}
}

void SourceTreeModel::Clear()
{
	if (items.isEmpty())
		return;
	beginResetModel();
	items.clear();
	endResetModel();

	hasGroups = false;
}

static bool enumItem(obs_scene_t *, obs_sceneitem_t *item, void *ptr)
{
	QVector<OBSSceneItem> &items = *reinterpret_cast<QVector<OBSSceneItem> *>(ptr);

	obs_source_t *src = obs_sceneitem_get_source(item);
	if (obs_source_removed(src)) {
		return true;
	}

	if (obs_sceneitem_is_group(item)) {
		OBSDataAutoRelease data = obs_sceneitem_get_private_settings(item);

		bool collapse = obs_data_get_bool(data, "collapsed");
		if (!collapse) {
			obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

			obs_scene_enum_items(scene, enumItem, &items);
		}
	}

	items.insert(0, item);
	return true;
}

void SourceTreeModel::SceneChanged()
{
	if (std::find(canvas_docks.begin(), canvas_docks.end(), st->canvasDock) == canvas_docks.end())
		return;
	obs_scene_t *scene = st->canvasDock->scene;

	beginResetModel();
	items.clear();
	obs_scene_enum_items(scene, enumItem, &items);
	endResetModel();

	UpdateGroupState(false);
	st->ResetWidgets();

	for (int i = 0; i < items.count(); i++) {
		bool select = obs_sceneitem_selected(items[i]);
		QModelIndex index = createIndex(i, 0);

		st->selectionModel()->select(index, select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
	}
}

/* moves a scene item index (blame linux distros for using older Qt builds) */
static inline void MoveItem(QVector<OBSSceneItem> &items, int oldIdx, int newIdx)
{
	OBSSceneItem item = items[oldIdx];
	items.remove(oldIdx);
	items.insert(newIdx, item);
}

/* reorders list optimally with model reorder funcs */
void SourceTreeModel::ReorderItems()
{
	obs_scene_t *scene = st->canvasDock->scene;

	QVector<OBSSceneItem> newitems;
	obs_scene_enum_items(scene, enumItem, &newitems);

	/* if item list has changed size, do full reset */
	if (newitems.count() != items.count()) {
		SceneChanged();
		return;
	}

	for (;;) {
		int idx1Old = 0;
		int idx1New = 0;
		int count;
		int i;

		/* find first starting changed item index */
		for (i = 0; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[i];
			obs_sceneitem_t *newItem = newitems[i];
			if (oldItem != newItem) {
				idx1Old = i;
				break;
			}
		}

		/* if everything is the same, break */
		if (i == newitems.count()) {
			break;
		}

		/* find new starting index */
		for (i = idx1Old + 1; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[idx1Old];
			obs_sceneitem_t *newItem = newitems[i];

			if (oldItem == newItem) {
				idx1New = i;
				break;
			}
		}

		/* if item could not be found, do full reset */
		if (i == newitems.count()) {
			SceneChanged();
			return;
		}

		/* get move count */
		for (count = 1; (idx1New + count) < newitems.count(); count++) {
			int oldIdx = idx1Old + count;
			int newIdx = idx1New + count;

			obs_sceneitem_t *oldItem = items[oldIdx];
			obs_sceneitem_t *newItem = newitems[newIdx];

			if (oldItem != newItem) {
				break;
			}
		}

		/* move items */
		beginMoveRows(QModelIndex(), idx1Old, idx1Old + count - 1, QModelIndex(), idx1New + count);
		for (i = 0; i < count; i++) {
			int to = idx1New + count;
			if (to > idx1Old)
				to--;
			MoveItem(items, idx1Old, to);
		}
		endMoveRows();
	}
}

void SourceTreeModel::Add(obs_sceneitem_t *item)
{
	if (obs_sceneitem_is_group(item)) {
		SceneChanged();
	} else {
		beginInsertRows(QModelIndex(), 0, 0);
		items.insert(0, item);
		endInsertRows();

		st->UpdateWidget(createIndex(0, 0, nullptr), item);
	}
}

void SourceTreeModel::Remove(obs_sceneitem_t *item)
{
	if (std::find(canvas_docks.begin(), canvas_docks.end(), st->canvasDock) == canvas_docks.end())
		return;
	int idx = -1;
	for (int i = 0; i < items.count(); i++) {
		if (items[i] == item) {
			idx = i;
			break;
		}
	}

	if (idx == -1)
		return;

	int startIdx = idx;
	int endIdx = idx;

	bool is_group = obs_sceneitem_is_group(item);
	if (is_group) {
		obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

		for (int i = endIdx + 1; i < items.count(); i++) {
			obs_sceneitem_t *subitem = items[i];
			obs_scene_t *subscene = obs_sceneitem_get_scene(subitem);

			if (subscene == scene)
				endIdx = i;
			else
				break;
		}
	}

	beginRemoveRows(QModelIndex(), startIdx, endIdx);
	items.remove(idx, endIdx - startIdx + 1);
	endRemoveRows();

	if (is_group)
		UpdateGroupState(true);
}

OBSSceneItem SourceTreeModel::Get(int idx)
{
	if (idx == -1 || idx >= items.count())
		return OBSSceneItem();
	return items[idx];
}

SourceTreeModel::SourceTreeModel(SourceTree *st_) : QAbstractListModel(st_), st(st_)
{
	obs_frontend_add_event_callback(OBSFrontendEvent, this);
}

SourceTreeModel::~SourceTreeModel()
{
	obs_frontend_remove_event_callback(OBSFrontendEvent, this);
}

int SourceTreeModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : (int)items.count();
}

QVariant SourceTreeModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::AccessibleTextRole) {
		OBSSceneItem item = items[index.row()];
		obs_source_t *source = obs_sceneitem_get_source(item);
		return QVariant(QString::fromUtf8(obs_source_get_name(source)));
	}

	return QVariant();
}

Qt::ItemFlags SourceTreeModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;

	obs_sceneitem_t *item = items[index.row()];
	bool is_group = obs_sceneitem_is_group(item);

	return QAbstractListModel::flags(index) | Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
	       (is_group ? Qt::ItemIsDropEnabled : Qt::NoItemFlags);
}

Qt::DropActions SourceTreeModel::supportedDropActions() const
{
	return QAbstractItemModel::supportedDropActions() | Qt::MoveAction;
}

QString SourceTreeModel::GetNewGroupName()
{
	QString name = QString::fromUtf8(obs_frontend_get_locale_string("Group"));
	int i = 2;
	for (;;) {
		OBSSourceAutoRelease group = obs_canvas_get_source_by_name(st->canvasDock->canvas, name.toUtf8().constData());
		if (!group)
			break;
		name = QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Group")).arg(QString::number(i++));
	}

	return name;
}

void SourceTreeModel::AddGroup()
{
	QString name = GetNewGroupName();
	obs_sceneitem_t *group = obs_scene_add_group(st->canvasDock->scene, name.toUtf8().constData());
	if (!group)
		return;

	beginInsertRows(QModelIndex(), 0, 0);
	items.insert(0, group);
	endInsertRows();

	st->UpdateWidget(createIndex(0, 0, nullptr), group);
	UpdateGroupState(true);

	QMetaObject::invokeMethod(st, "Edit", Qt::QueuedConnection, Q_ARG(int, 0));
}

void SourceTreeModel::GroupSelectedItems(QModelIndexList &indices)
{
	if (indices.count() == 0)
		return;

	obs_scene_t *scene = st->canvasDock->scene;
	QString name = GetNewGroupName();

	QVector<obs_sceneitem_t *> item_order;

	for (auto i = indices.count() - 1; i >= 0; i--) {
		obs_sceneitem_t *si = items[indices[i].row()];
		item_order << si;
	}

	obs_sceneitem_t *item = obs_scene_insert_group(scene, name.toUtf8().constData(), item_order.data(), item_order.size());
	if (!item) {
		return;
	}

	for (obs_sceneitem_t *si : item_order)
		obs_sceneitem_select(si, false);

	hasGroups = true;
	st->UpdateWidgets(true);

	obs_sceneitem_select(item, true);

	/* ----------------------------------------------------------------- */
	/* obs_scene_insert_group triggers a full refresh of scene items via */
	/* the item_add signal. No need to insert a row, just edit the one   */
	/* that's created automatically.                                     */

	int newIdx = indices[0].row();
	QMetaObject::invokeMethod(st, "NewGroupEdit", Qt::QueuedConnection, Q_ARG(int, newIdx));
}

void SourceTreeModel::UngroupSelectedGroups(QModelIndexList &indices)
{
	if (indices.count() == 0)
		return;

	for (auto i = indices.count() - 1; i >= 0; i--) {
		obs_sceneitem_t *item = items[indices[i].row()];
		obs_sceneitem_group_ungroup(item);
	}

	SceneChanged();
}

void SourceTreeModel::ExpandGroup(obs_sceneitem_t *item)
{
	auto itemIdx = items.indexOf(item);
	if (itemIdx == -1)
		return;

	itemIdx++;

	obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

	QVector<OBSSceneItem> subItems;
	obs_scene_enum_items(scene, enumItem, &subItems);

	if (!subItems.size())
		return;

	beginInsertRows(QModelIndex(), (int)itemIdx, (int)itemIdx + (int)subItems.size() - 1);
	for (qsizetype i = 0; i < subItems.size(); i++)
		items.insert(i + itemIdx, subItems[i]);
	endInsertRows();

	st->UpdateWidgets();
}

void SourceTreeModel::CollapseGroup(obs_sceneitem_t *item)
{
	int startIdx = -1;
	int endIdx = -1;

	obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

	for (int i = 0; i < items.size(); i++) {
		obs_scene_t *itemScene = obs_sceneitem_get_scene(items[i]);

		if (itemScene == scene) {
			if (startIdx == -1)
				startIdx = i;
			endIdx = i;
		}
	}

	if (startIdx == -1)
		return;

	beginRemoveRows(QModelIndex(), startIdx, endIdx);
	items.remove(startIdx, endIdx - startIdx + 1);
	endRemoveRows();
}

void SourceTreeModel::UpdateGroupState(bool update)
{
	bool nowHasGroups = false;
	for (auto &item : items) {
		if (obs_sceneitem_is_group(item)) {
			nowHasGroups = true;
			break;
		}
	}

	if (nowHasGroups != hasGroups) {
		hasGroups = nowHasGroups;
		if (update) {
			st->UpdateWidgets(true);
		}
	}
}

/* ========================================================================= */

SourceTree::SourceTree(CanvasDock *canvas_dock, QWidget *parent_) : QListView(parent_), canvasDock(canvas_dock)
{
	SourceTreeModel *stm_ = new SourceTreeModel(this);
	setModel(stm_);
	setStyleSheet(QString("*[bgColor=\"1\"]{background-color:rgba(255,68,68,33%);}"
			      "*[bgColor=\"2\"]{background-color:rgba(255,255,68,33%);}"
			      "*[bgColor=\"3\"]{background-color:rgba(68,255,68,33%);}"
			      "*[bgColor=\"4\"]{background-color:rgba(68,255,255,33%);}"
			      "*[bgColor=\"5\"]{background-color:rgba(68,68,255,33%);}"
			      "*[bgColor=\"6\"]{background-color:rgba(255,68,255,33%);}"
			      "*[bgColor=\"7\"]{background-color:rgba(68,68,68,33%);}"
			      "*[bgColor=\"8\"]{background-color:rgba(255,255,255,33%);}"));

	UpdateNoSourcesMessage();
	//const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	//connect(App(), &OBSApp::StyleChanged, this, &SourceTree::UpdateNoSourcesMessage);
	//connect(App(), &OBSApp::StyleChanged, this, &SourceTree::UpdateIcons);

	setItemDelegate(new SourceTreeDelegate(this));
}

void SourceTree::UpdateIcons()
{
	SourceTreeModel *stm = GetStm();
	stm->SceneChanged();
}

void SourceTree::SetIconsVisible(bool visible)
{
	SourceTreeModel *stm = GetStm();

	iconsVisible = visible;
	stm->SceneChanged();
}

void SourceTree::ResetWidgets()
{
	SourceTreeModel *stm = GetStm();
	stm->UpdateGroupState(false);

	for (int i = 0; i < stm->items.count(); i++) {
		QModelIndex index = stm->createIndex(i, 0, nullptr);
		setIndexWidget(index, new SourceTreeItem(this, stm->items[i]));
	}
}

void SourceTree::UpdateWidget(const QModelIndex &idx, obs_sceneitem_t *item)
{
	setIndexWidget(idx, new SourceTreeItem(this, item));
}

void SourceTree::UpdateWidgets(bool force)
{
	SourceTreeModel *stm = GetStm();

	for (int i = 0; i < stm->items.size(); i++) {
		obs_sceneitem_t *item = stm->items[i];
		SourceTreeItem *widget = GetItemWidget(i);

		if (!widget) {
			UpdateWidget(stm->createIndex(i, 0), item);
		} else {
			widget->Update(force);
		}
	}
}

void SourceTree::SelectItem(obs_sceneitem_t *sceneitem, bool select)
{
	SourceTreeModel *stm = GetStm();
	int i = 0;

	for (; i < stm->items.count(); i++) {
		if (stm->items[i] == sceneitem)
			break;
	}

	if (i == stm->items.count())
		return;

	QModelIndex index = stm->createIndex(i, 0);
	if (index.isValid() && selectionModel()->isSelected(index) != select)
		selectionModel()->select(index, select ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}

Q_DECLARE_METATYPE(OBSSceneItem);

void SourceTree::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		QListView::mouseDoubleClickEvent(event);
}

void SourceTree::dropEvent(QDropEvent *event)
{
	if (event->source() != this) {
		QListView::dropEvent(event);
		return;
	}

	obs_scene_t *scene = canvasDock->scene;
	SourceTreeModel *stm = GetStm();
	auto &items = stm->items;
	QModelIndexList indices = selectedIndexes();

	DropIndicatorPosition indicator = dropIndicatorPosition();
	int row = indexAt(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
			  event->position().toPoint()
#else
			  event->pos()
#endif
				  )
			  .row();
	bool emptyDrop = row == -1;

	if (emptyDrop) {
		if (!items.size()) {
			QListView::dropEvent(event);
			return;
		}

		row = (int)items.size() - 1;
		indicator = QAbstractItemView::BelowItem;
	}

	/* --------------------------------------- */
	/* store destination group if moving to a  */
	/* group                                   */

	obs_sceneitem_t *dropItem = items[row]; /* item being dropped on */
	bool itemIsGroup = obs_sceneitem_is_group(dropItem);

	obs_sceneitem_t *dropGroup = itemIsGroup ? dropItem : obs_sceneitem_get_group(scene, dropItem);

	/* not a group if moving above the group */
	if (indicator == QAbstractItemView::AboveItem && itemIsGroup)
		dropGroup = nullptr;
	if (emptyDrop)
		dropGroup = nullptr;

	/* --------------------------------------- */
	/* remember to remove list items if        */
	/* dropping on collapsed group             */

	bool dropOnCollapsed = false;
	if (dropGroup) {
		obs_data_t *private_settings = obs_sceneitem_get_private_settings(dropGroup);
		dropOnCollapsed = obs_data_get_bool(private_settings, "collapsed");
		obs_data_release(private_settings);
	}

	if (indicator == QAbstractItemView::BelowItem || indicator == QAbstractItemView::OnItem ||
	    indicator == QAbstractItemView::OnViewport)
		row++;

	if (row < 0 || row > stm->items.count()) {
		QListView::dropEvent(event);
		return;
	}

	/* --------------------------------------- */
	/* determine if any base group is selected */

	bool hasGroups = false;
	for (int i = 0; i < indices.size(); i++) {
		obs_sceneitem_t *item = items[indices[i].row()];
		if (obs_sceneitem_is_group(item)) {
			hasGroups = true;
			break;
		}
	}

	/* --------------------------------------- */
	/* if dropping a group, detect if it's     */
	/* below another group                     */

	obs_sceneitem_t *itemBelow;
	if (row == stm->items.count())
		itemBelow = nullptr;
	else
		itemBelow = stm->items[row];

	if (hasGroups) {
		if (!itemBelow || obs_sceneitem_get_group(scene, itemBelow) != dropGroup) {
			dropGroup = nullptr;
			dropOnCollapsed = false;
		}
	}

	/* --------------------------------------- */
	/* if dropping groups on other groups,     */
	/* disregard as invalid drag/drop          */

	if (dropGroup && hasGroups) {
		QListView::dropEvent(event);
		return;
	}

	/* --------------------------------------- */
	/* save undo data                          */
	std::vector<obs_source_t *> sources;
	for (int i = 0; i < indices.size(); i++) {
		obs_sceneitem_t *item = items[indices[i].row()];
		if (obs_sceneitem_get_scene(item) != scene)
			sources.push_back(obs_scene_get_source(obs_sceneitem_get_scene(item)));
	}
	if (dropGroup)
		sources.push_back(obs_sceneitem_get_source(dropGroup));

	/* --------------------------------------- */
	/* if selection includes base group items, */
	/* include all group sub-items and treat   */
	/* them all as one                         */

	if (hasGroups) {
		/* remove sub-items if selected */
		for (auto i = indices.size() - 1; i >= 0; i--) {
			obs_sceneitem_t *item = items[indices[i].row()];
			obs_scene_t *itemScene = obs_sceneitem_get_scene(item);

			if (itemScene != scene) {
				indices.removeAt(i);
			}
		}

		/* add all sub-items of selected groups */
		for (auto i = indices.size() - 1; i >= 0; i--) {
			obs_sceneitem_t *item = items[indices[i].row()];

			if (obs_sceneitem_is_group(item)) {
				for (int j = (int)items.size() - 1; j >= 0; j--) {
					obs_sceneitem_t *subitem = items[j];
					obs_sceneitem_t *subitemGroup = obs_sceneitem_get_group(scene, subitem);

					if (subitemGroup == item) {
						QModelIndex idx = stm->createIndex(j, 0);
						indices.insert(i + 1, idx);
					}
				}
			}
		}
	}

	/* --------------------------------------- */
	/* build persistent indices                */

	QList<QPersistentModelIndex> persistentIndices;
	persistentIndices.reserve(indices.count());
	for (QModelIndex &index : indices)
		persistentIndices.append(index);
	std::sort(persistentIndices.begin(), persistentIndices.end());

	/* --------------------------------------- */
	/* move all items to destination index     */

	int r = row;
	for (auto &persistentIdx : persistentIndices) {
		int from = persistentIdx.row();
		int to = r;
		int itemTo = to;

		if (itemTo > from)
			itemTo--;

		if (itemTo != from) {
			stm->beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);
			MoveItem(items, from, itemTo);
			stm->endMoveRows();
		}

		r = persistentIdx.row() + 1;
	}

	std::sort(persistentIndices.begin(), persistentIndices.end());
	int firstIdx = persistentIndices.front().row();
	int lastIdx = persistentIndices.back().row();

	/* --------------------------------------- */
	/* reorder scene items in back-end         */

	QVector<struct obs_sceneitem_order_info> orderList;
	obs_sceneitem_t *lastGroup = nullptr;
	int insertCollapsedIdx = 0;

	auto insertCollapsed = [&](obs_sceneitem_t *item) {
		struct obs_sceneitem_order_info info;
		info.group = lastGroup;
		info.item = item;

		orderList.insert(insertCollapsedIdx++, info);
	};

	using insertCollapsed_t = decltype(insertCollapsed);

	auto preInsertCollapsed = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		(*static_cast<insertCollapsed_t *>(param))(item);
		return true;
	};

	auto insertLastGroup = [&]() {
		OBSDataAutoRelease private_settings = obs_sceneitem_get_private_settings(lastGroup);
		bool collapsed = obs_data_get_bool(private_settings, "collapsed");

		if (collapsed) {
			insertCollapsedIdx = 0;
			obs_sceneitem_group_enum_items(lastGroup, preInsertCollapsed, &insertCollapsed);
		}

		struct obs_sceneitem_order_info info;
		info.group = nullptr;
		info.item = lastGroup;
		orderList.insert(0, info);
	};

	auto updateScene = [&]() {
		struct obs_sceneitem_order_info info;

		for (int i = 0; i < items.size(); i++) {
			obs_sceneitem_t *item = items[i];
			obs_sceneitem_t *group;

			if (obs_sceneitem_is_group(item)) {
				if (lastGroup) {
					insertLastGroup();
				}
				lastGroup = item;
				continue;
			}

			if (!hasGroups && i >= firstIdx && i <= lastIdx)
				group = dropGroup;
			else
				group = obs_sceneitem_get_group(scene, item);

			if (lastGroup && lastGroup != group) {
				insertLastGroup();
			}

			lastGroup = group;

			info.group = group;
			info.item = item;
			orderList.insert(0, info);
		}

		if (lastGroup) {
			insertLastGroup();
		}

		obs_scene_reorder_items2(scene, orderList.data(), orderList.size());
	};

	using updateScene_t = decltype(updateScene);

	auto preUpdateScene = [](void *d, obs_scene_t *) {
		(*static_cast<updateScene_t *>(d))();
	};

	ignoreReorder = true;
	obs_scene_atomic_update(scene, preUpdateScene, &updateScene);
	ignoreReorder = false;

	/* --------------------------------------- */
	/* remove items if dropped in to collapsed */
	/* group                                   */

	if (dropOnCollapsed) {
		stm->beginRemoveRows(QModelIndex(), firstIdx, lastIdx);
		items.remove(firstIdx, lastIdx - firstIdx + 1);
		stm->endRemoveRows();
	}

	/* --------------------------------------- */
	/* update widgets and accept event         */

	UpdateWidgets(true);

	event->accept();
	event->setDropAction(Qt::CopyAction);

	QListView::dropEvent(event);
}

void SourceTree::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	{
		const bool blocked = blockSignals(true);
		SourceTreeModel *stm = GetStm();
		{
			QModelIndexList selectedIdxs = selected.indexes();
			for (int i = 0; i < selectedIdxs.count(); i++) {
				int idx = selectedIdxs[i].row();
				obs_sceneitem_select(stm->items[idx], true);
			}
		}
		{
			QModelIndexList deselectedIdxs = deselected.indexes();
			for (int i = 0; i < deselectedIdxs.count(); i++) {
				int idx = deselectedIdxs[i].row();
				obs_sceneitem_select(stm->items[idx], false);
			}
		}
		blockSignals(blocked);
	}
	QListView::selectionChanged(selected, deselected);
}

void SourceTree::NewGroupEdit(int row)
{
	if (!Edit(row)) {

		blog(LOG_WARNING, "Uh, somehow the edit didn't process, this "
				  "code should never be reached.\nAnd by "
				  "\"never be reached\", I mean that "
				  "theoretically, it should be\nimpossible "
				  "for this code to be reached. But if this "
				  "code is reached,\nfeel free to laugh at "
				  "Jim, because apparently it is, in fact, "
				  "actually\npossible for this code to be "
				  "reached. But I mean, again, theoretically\n"
				  "it should be impossible. So if you see "
				  "this in your log, just know that\nit's "
				  "really dumb, and depressing. But at least "
				  "the undo/redo action is\nstill covered, so "
				  "in theory things *should* be fine. But "
				  "it's entirely\npossible that they might "
				  "not be exactly. But again, yea. This "
				  "really\nshould not be possible.");
	}
}

bool SourceTree::Edit(int row)
{
	SourceTreeModel *stm = GetStm();
	if (row < 0 || row >= stm->items.count())
		return false;

	QModelIndex index = stm->createIndex(row, 0);
	QWidget *widget = indexWidget(index);
	SourceTreeItem *itemWidget = reinterpret_cast<SourceTreeItem *>(widget);
	if (itemWidget->IsEditing()) {
#ifdef __APPLE__
		itemWidget->ExitEditMode(true);
#endif
		return false;
	}

	itemWidget->EnterEditMode();
	edit(index);
	return true;
}

bool SourceTree::MultipleBaseSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();

	obs_scene_t *scene = canvasDock->scene;

	if (selectedIndices.size() < 1) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		if (obs_sceneitem_is_group(item)) {
			return false;
		}

		obs_scene *itemScene = obs_sceneitem_get_scene(item);
		if (itemScene != scene) {
			return false;
		}
	}

	return true;
}

bool SourceTree::GroupsSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();

	if (selectedIndices.size() < 1) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		if (!obs_sceneitem_is_group(item)) {
			return false;
		}
	}

	return true;
}

bool SourceTree::GroupedItemsSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();
	obs_scene_t *scene = canvasDock->scene;

	if (!selectedIndices.size()) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		obs_scene *itemScene = obs_sceneitem_get_scene(item);

		if (itemScene != scene) {
			return true;
		}
	}

	return false;
}

void SourceTree::Remove(OBSSceneItem item, OBSScene scene)
{
	if (std::find(canvas_docks.begin(), canvas_docks.end(), canvasDock) == canvas_docks.end())
		return;
	GetStm()->Remove(item);
	obs_frontend_save();

	obs_source_t *sceneSource = obs_scene_get_source(scene);
	obs_source_t *itemSource = obs_sceneitem_get_source(item);
	blog(LOG_INFO, "User Removed source '%s' (%s) from scene '%s'", obs_source_get_name(itemSource),
	     obs_source_get_id(itemSource), sceneSource ? obs_source_get_name(sceneSource) : "");
}

void SourceTree::GroupSelectedItems()
{
	QModelIndexList indices = selectedIndexes();
	std::sort(indices.begin(), indices.end());
	GetStm()->GroupSelectedItems(indices);
}

void SourceTree::UngroupSelectedGroups()
{
	QModelIndexList indices = selectedIndexes();
	GetStm()->UngroupSelectedGroups(indices);
}

void SourceTree::AddGroup()
{
	GetStm()->AddGroup();
}

void SourceTree::UpdateNoSourcesMessage()
{
	std::string darkPath = ":res/images/no_sources.svg";
	//GetDataFilePath("themes/Dark/no_sources.svg", darkPath);

	QString file = !obs_frontend_is_theme_dark() ? ":res/images/no_sources.svg" : darkPath.c_str();
	//iconNoSources.load(file);

	QTextOption opt(Qt::AlignHCenter);
	opt.setWrapMode(QTextOption::WordWrap);
	textNoSources.setTextOption(opt);
	textNoSources.setText(QString::fromUtf8(obs_frontend_get_locale_string("NoSources.Label")).replace("\n", "<br/>"));

	textPrepared = false;
}

void SourceTree::paintEvent(QPaintEvent *event)
{
	SourceTreeModel *stm = GetStm();
	if (stm && !stm->items.count()) {
		QPainter p(viewport());

		if (!textPrepared) {
			textNoSources.prepare(QTransform(), p.font());
			textPrepared = true;
		}

		//QRectF iconRect = iconNoSources.viewBoxF();
		//iconRect.setSize(QSizeF(32.0, 32.0));

		//QSizeF iconSize = iconRect.size();
		QSizeF textSize = textNoSources.size();
		QSizeF thisSize = size();
		//const qreal spacing = 16.0;

		qreal totalHeight =
			/*iconSize.height() + spacing + */ textSize.height();

		//qreal x = thisSize.width() / 2.0 /* - iconSize.width() / 2.0*/;
		qreal y = thisSize.height() / 2.0 - totalHeight / 2.0;
		//iconRect.moveTo(std::round(x), std::round(y));
		//iconNoSources.render(&p, iconRect);

		qreal x = thisSize.width() / 2.0 - textSize.width() / 2.0;
		//y += spacing + iconSize.height();
		p.drawStaticText((int)x, (int)y, textNoSources);
	} else {
		QListView::paintEvent(event);
	}
}

SourceTreeDelegate::SourceTreeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize SourceTreeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	SourceTree *tree = qobject_cast<SourceTree *>(parent());
	QWidget *item = tree->indexWidget(index);

	if (!item)
		return QStyledItemDelegate::sizeHint(option, index);

	return (QSize(item->sizeHint()));
}
