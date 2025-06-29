/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QDockWidget>
#include <QMenuBar>
#include <QApplication>
#include "phase-meter-dock.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static PhaseMeterDock *phaseMeterDock = nullptr;

// 音声データを取得するためのフィルタ
struct phase_meter_filter {
	obs_source_t *context;
	PhaseMeterWidget *widget;
	QString sourceName;
};

static const char *phase_meter_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Phase Meter Monitor");
}

static void *phase_meter_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct phase_meter_filter *filter = (struct phase_meter_filter *)bzalloc(sizeof(struct phase_meter_filter));
	filter->context = source;

	if (phaseMeterDock) {
		filter->widget = phaseMeterDock->getPhaseMeterWidget();
		filter->sourceName = QString::fromUtf8(obs_source_get_name(source));
		filter->widget->addAudioSource(filter->sourceName);
	}

	return filter;
}

static void phase_meter_filter_destroy(void *data)
{
	struct phase_meter_filter *filter = (struct phase_meter_filter *)data;

	if (filter->widget) {
		filter->widget->removeAudioSource(filter->sourceName);
	}

	bfree(filter);
}

static struct obs_audio_data *phase_meter_filter_audio(void *data, struct obs_audio_data *audio_data)
{
	struct phase_meter_filter *filter = (struct phase_meter_filter *)data;

	if (filter->widget && audio_data->frames > 0) {
		// ステレオ音声データを取得
		if (audio_data->data[0] && audio_data->data[1]) {
			const float *left = (const float *)audio_data->data[0];
			const float *right = (const float *)audio_data->data[1];

			filter->widget->updateAudioData(filter->sourceName, left, right, audio_data->frames);
		}
	}

	return audio_data;
}

static struct obs_source_info phase_meter_filter_info = {
	.id = "phase_meter_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = phase_meter_filter_get_name,
	.create = phase_meter_filter_create,
	.destroy = phase_meter_filter_destroy,
	.filter_audio = phase_meter_filter_audio,
};

bool obs_module_load(void)
{
    // フィルタを登録
	obs_register_source(&phase_meter_filter_info);

	// メインウィンドウが準備できたらドックを作成
	QMetaObject::invokeMethod(
		QApplication::instance(),
		[]() {
			QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
			if (mainWindow) {
				phaseMeterDock = new PhaseMeterDock(mainWindow);

				// ドックをメインウィンドウに追加
				mainWindow->addDockWidget(Qt::RightDockWidgetArea, phaseMeterDock);

				// メニューにアクションを追加
				QAction *action = new QAction("Phase Meter", mainWindow);
				action->setCheckable(true);
				action->setChecked(true);

				QObject::connect(action, &QAction::toggled, [](bool checked) {
					if (phaseMeterDock) {
						phaseMeterDock->setVisible(checked);
					}
				});

				QObject::connect(phaseMeterDock, &QDockWidget::visibilityChanged,
						 [action](bool visible) { action->setChecked(visible); });

				// ツールメニューに追加
				QMenu *toolsMenu = nullptr;
				for (QMenu *menu : mainWindow->menuBar()->findChildren<QMenu *>()) {
					if (menu->title().contains("Tools") || menu->title().contains("ツール")) {
						toolsMenu = menu;
						break;
					}
				}

				if (toolsMenu) {
					toolsMenu->addAction(action);
				}
			}
		},
		Qt::QueuedConnection);

	return true;
}

void obs_module_post_load(void)
{
	obs_log(LOG_INFO, "plugin post-load actions executed");
	
	// Register the Phase Meter widget
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	QDockWidget *dock = new QDockWidget(mainWindow);
	QDockWidget *myDock = new QDockWidget();
	if (mainWindow) 
	{
		dock->setWidget(myDock);

		dock->setWindowTitle(QString::fromUtf8(obs_module_text("My Doc Sample"), -1));
		dock->resize(100, 100);
		dock->setFloating(true);
		dock->hide();

		// depcretd
		//obs_frontend_add_dock(dock);

		const char *dock_id = "my_doc_sample_dock";
		const char *dock_title = obs_module_text("My Doc Sample");

		obs_frontend_add_dock_by_id(dock_id, dock_title, dock);

	}
}

void obs_module_unload(void)
{
	if (phaseMeterDock) {
		delete phaseMeterDock;
		phaseMeterDock = nullptr;
	}
	obs_log(LOG_INFO, "plugin unloaded");
}
