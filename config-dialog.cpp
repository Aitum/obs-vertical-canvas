#include "config-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>

#include "obs-module.h"
#include "version.h"

CanvasConfigDialog::CanvasConfigDialog(QMainWindow *parent) : QDialog(parent)
{

	auto mainLayout = new QFormLayout;
	mainLayout->setContentsMargins(9, 2, 9, 9);
	mainLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	mainLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTrailing |
				      Qt::AlignVCenter);

	resolution = new QComboBox;
	resolution->setEditable(true);
	resolution->addItem("720x1280");
	resolution->addItem("1080x1920");
	mainLayout->addRow(QString::fromUtf8(obs_module_text("Resolution")),
			   resolution);

	hideScenes =
		new QCheckBox(QString::fromUtf8(obs_module_text("HideScenes")));
	mainLayout->addWidget(hideScenes);

	replayBuffer = new QComboBox;
	replayBuffer->addItem(
		QString::fromUtf8(obs_module_text("ReplayBufferNone")));
	replayBuffer->addItem(
		QString::fromUtf8(obs_module_text("ReplayBufferStart")));
	replayBuffer->addItem(
		QString::fromUtf8(obs_module_text("ReplayBufferRecording")));
	replayBuffer->addItem(
		QString::fromUtf8(obs_module_text("ReplayBufferStreaming")));
	replayBuffer->addItem(QString::fromUtf8(
		obs_module_text("ReplayBufferVirtualCamera")));
	replayBuffer->addItem(
		QString::fromUtf8(obs_module_text("ReplayBufferAny")));
	mainLayout->addRow(QString::fromUtf8(obs_module_text("ReplayBuffer")),
			   replayBuffer);

	server = new QComboBox;
	server->setEditable(true);

	server->addItem("rtmps://a.rtmps.youtube.com:443/live2");
	server->addItem("rtmps://b.rtmps.youtube.com:443/live2?backup=1");
	server->addItem("rtmp://a.rtmp.youtube.com/live2");
	server->addItem("rtmp://b.rtmp.youtube.com/live2?backup=1");
	mainLayout->addRow(QString::fromUtf8(obs_module_text("Server")),
			   server);

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

	mainLayout->addRow(QString::fromUtf8(obs_module_text("Key")),
			   subLayout);

	mainLayout->addRow(QString::fromUtf8(obs_module_text("Version")),
			   new QLabel(QString::fromUtf8(PROJECT_VERSION)));

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
