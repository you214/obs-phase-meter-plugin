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
#include <QAction>
#include <QMenuBar>
#include <QApplication>
#include <QTimer>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include "phase-meter-dock.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-phase-meter", "en-US")

// グローバル変数をスマートポインタで管理
static QPointer<PhaseMeterDock> phaseMeterDock = nullptr;
static QMutex filterMutex;
static QList<struct phase_meter_filter *> activeFilters;

// 音声データを取得するためのフィルタ
struct phase_meter_filter {
	obs_source_t *context;
	QPointer<PhaseMeterWidget> widget;
	QString sourceName;
	bool isDestroying;

	phase_meter_filter() : context(nullptr), isDestroying(false) {}
};

static const char *phase_meter_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Phase Meter Monitor");
}

static void *phase_meter_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct phase_meter_filter *filter = new phase_meter_filter();
	filter->context = source;

	// メインスレッドでウィジェット操作を実行
	QMetaObject::invokeMethod(
		QApplication::instance(),
		[filter, source]() {
			if (phaseMeterDock && !phaseMeterDock.isNull()) {
				filter->widget = phaseMeterDock->getPhaseMeterWidget();
				filter->sourceName = QString::fromUtf8(obs_source_get_name(source));
				if (filter->widget) {
					filter->widget->addAudioSource(filter->sourceName);
				}
			}

			QMutexLocker locker(&filterMutex);
			activeFilters.append(filter);
		},
		Qt::QueuedConnection);

	return filter;
}

static void phase_meter_filter_destroy(void *data)
{
	struct phase_meter_filter *filter = static_cast<struct phase_meter_filter *>(data);
	if (!filter)
		return;

	filter->isDestroying = true;

	// メインスレッドでウィジェット操作を実行
	QMetaObject::invokeMethod(
		QApplication::instance(),
		[filter]() {
			if (filter->widget && !filter->widget.isNull()) {
				filter->widget->removeAudioSource(filter->sourceName);
			}

			QMutexLocker locker(&filterMutex);
			activeFilters.removeAll(filter);

			delete filter;
		},
		Qt::QueuedConnection);
}

static struct obs_audio_data *phase_meter_filter_audio(void *data, struct obs_audio_data *audio_data)
{
	struct phase_meter_filter *filter = static_cast<struct phase_meter_filter *>(data);

	if (!filter || filter->isDestroying || !audio_data || audio_data->frames == 0) {
		return audio_data;
	}

	// メインスレッドでウィジェット更新を実行（非同期）
	if (filter->widget && !filter->widget.isNull()) {
		if (audio_data->data[0] && audio_data->data[1]) {
			const float *left = reinterpret_cast<const float *>(audio_data->data[0]);
			const float *right = reinterpret_cast<const float *>(audio_data->data[1]);

			// データをコピーしてメインスレッドに渡す
			QVector<float> leftData(left, left + audio_data->frames);
			QVector<float> rightData(right, right + audio_data->frames);
			QString sourceName = filter->sourceName;
			QPointer<PhaseMeterWidget> widget = filter->widget;

			QMetaObject::invokeMethod(
				QApplication::instance(),
				[widget, sourceName, leftData, rightData]() {
					if (widget && !widget.isNull()) {
						widget->updateAudioData(sourceName, leftData.constData(),
									rightData.constData(), leftData.size());
					}
				},
				Qt::QueuedConnection);
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

// クリーンアップ関数
static void cleanup_all_filters()
{
	QMutexLocker locker(&filterMutex);
	for (auto *filter : activeFilters) {
		if (filter) {
			filter->isDestroying = true;
		}
	}
	activeFilters.clear();
}

bool obs_module_load(void)
{
	// フィルタを登録
	obs_register_source(&phase_meter_filter_info);

	// メインウィンドウが準備できたらドックを作成
	QTimer::singleShot(1000, []() {
		QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (mainWindow && phaseMeterDock.isNull()) {
			phaseMeterDock = new PhaseMeterDock(mainWindow);

			// ドックをメインウィンドウに追加
			mainWindow->addDockWidget(Qt::RightDockWidgetArea, phaseMeterDock);

			// メニューにアクションを追加
			QAction *action = new QAction("Phase Meter", mainWindow);
			action->setCheckable(true);
			action->setChecked(true);

			QObject::connect(action, &QAction::toggled, [](bool checked) {
				if (phaseMeterDock && !phaseMeterDock.isNull()) {
					phaseMeterDock->setVisible(checked);
				}
			});

			QObject::connect(phaseMeterDock.data(), &QDockWidget::visibilityChanged,
					 [action](bool visible) {
						 if (action) {
							 action->setChecked(visible);
						 }
					 });

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
	});

	return true;
}

void obs_module_unload(void)
{
	// すべてのフィルタをクリーンアップ
	cleanup_all_filters();

	// ドックの削除
	if (phaseMeterDock && !phaseMeterDock.isNull()) {
		phaseMeterDock->deleteLater();
		phaseMeterDock = nullptr;
	}

	// Qtイベントループを少し回す
	if (QApplication::instance()) {
		QApplication::processEvents();
	}
}