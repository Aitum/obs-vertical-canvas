#include "transitions-dock.hpp"

#include "obs-module.h"
#include "vertical-canvas.hpp"
#include <QMenu>
#include "name-dialog.hpp"
#include <QMessageBox>

CanvasTransitionsDock::CanvasTransitionsDock(CanvasDock *canvas_dock, QWidget *parent) : QFrame(parent), canvasDock(canvas_dock)
{

	setMinimumWidth(100);
	setMinimumHeight(50);
	setContentsMargins(0, 0, 0, 0);

	auto mainLayout = new QVBoxLayout();
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(2);

	transition = new QComboBox();
	mainLayout->addWidget(transition);

	auto hl = new QHBoxLayout();
	/* auto l = new QLabel(QString::fromUtf8(
		obs_frontend_get_locale_string("Basic.TransitionDuration")));
	l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	hl->addWidget(l);
	duration = new QSpinBox();
	duration->setSuffix(" ms");
	duration->setMinimum(50);
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(300);
	l->setBuddy(duration);
	hl->addWidget(duration);
	mainLayout->addLayout(hl);

	hl = new QHBoxLayout();*/
	hl->addStretch();
	auto addButton = new QPushButton();
	addButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	addButton->setAccessibleName(QString::fromUtf8(obs_frontend_get_locale_string("Basic.AddTransition")));
	addButton->setToolTip(QString::fromUtf8(obs_frontend_get_locale_string("Basic.AddTransition")));
	addButton->setIcon(QIcon(":/res/images/add.png"));
	addButton->setProperty("themeID", "addIconSmall");
	addButton->setProperty("class", "icon-plus");
	addButton->setProperty("toolButton", true);
	addButton->setFlat(false);

	connect(addButton, &QPushButton::clicked, [this] {
		auto menu = QMenu(this);
		auto subMenu = menu.addMenu(QString::fromUtf8(obs_module_text("CopyFromMain")));
		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			auto tr = transitions.sources.array[i];
			const char *name = obs_source_get_name(tr);
			auto action = subMenu->addAction(QString::fromUtf8(name));
			if (!obs_is_source_configurable(obs_source_get_unversioned_id(tr))) {
				action->setEnabled(false);
			}
			for (auto t : canvasDock->transitions) {
				if (strcmp(name, obs_source_get_name(t)) == 0) {
					action->setEnabled(false);
					break;
				}
			}
			connect(action, &QAction::triggered, [this, tr] {
				OBSDataAutoRelease d = obs_save_source(tr);
				OBSSourceAutoRelease t = obs_load_private_source(d);
				if (t) {
					canvasDock->transitions.emplace_back(t);
					auto n = QString::fromUtf8(obs_source_get_name(t));
					transition->addItem(n);
					transition->setCurrentText(n);
				}
			});
		}
		obs_frontend_source_list_free(&transitions);
		menu.addSeparator();
		size_t idx = 0;
		const char *id;
		while (obs_enum_transition_types(idx++, &id)) {
			if (!obs_is_source_configurable(id))
				continue;
			const char *display_name = obs_source_get_display_name(id);

			auto action = menu.addAction(QString::fromUtf8(display_name));
			connect(action, &QAction::triggered, [this, id] {
				OBSSourceAutoRelease t = obs_source_create_private(id, obs_source_get_display_name(id), nullptr);
				if (t) {
					std::string name = obs_source_get_name(t);
					while (true) {
						if (!NameDialog::AskForName(
							    this, QString::fromUtf8(obs_module_text("TransitionName")), name)) {
							obs_source_release(t);
							return;
						}
						if (name.empty())
							continue;
						bool found = false;
						for (auto tr : canvasDock->transitions) {
							if (strcmp(obs_source_get_name(tr), name.c_str()) == 0) {
								found = true;
								break;
							}
						}
						if (found)
							continue;

						obs_source_set_name(t, name.c_str());
						break;
					}
					canvasDock->transitions.emplace_back(t);
					auto n = QString::fromUtf8(obs_source_get_name(t));
					transition->addItem(n);
					transition->setCurrentText(n);
					obs_frontend_open_source_properties(t);
				}
			});
		}
		menu.exec(QCursor::pos());
	});

	hl->addWidget(addButton);

	removeButton = new QPushButton();
	removeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	removeButton->setAccessibleName(QString::fromUtf8(obs_frontend_get_locale_string("Basic.RemoveTransition")));
	removeButton->setToolTip(QString::fromUtf8(obs_frontend_get_locale_string("Basic.RemoveTransition")));
	removeButton->setIcon(QIcon(":/res/images/list_remove.png"));
	removeButton->setProperty("themeID", "removeIconSmall");
	removeButton->setProperty("class", "icon-minus");
	removeButton->setProperty("toolButton", true);
	removeButton->setFlat(false);

	connect(removeButton, &QPushButton::clicked, [this] {
		QMessageBox mb(
			QMessageBox::Question, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Title")),
			QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Text")).arg(transition->currentText()),
			QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No));
		mb.setDefaultButton(QMessageBox::NoButton);
		if (mb.exec() != QMessageBox::Yes)
			return;

		auto n = transition->currentText().toUtf8();
		for (auto it = canvasDock->transitions.begin(); it != canvasDock->transitions.end(); ++it) {
			if (strcmp(n.constData(), obs_source_get_name(it->Get())) == 0) {
				if (!obs_is_source_configurable(obs_source_get_unversioned_id(it->Get())))
					return;
				canvasDock->transitions.erase(it);
				break;
			}
		}
		transition->removeItem(transition->currentIndex());
		if (transition->currentIndex() < 0)
			transition->setCurrentIndex(0);
	});

	hl->addWidget(removeButton);

	propsButton = new QPushButton();
	propsButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	propsButton->setAccessibleName(QString::fromUtf8(obs_frontend_get_locale_string("Basic.TransitionProperties")));
	propsButton->setToolTip(QString::fromUtf8(obs_frontend_get_locale_string("Basic.TransitionProperties")));
	propsButton->setIcon(QIcon(":/settings/images/settings/general.svg"));
	propsButton->setProperty("themeID", "menuIconSmall");
	propsButton->setProperty("class", "icon-dots-vert");
	propsButton->setProperty("toolButton", true);
	propsButton->setFlat(false);

	connect(propsButton, &QPushButton::clicked, [this] {
		auto menu = QMenu(this);
		auto action = menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Rename")));
		connect(action, &QAction::triggered, [this] {
			auto tn = transition->currentText().toUtf8();
			obs_source_t *t = canvasDock->GetTransition(tn.constData());
			if (!t)
				return;
			std::string name = obs_source_get_name(t);
			while (true) {
				if (!NameDialog::AskForName(this, QString::fromUtf8(obs_module_text("TransitionName")), name)) {
					return;
				}
				if (name.empty())
					continue;
				bool found = false;
				for (auto tr : canvasDock->transitions) {
					if (strcmp(obs_source_get_name(tr), name.c_str()) == 0) {
						found = true;
						break;
					}
				}
				if (found)
					continue;

				transition->setItemText(transition->currentIndex(), QString::fromUtf8(name.c_str()));
				obs_source_set_name(t, name.c_str());
				break;
			}
		});
		action = menu.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Properties")));
		connect(action, &QAction::triggered, [this] {
			auto tn = transition->currentText().toUtf8();
			auto t = canvasDock->GetTransition(tn.constData());
			if (!t)
				return;
			obs_frontend_open_source_properties(t);
		});
		menu.exec(QCursor::pos());
	});

	hl->addWidget(propsButton);

	mainLayout->addLayout(hl);
	mainLayout->addStretch();

	setObjectName(QStringLiteral("contextContainer"));
	setLayout(mainLayout);

	for (auto t : canvasDock->transitions) {
		auto name = QString::fromUtf8(obs_source_get_name(t));
		transition->addItem(name);
		if (obs_weak_source_references_source(canvasDock->source, t)) {
			transition->setCurrentText(name);
			bool config = obs_is_source_configurable(obs_source_get_unversioned_id(t));
			removeButton->setEnabled(config);
			propsButton->setEnabled(config);
		}
	}
	connect(transition, &QComboBox::currentTextChanged, [this] {
		auto tn = transition->currentText().toUtf8();
		auto t = canvasDock->GetTransition(tn.constData());
		if (!t)
			return;
		canvasDock->SwapTransition(t);
		bool config = obs_is_source_configurable(obs_source_get_unversioned_id(t));
		removeButton->setEnabled(config);
		propsButton->setEnabled(config);
	});
}

CanvasTransitionsDock::~CanvasTransitionsDock() {}
