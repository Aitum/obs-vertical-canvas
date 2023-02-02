#include "config-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>

#include "hotkey-edit.hpp"
#include "obs-module.h"
#include "version.h"
#include "vertical-canvas.hpp"

OBSBasicSettings::OBSBasicSettings(CanvasDock *canvas_dock, QMainWindow *parent)
	: QDialog(parent), canvasDock(canvas_dock)
{

	listWidget = new QListWidget(this);
	listWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
	listWidget->setMaximumWidth(120);
	QListWidgetItem *listwidgetitem = new QListWidgetItem(listWidget);
	listwidgetitem->setIcon(QIcon(
		QString::fromUtf8(":/settings/images/settings/general.svg")));
	listwidgetitem->setText(QString::fromUtf8(obs_module_text("General")));

	listwidgetitem = new QListWidgetItem(listWidget);
	listwidgetitem->setIcon(QIcon(
		QString::fromUtf8(":/settings/images/settings/stream.svg")));
	listwidgetitem->setText(
		QString::fromUtf8(obs_module_text("Streaming")));

	listwidgetitem = new QListWidgetItem(listWidget);
	listwidgetitem->setIcon(QIcon(
		QString::fromUtf8(":/settings/images/settings/output.svg")));
	listwidgetitem->setText(
		QString::fromUtf8(obs_module_text("Recording")));

	listWidget->setCurrentRow(0);

	auto settingsPages = new QStackedWidget;
	settingsPages->setContentsMargins(0, 0, 0, 0);
	settingsPages->setFrameShape(QFrame::NoFrame);
	settingsPages->setLineWidth(0);

	QWidget *generalPage = new QWidget;
	QScrollArea *scrollArea = new QScrollArea;
	scrollArea->setWidget(generalPage);
	scrollArea->setWidgetResizable(true);
	scrollArea->setLineWidth(0);
	scrollArea->setFrameShape(QFrame::NoFrame);
	settingsPages->addWidget(scrollArea);

	auto streamingPage =
		new QGroupBox(QString::fromUtf8(obs_module_text("Streaming")));
	scrollArea = new QScrollArea;
	scrollArea->setWidget(streamingPage);
	scrollArea->setWidgetResizable(true);
	scrollArea->setLineWidth(0);
	scrollArea->setFrameShape(QFrame::NoFrame);
	settingsPages->addWidget(scrollArea);

	auto recordingPage =
		new QGroupBox(QString::fromUtf8(obs_module_text("Recording")));
	scrollArea = new QScrollArea;
	scrollArea->setWidget(recordingPage);
	scrollArea->setWidgetResizable(true);
	scrollArea->setLineWidth(0);
	scrollArea->setFrameShape(QFrame::NoFrame);
	settingsPages->addWidget(scrollArea);

	connect(listWidget, &QListWidget::currentRowChanged, settingsPages,
		&QStackedWidget::setCurrentIndex);

	auto generalGroup =
		new QGroupBox(QString::fromUtf8(obs_module_text("General")));

	auto generalLayout = new QFormLayout;
	generalLayout->setContentsMargins(9, 2, 9, 9);
	generalLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	generalLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

	generalGroup->setLayout(generalLayout);

	resolution = new QComboBox;
	resolution->setEditable(true);
	resolution->addItem("720x1280");
	resolution->addItem("1080x1920");
	generalLayout->addRow(QString::fromUtf8(obs_module_text("Resolution")),
			      resolution);

	showScenes =
		new QCheckBox(QString::fromUtf8(obs_module_text("ShowScenes")));
	generalLayout->addWidget(showScenes);

	videoBitrate = new QSpinBox;
	videoBitrate->setSuffix(" Kbps");
	videoBitrate->setMinimum(200);
	videoBitrate->setMaximum(1000000);
	generalLayout->addRow(QString::fromUtf8(obs_frontend_get_locale_string(
				      "Basic.Settings.Output.VideoBitrate")),
			      videoBitrate);

	audioBitrate = new QComboBox;
	audioBitrate->addItem("64", QVariant(64));
	audioBitrate->addItem("96", QVariant(96));
	audioBitrate->addItem("128", QVariant(128));
	audioBitrate->addItem("160", QVariant(160));
	audioBitrate->addItem("192", QVariant(192));
	audioBitrate->addItem("224", QVariant(224));
	audioBitrate->addItem("256", QVariant(256));
	audioBitrate->addItem("288", QVariant(288));
	audioBitrate->addItem("320", QVariant(320));

	audioBitrate->setCurrentText("160");
	generalLayout->addRow(QString::fromUtf8(obs_frontend_get_locale_string(
				      "Basic.Settings.Output.AudioBitrate")),
			      audioBitrate);

	auto backtrackGroup =
		new QGroupBox(QString::fromUtf8(obs_module_text("Backtrack")));
	auto backtrackLayout = new QFormLayout;
	backtrackLayout->setContentsMargins(9, 2, 9, 9);
	backtrackLayout->setFieldGrowthPolicy(
		QFormLayout::AllNonFixedFieldsGrow);
	backtrackLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	backtrackGroup->setLayout(backtrackLayout);

	backtrackAlwaysOn = new QCheckBox(
		QString::fromUtf8(obs_module_text("BacktrackAlwaysOn")));
	backtrackLayout->addWidget(backtrackAlwaysOn);

	backtrackClip = new QCheckBox(
		QString::fromUtf8(obs_module_text("BacktrackEnable")));
	backtrackLayout->addWidget(backtrackClip);

	backtrackDuration = new QSpinBox;
	backtrackDuration->setSuffix(" sec");
	backtrackDuration->setMinimum(5);
	backtrackDuration->setMaximum(21600);
	backtrackLayout->addRow(
		QString::fromUtf8(obs_module_text("BacktrackDuration")),
		backtrackDuration);

	QLayout *backtrackPathLayout = new QHBoxLayout;
	backtrackPath = new QLineEdit();
	backtrackPath->setReadOnly(true);

	auto button = new QPushButton(
		QString::fromUtf8(obs_frontend_get_locale_string("Browse")));
	button->setProperty("themeID", "settingsButtons");
	connect(button, &QPushButton::clicked, [this] {
		const QString dir = QFileDialog::getExistingDirectory(
			this,
			QString::fromUtf8(obs_module_text("BacktrackPath")),
			backtrackPath->text(),
			QFileDialog::ShowDirsOnly |
				QFileDialog::DontResolveSymlinks);
		if (dir.isEmpty())
			return;
		backtrackPath->setText(dir);
	});

	backtrackPathLayout->addWidget(backtrackPath);
	backtrackPathLayout->addWidget(button);

	backtrackLayout->addRow(
		QString::fromUtf8(obs_module_text("BacktrackPath")),
		backtrackPathLayout);

	auto replayHotkeys = GetHotKeysFromOutput(canvasDock->replayOutput);

	for (auto &hotkey : replayHotkeys) {
		auto id = obs_hotkey_get_id(hotkey);
		std::vector<obs_key_combination_t> combos =
			GetCombosForHotkey(id);
		auto hn = obs_hotkey_get_name(hotkey);
		auto hw = new OBSHotkeyWidget(this, id, hn, combos);
		if (strcmp(hn, "ReplayBuffer.Save") == 0) {
			backtrackLayout->addRow(
				QString::fromUtf8(
					obs_module_text("SaveBacktrackHotkey")),
				hw);
		} else {
			backtrackLayout->addRow(
				QString::fromUtf8(
					obs_hotkey_get_description(hotkey)),
				hw);
		}
		hotkeys.push_back(hw);
	}
	auto maxWidth = 180;
	for (int row = 0; row < generalLayout->rowCount(); row++) {
		auto item = generalLayout->itemAt(row, QFormLayout::LabelRole);
		if (!item)
			continue;
		auto label = dynamic_cast<QLabel *>(item->widget());
		if (!label)
			continue;
		label->setFixedWidth(maxWidth);
		label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}
	for (int row = 0; row < backtrackLayout->rowCount(); row++) {
		auto item =
			backtrackLayout->itemAt(row, QFormLayout::LabelRole);
		if (!item)
			continue;
		auto label = dynamic_cast<QLabel *>(item->widget());
		if (!label)
			continue;
		label->setFixedWidth(maxWidth);
		label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	}

	auto vb = new QVBoxLayout;
	vb->setContentsMargins(0, 0, 0, 0);
	vb->addWidget(generalGroup);
	vb->addWidget(backtrackGroup);
	generalPage->setLayout(vb);

	auto streamingLayout = new QFormLayout;
	streamingLayout->setContentsMargins(9, 2, 9, 9);
	streamingLayout->setFieldGrowthPolicy(
		QFormLayout::AllNonFixedFieldsGrow);
	streamingLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignTrailing |
					   Qt::AlignVCenter);

	server = new QComboBox;
	server->setEditable(true);

	server->addItem("rtmps://a.rtmps.youtube.com:443/live2");
	server->addItem("rtmps://b.rtmps.youtube.com:443/live2?backup=1");
	server->addItem("rtmp://a.rtmp.youtube.com/live2");
	server->addItem("rtmp://b.rtmp.youtube.com/live2?backup=1");
	streamingLayout->addRow(QString::fromUtf8(obs_module_text("Server")),
				server);

	QLayout *subLayout = new QHBoxLayout();
	key = new QLineEdit;
	key->setEchoMode(QLineEdit::Password);

	QPushButton *show = new QPushButton();
	show->setText(
		QString::fromUtf8(obs_frontend_get_locale_string("Show")));
	show->setCheckable(true);
	connect(show, &QAbstractButton::toggled, [=](bool hide) {
		show->setText(QString::fromUtf8(
			hide ? obs_frontend_get_locale_string("Hide")
			     : obs_frontend_get_locale_string("Show")));
		key->setEchoMode(hide ? QLineEdit::Normal
				      : QLineEdit::Password);
	});

	subLayout->addWidget(key);
	subLayout->addWidget(show);

	streamingLayout->addRow(QString::fromUtf8(obs_module_text("Key")),
				subLayout);

	OBSHotkeyWidget *otherHotkey = nullptr;
	auto hotkey =
		GetHotkeyByName(canvasDock->objectName() + "StartStreaming");
	if (hotkey) {
		auto id = obs_hotkey_get_id(hotkey);
		std::vector<obs_key_combination_t> combos =
			GetCombosForHotkey(id);
		auto hn = obs_hotkey_get_name(hotkey);
		auto hw = new OBSHotkeyWidget(this, id, hn, combos);
		otherHotkey = hw;
		auto label = new OBSHotkeyLabel;
		label->setText(QString::fromUtf8(
			obs_module_text("StartStreamingHotkey")));
		hw->label = label;
		streamingLayout->addRow(label, hw);
		hotkeys.push_back(hw);
	}

	hotkey = GetHotkeyByName(canvasDock->objectName() + "StopStreaming");
	if (hotkey) {
		auto id = obs_hotkey_get_id(hotkey);
		std::vector<obs_key_combination_t> combos =
			GetCombosForHotkey(id);
		auto hn = obs_hotkey_get_name(hotkey);
		auto hw = new OBSHotkeyWidget(this, id, hn, combos);
		auto label = new OBSHotkeyLabel;
		label->setText(QString::fromUtf8(
			obs_module_text("StopStreamingHotkey")));
		hw->label = label;
		streamingLayout->addRow(label, hw);
		hotkeys.push_back(hw);
		if (otherHotkey) {
			hw->label->pairPartner = otherHotkey->label;
			otherHotkey->label->pairPartner = hw->label;
		}
	}

	streamingPage->setLayout(streamingLayout);

	auto recordLayout = new QFormLayout;
	recordLayout->setContentsMargins(9, 2, 9, 9);
	recordLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	recordLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

	QLayout *recordPathLayout = new QHBoxLayout();
	recordPath = new QLineEdit();
	recordPath->setReadOnly(true);

	button = new QPushButton(
		QString::fromUtf8(obs_frontend_get_locale_string("Browse")));
	button->setProperty("themeID", "settingsButtons");
	connect(button, &QPushButton::clicked, [this] {
		const QString dir = QFileDialog::getExistingDirectory(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Basic.Settings.Output.Simple.SavePath")),
			backtrackPath->text(),
			QFileDialog::ShowDirsOnly |
				QFileDialog::DontResolveSymlinks);
		if (dir.isEmpty())
			return;
		recordPath->setText(dir);
	});

	recordPathLayout->addWidget(recordPath);
	recordPathLayout->addWidget(button);

	recordLayout->addRow(QString::fromUtf8(obs_frontend_get_locale_string(
				     "Basic.Settings.Output.Simple.SavePath")),
			     recordPathLayout);

	otherHotkey = nullptr;

	hotkey = GetHotkeyByName(canvasDock->objectName() + "StartRecording");
	if (hotkey) {
		auto id = obs_hotkey_get_id(hotkey);
		std::vector<obs_key_combination_t> combos =
			GetCombosForHotkey(id);
		auto hn = obs_hotkey_get_name(hotkey);
		auto hw = new OBSHotkeyWidget(this, id, hn, combos);
		otherHotkey = hw;
		auto label = new OBSHotkeyLabel;
		label->setText(QString::fromUtf8(
			obs_module_text("StartRecordingHotkey")));
		hw->label = label;
		recordLayout->addRow(label, hw);
		hotkeys.push_back(hw);
	}

	hotkey = GetHotkeyByName(canvasDock->objectName() + "StopRecording");
	if (hotkey) {
		auto id = obs_hotkey_get_id(hotkey);
		std::vector<obs_key_combination_t> combos =
			GetCombosForHotkey(id);
		auto hn = obs_hotkey_get_name(hotkey);
		auto hw = new OBSHotkeyWidget(this, id, hn, combos);

		auto label = new OBSHotkeyLabel;
		label->setText(QString::fromUtf8(
			obs_module_text("StopRecordingHotkey")));
		hw->label = label;
		recordLayout->addRow(label, hw);
		hotkeys.push_back(hw);
		if (otherHotkey) {
			hw->label->pairPartner = otherHotkey->label;
			otherHotkey->label->pairPartner = hw->label;
		}
	}

	recordingPage->setLayout(recordLayout);

	QPushButton *okButton = new QPushButton(
		QString::fromUtf8(obs_frontend_get_locale_string("OK")));
	connect(okButton, &QPushButton::clicked, [this] {
		SaveSettings();
		close();
	});

	QPushButton *cancelButton = new QPushButton(
		QString::fromUtf8(obs_frontend_get_locale_string("Cancel")));
	connect(cancelButton, &QPushButton::clicked, [this] { close(); });

	QHBoxLayout *contentLayout = new QHBoxLayout;
	contentLayout->addWidget(listWidget);

	contentLayout->addWidget(settingsPages, 1);

	QHBoxLayout *bottomLayout = new QHBoxLayout;
	const auto version =
		new QLabel(QString::fromUtf8(obs_module_text("Version")) + " " +
			   QString::fromUtf8(PROJECT_VERSION) + " " +
			   QString::fromUtf8(obs_module_text("MadeBy")) +
			   " <a href=\"https://aitum.tv\">Aitum</a>");
	version->setOpenExternalLinks(true);
	version->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	bottomLayout->addWidget(version, 1, Qt::AlignLeft);
	bottomLayout->addWidget(okButton, 0, Qt::AlignRight);
	bottomLayout->addWidget(cancelButton, 0, Qt::AlignRight);

	QVBoxLayout *vlayout = new QVBoxLayout;
	vlayout->setContentsMargins(11, 11, 11, 11);
	vlayout->addLayout(contentLayout);
	vlayout->addLayout(bottomLayout);
	setLayout(vlayout);

	setWindowTitle(obs_module_text("VerticalSettings"));
	setSizeGripEnabled(true);
}

OBSBasicSettings::~OBSBasicSettings() {}

QIcon OBSBasicSettings::GetGeneralIcon() const
{
	return generalIcon;
}

QIcon OBSBasicSettings::GetStreamIcon() const
{
	return streamIcon;
}

QIcon OBSBasicSettings::GetOutputIcon() const
{
	return outputIcon;
}

void OBSBasicSettings::SetGeneralIcon(const QIcon &icon)
{
	listWidget->item(0)->setIcon(icon);
}

void OBSBasicSettings::SetStreamIcon(const QIcon &icon)
{
	listWidget->item(1)->setIcon(icon);
}

void OBSBasicSettings::SetOutputIcon(const QIcon &icon)
{
	listWidget->item(2)->setIcon(icon);
}

void OBSBasicSettings::LoadSettings()
{

	resolution->setCurrentText(QString::number(canvasDock->canvas_width) +
				   "x" +
				   QString::number(canvasDock->canvas_height));
	resolution->setEnabled(
		!obs_output_active(canvasDock->recordOutput) &&
		!obs_output_active(canvasDock->streamOutput) &&
		!obs_output_active(canvasDock->virtualCamOutput));
	showScenes->setChecked(!canvasDock->hideScenes);
	videoBitrate->setValue(
		canvasDock->videoBitrate ? canvasDock->videoBitrate : 6000);
	for (int i = 0; i < audioBitrate->count(); i++) {
		if (audioBitrate->itemData(i).toUInt() ==
				    canvasDock->audioBitrate
			    ? canvasDock->audioBitrate
			    : 160) {
			audioBitrate->setCurrentIndex(i);
		}
	}
	backtrackClip->setChecked(canvasDock->startReplay);
	backtrackAlwaysOn->setChecked(canvasDock->replayAlwaysOn);
	backtrackDuration->setValue(canvasDock->replayDuration);
	backtrackPath->setText(QString::fromUtf8(canvasDock->replayPath));

	key->setEchoMode(QLineEdit::Password);
	key->setText(QString::fromUtf8(canvasDock->stream_key));
	server->setCurrentText(QString::fromUtf8(canvasDock->stream_server));

	recordPath->setText(QString::fromUtf8(canvasDock->recordPath));
}

void OBSBasicSettings::SaveSettings()
{
	for (auto &hw : hotkeys) {
		hw->Save();
	}

	if (canvasDock->hideScenes == showScenes->isChecked()) {
		canvasDock->hideScenes = !showScenes->isChecked();
		auto sl = canvasDock->GetGlobalScenesList();
		for (int j = 0; j < sl->count(); j++) {
			auto item = sl->item(j);
			if (canvasDock->HasScene(item->text())) {
				item->setHidden(canvasDock->hideScenes);
			}
		}
	}
	const auto res = resolution->currentText();
	uint32_t width, height;
	if (sscanf(res.toUtf8().constData(), "%dx%d", &width, &height) == 2 &&
	    width && height && width != canvasDock->canvas_width &&
	    height != canvasDock->canvas_height) {
		if (obs_output_active(canvasDock->replayOutput))
			obs_output_stop(canvasDock->replayOutput);
		canvasDock->DestroyVideo();

		canvasDock->SwitchScene("");

		canvasDock->canvas_width = width;
		canvasDock->canvas_height = height;

		const QString name = "CanvasDock" +
				     QString::number(canvasDock->canvas_width) +
				     "x" +
				     QString::number(canvasDock->canvas_height);
		canvasDock->setObjectName(name);

		if (canvasDock->scenesDock)
			canvasDock->scenesDock->setObjectName(name + "Scenes");

		if (canvasDock->sourcesDock)
			canvasDock->sourcesDock->setObjectName(name +
							       "Sources");

		canvasDock->LoadScenes();
	}
	uint32_t bitrate = (uint32_t)videoBitrate->value();
	if (bitrate != canvasDock->videoBitrate) {
		canvasDock->videoBitrate = bitrate;
		SetEncoderBitrate(
			obs_output_get_video_encoder(canvasDock->replayOutput));
		SetEncoderBitrate(
			obs_output_get_video_encoder(canvasDock->recordOutput));
		SetEncoderBitrate(
			obs_output_get_video_encoder(canvasDock->streamOutput));
	}
	bitrate = (uint32_t)audioBitrate->currentData().toUInt();
	if (bitrate != canvasDock->audioBitrate) {
		canvasDock->audioBitrate = bitrate;
		for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
			SetEncoderBitrate(obs_output_get_audio_encoder(
				canvasDock->replayOutput, i));
			SetEncoderBitrate(obs_output_get_audio_encoder(
				canvasDock->recordOutput, i));
			SetEncoderBitrate(obs_output_get_audio_encoder(
				canvasDock->streamOutput, i));
		}
	}

	auto startReplay = backtrackClip->isChecked();
	auto replayAlwaysOn = backtrackAlwaysOn->isChecked();
	auto duration = (uint32_t)backtrackDuration->value();
	std::string replayPath = backtrackPath->text().toUtf8().constData();
	if (duration != canvasDock->replayDuration ||
	    replayPath != canvasDock->replayPath ||
	    canvasDock->startReplay != startReplay ||
	    replayAlwaysOn != canvasDock->replayAlwaysOn) {
		canvasDock->replayDuration = duration;
		canvasDock->replayPath = replayPath;
		canvasDock->startReplay = startReplay;
		canvasDock->replayAlwaysOn = replayAlwaysOn;
		if (replayAlwaysOn || startReplay) {
			if (obs_output_active(canvasDock->replayOutput)) {
				canvasDock->StopReplayBuffer();
				QTimer::singleShot(500, this, [this] {
					canvasDock->CheckReplayBuffer(true);
				});
			} else {
				canvasDock->CheckReplayBuffer(true);
			}
		} else {
			canvasDock->StopReplayBuffer();
		}
	}

	std::string sk = key->text().toUtf8().constData();
	std::string ss = server->currentText().toUtf8().constData();
	if (sk != canvasDock->stream_key || ss != canvasDock->stream_server) {
		canvasDock->stream_key = sk;
		canvasDock->stream_server = ss;
		if (obs_output_active(canvasDock->streamOutput)) {
			//TODO restart
			//StopStream();
			//StartStream();
		}
	}

	canvasDock->recordPath = recordPath->text().toUtf8().constData();
}

void OBSBasicSettings::SetEncoderBitrate(obs_encoder_t *encoder)
{
	if (!encoder)
		return;
	auto settings = obs_encoder_get_settings(encoder);
	if (!settings)
		return;
	auto bitrate = obs_encoder_get_type(encoder) == OBS_ENCODER_AUDIO
			       ? canvasDock->audioBitrate
			       : canvasDock->videoBitrate;
	if (obs_data_get_int(settings, "bitrate") == bitrate) {
		obs_data_release(settings);
		return;
	}
	obs_data_set_int(settings, "bitrate", bitrate);
	obs_encoder_update(encoder, nullptr);
	obs_data_release(settings);
}

std::vector<obs_hotkey_t *>
OBSBasicSettings::GetHotKeysFromOutput(obs_output_t *output)
{
	struct find_hotkey {
		std::vector<obs_hotkey_t *> hotkeys;
		obs_weak_output_t *output;
	};
	find_hotkey t = {};
	t.output = obs_output_get_weak_output(output);
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			UNUSED_PARAMETER(id);
			if (obs_hotkey_get_registerer_type(key) !=
			    OBS_HOTKEY_REGISTERER_OUTPUT)
				return true;
			auto hp = (struct find_hotkey *)data;
			auto o = obs_hotkey_get_registerer(key);
			if (o == hp->output ||
			    obs_weak_output_references_output(
				    hp->output, (obs_output_t *)o)) {
				hp->hotkeys.push_back(key);
			}
			return true;
		},
		&t);
	obs_weak_output_release(t.output);
	return t.hotkeys;
}

std::vector<obs_key_combination_t>
OBSBasicSettings::GetCombosForHotkey(obs_hotkey_id hotkey)
{
	struct find_combos {
		obs_hotkey_id hotkey;
		std::vector<obs_key_combination_t> combos;
	};
	find_combos t = {hotkey, {}};
	obs_enum_hotkey_bindings(
		[](void *data, size_t idx, obs_hotkey_binding_t *binding) {
			UNUSED_PARAMETER(idx);
			auto t = (struct find_combos *)data;
			if (t->hotkey ==
			    obs_hotkey_binding_get_hotkey_id(binding)) {
				t->combos.push_back(
					obs_hotkey_binding_get_key_combination(
						binding));
			}
			return true;
		},
		&t);
	return t.combos;
}

std::vector<obs_hotkey_t *>
OBSBasicSettings::GetHotkeyById(obs_hotkey_id hotkey)
{
	struct find_hotkey {
		std::vector<obs_hotkey_t *> hotkeys;
		obs_hotkey_id hotkey_id;
	};
	find_hotkey t = {};
	t.hotkey_id = hotkey;
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			auto hp = (struct find_hotkey *)data;
			if (hp->hotkey_id == id) {
				hp->hotkeys.push_back(key);
			}
			return true;
		},
		&t);
	return t.hotkeys;
}

obs_hotkey_t *OBSBasicSettings::GetHotkeyByName(QString name)
{
	struct find_hotkey {
		obs_hotkey_t *hotkey;
		const char *name;
	};
	find_hotkey t = {};
	auto n = name.toUtf8();
	t.name = n.constData();
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			UNUSED_PARAMETER(id);
			const auto hp = (struct find_hotkey *)data;
			const auto hn = obs_hotkey_get_name(key);
			if (strcmp(hp->name, hn) == 0)
				hp->hotkey = key;
			return true;
		},
		&t);
	return t.hotkey;
}
