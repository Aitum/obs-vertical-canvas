#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <string>

class NameDialog : public QDialog {
	Q_OBJECT
public:
	static bool AskForName(QWidget *parent, std::string &name);

private:
	NameDialog(QWidget *parent);
	QLineEdit *userText;
};
