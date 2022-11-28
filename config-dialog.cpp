#include "config-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>

#include "obs-module.h"

MultiCanvasConfigDialog::MultiCanvasConfigDialog(QMainWindow *parent)
	: QDialog(parent)
{
	int row = 0;
	auto mainLayout = new QGridLayout;
	mainLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *label = new QLabel(obs_module_text("Resolution"));
	mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	QComboBox *combo = new QComboBox;
	combo->setEditable(true);
	mainLayout->addWidget(combo, row, 1, Qt::AlignLeft);

	row++;

	//label = new QLabel(obs_module_text("ReplayBuffer"));
	//mainLayout->addWidget(label, row, 0, Qt::AlignRight);

	QCheckBox *checkBox = new QCheckBox(obs_module_text("ReplayBuffer"));
	mainLayout->addWidget(checkBox, row, 1, Qt::AlignLeft);

	QWidget *widget = new QWidget;
	widget->setLayout(mainLayout);
	widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

	QScrollArea *scrollArea = new QScrollArea;
	scrollArea->setWidget(widget);
	scrollArea->setWidgetResizable(true);

	QPushButton *okButton = new QPushButton(obs_module_text("Ok"));
	connect(okButton, &QPushButton::clicked, [this] { done(1); });

	QPushButton *cancelButton = new QPushButton(obs_module_text("Cancel"));
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

	setWindowTitle(obs_module_text("MultiCanvas"));
	setSizeGripEnabled(true);
}

MultiCanvasConfigDialog::~MultiCanvasConfigDialog() {}
