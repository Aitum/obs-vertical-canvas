#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QMainWindow>
#include <qtextedit.h>
#include <obs-frontend-api.h>
#include <QGroupBox>
#include <qlistwidget.h>
#include <qspinbox.h>
#include <QFormLayout>
#include <QRadioButton>

#include "hotkey-edit.hpp"

class CanvasDock;

class OBSBasicSettings : public QDialog {
	Q_OBJECT
	Q_PROPERTY(QIcon generalIcon READ GetGeneralIcon WRITE SetGeneralIcon
			   DESIGNABLE true)
	Q_PROPERTY(QIcon streamIcon READ GetStreamIcon WRITE SetStreamIcon
			   DESIGNABLE true)
	Q_PROPERTY(QIcon outputIcon READ GetOutputIcon WRITE SetOutputIcon
			   DESIGNABLE true)
private:
	CanvasDock *canvasDock;
	QLabel *newVersion;
	QListWidget *listWidget;
	QComboBox *resolution;
	QCheckBox *showScenes;
	QSpinBox *videoBitrate;
	QComboBox *audioBitrate;
	QCheckBox *backtrackClip;
	QCheckBox *backtrackAlwaysOn;
	QSpinBox *backtrackDuration;
	QLineEdit *backtrackPath;

	QComboBox *server;
	QLineEdit *key;
	QCheckBox *streamingUseMain;
	std::vector<QRadioButton *> streamingAudioTracks;
	QComboBox *streamingEncoder;
	obs_properties_t *stream_encoder_properties = nullptr;
	obs_data_t *stream_encoder_settings = nullptr;
	std::map<obs_property_t *, QWidget *> stream_encoder_property_widgets;

	QLineEdit *recordPath;
	QLineEdit *recordFileFormat;

	QCheckBox *recordingUseMain;
	QLineEdit *filenameFormat;
	QComboBox *fileFormat;
	std::vector<QCheckBox *> recordingAudioTracks;
	QComboBox *recordingEncoder;
	obs_properties_t *record_encoder_properties = nullptr;
	obs_data_t *record_encoder_settings = nullptr;
	std::map<obs_property_t *, QWidget *> record_encoder_property_widgets;

	std::vector<OBSHotkeyWidget *> hotkeys;

	QIcon generalIcon;
	QIcon streamIcon;
	QIcon outputIcon;

	QIcon GetGeneralIcon() const;
	QIcon GetStreamIcon() const;
	QIcon GetOutputIcon() const;

	std::vector<obs_hotkey_t *>
	GetHotKeysFromOutput(obs_output_t *obs_output);
	std::vector<obs_key_combination_t>
	GetCombosForHotkey(obs_hotkey_id hotkey);
	std::vector<obs_hotkey_t *> GetHotkeyById(obs_hotkey_id hotkey);
	obs_hotkey_t *GetHotkeyByName(QString name);

	void SetEncoderBitrate(obs_encoder_t *obs_encoder);
	void AddProperty(obs_property_t *property, obs_data_t *settings,
			 QFormLayout *layout,
			 std::map<obs_property_t *, QWidget *>* widgets);
	void LoadProperty(obs_property_t *property, obs_data_t *settings,
			  QWidget *widget);
	void RefreshProperties(std::map<obs_property_t *, QWidget *> *widgets,
			       QFormLayout *layout);

private slots:
	void SetGeneralIcon(const QIcon &icon);
	void SetStreamIcon(const QIcon &icon);
	void SetOutputIcon(const QIcon &icon);

public:
	OBSBasicSettings(CanvasDock *canvas_dock,
			 QMainWindow *parent = nullptr);
	~OBSBasicSettings();

	void LoadSettings();
	void SaveSettings();
public slots:
};
