#include "name-dialog.hpp"

#include <QVBoxLayout>

#include "obs-module.h"

NameDialog::NameDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("SceneName")));
	setModal(true);
	setWindowModality(Qt::WindowModality::WindowModal);
	setMinimumWidth(200);
	setMinimumHeight(100);
	QVBoxLayout *layout = new QVBoxLayout;
	setLayout(layout);

	userText = new QLineEdit(this);
	layout->addWidget(userText);

	QDialogButtonBox *buttonbox = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttonbox);
	buttonbox->setCenterButtons(true);
	connect(buttonbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

static bool IsWhitespace(char ch)
{
	return ch == ' ' || ch == '\t';
}

static void CleanWhitespace(std::string &str)
{
	while (!str.empty() && IsWhitespace(str.back()))
		str.erase(str.end() - 1);
	while (!str.empty() && IsWhitespace(str.front()))
		str.erase(str.begin());
}

bool NameDialog::AskForName(QWidget *parent, std::string &name)
{
	NameDialog dialog(parent);
	dialog.userText->setMaxLength(170);
	dialog.userText->setText(QString::fromUtf8(name.c_str()));
	dialog.userText->selectAll();

	if (dialog.exec() != DialogCode::Accepted) {
		return false;
	}
	name = dialog.userText->text().toUtf8().constData();
	CleanWhitespace(name);
	return true;
}
