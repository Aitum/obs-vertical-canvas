#include "config-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>

#include "obs-module.h"

CanvasConfigDialog::CanvasConfigDialog(QMainWindow *parent) : QDialog(parent)
{
	int row = 0;
	auto mainLayout = new QGridLayout;
	mainLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *label =
		new QLabel(QString::fromUtf8(obs_module_text("Resolution")));
	mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	QComboBox *combo = new QComboBox;
	combo->setEditable(true);
	combo->addItem("720x1280");
	combo->addItem("1080x1920");
	mainLayout->addWidget(combo, row, 1, Qt::AlignLeft);

	row++;

	//label = new QLabel(obs_module_text("ReplayBuffer"));
	//mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	replayBuffer = new QCheckBox(
		QString::fromUtf8(obs_module_text("ReplayBuffer")));
	mainLayout->addWidget(replayBuffer, row, 1, Qt::AlignLeft);

	row++;

	label = new QLabel(QString::fromUtf8(obs_module_text("Server")));
	mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	server = new QComboBox;
	server->setEditable(true);

	server->addItem("rtmps://a.rtmps.youtube.com:443/live2");
	server->addItem("rtmps://b.rtmps.youtube.com:443/live2?backup=1");
	server->addItem("rtmp://a.rtmp.youtube.com/live2");
	server->addItem("rtmp://b.rtmp.youtube.com/live2?backup=1");

	mainLayout->addWidget(server, row, 1, Qt::AlignLeft);

	row++;

	label = new QLabel(QString::fromUtf8(obs_module_text("Key")));
	mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	QLayout *subLayout = new QHBoxLayout();
	key = new QLineEdit;
	key->setEchoMode(QLineEdit::Password);

	QPushButton *show = new QPushButton();
	show->setText(QString::fromUtf8(obs_module_text("Show")));
	show->setCheckable(true);
	connect(show, &QAbstractButton::toggled, [=](bool hide) {
		show->setText(
			QString::fromUtf8(hide ? obs_module_text("Hide")
					       : obs_module_text("Show")));
		key->setEchoMode(hide ? QLineEdit::Normal
				      : QLineEdit::Password);
	});

	subLayout->addWidget(key);
	subLayout->addWidget(show);

	mainLayout->addLayout(subLayout, row, 1, Qt::AlignLeft);

	mainLayout->setColumnStretch(0, 0);
	mainLayout->setColumnStretch(1, 1);

	QWidget *widget = new QWidget;
	widget->setLayout(mainLayout);
	widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	QScrollArea *scrollArea = new QScrollArea;
	scrollArea->setWidget(widget);
	scrollArea->setWidgetResizable(true);

	QPushButton *okButton =
		new QPushButton(QString::fromUtf8(obs_module_text("Ok")));
	connect(okButton, &QPushButton::clicked, [this] { done(1); });

	QPushButton *cancelButton =
		new QPushButton(QString::fromUtf8(obs_module_text("Cancel")));
	connect(cancelButton, &QPushButton::clicked, [this] { close(); });

	QHBoxLayout *bottomLayout = new QHBoxLayout;
	bottomLayout->addWidget(new QWidget, 1, Qt::AlignLeft);
	bottomLayout->addWidget(okButton, 0, Qt::AlignRight);
	bottomLayout->addWidget(cancelButton, 0, Qt::AlignRight);

	QVBoxLayout *vlayout = new QVBoxLayout;
	vlayout->setContentsMargins(11, 11, 11, 11);
	vlayout->addWidget(scrollArea);
	vlayout->addLayout(bottomLayout);
	setLayout(vlayout);

	setWindowTitle(obs_module_text("VerticalCanvas"));
	setSizeGripEnabled(true);
}

CanvasConfigDialog::~CanvasConfigDialog() {}
