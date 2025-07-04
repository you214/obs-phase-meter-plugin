/*
Audoo-phase-meter for OBS
Copyright (C) 2025 you214 https://github.com/you214

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

// 修正前
#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>

// 修正後 - Qt系を先にinclude
#include <QMainWindow>
#include <QAction>
#include <QMenuBar>
#include <QApplication>
#include <QTimer>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QTime>
#include <QThread>

#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>

#include "phase-meter-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-phase-meter", "en-US")

// グローバル変数
static QPointer<PhaseMeterDock> phaseMeterDock = nullptr;
static bool moduleUnloading = false;
static bool audioMonitoringActive = false;

static QTimer *updateTimer = nullptr;
static QMutex pendingDataMutex;
static QHash<QString, QPair<QVector<float>, QVector<float>>> pendingAudioData;

// 音声データを監視するコールバック
static void audio_capture_callback(void *data, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	if (moduleUnloading || !data || !source || !audio_data || muted) {
		return;
	}

	const char *sourceName = obs_source_get_name(source);
	if (sourceName) {
		blog(LOG_INFO, "audio_capture_callback called for source: %s", sourceName);
	}

	if (audio_data->data[0] && audio_data->data[1] && audio_data->frames > 0) {
		if (sourceName) {
			QMutexLocker locker(&pendingDataMutex);
			QString sourceNameQt = QString::fromUtf8(sourceName);

			const float *left = reinterpret_cast<const float *>(audio_data->data[0]);
			const float *right = reinterpret_cast<const float *>(audio_data->data[1]);

			QVector<float> leftData(left, left + audio_data->frames);
			QVector<float> rightData(right, right + audio_data->frames);

			pendingAudioData[sourceNameQt] = qMakePair(leftData, rightData);
			blog(LOG_INFO, "Audio data added for source: %s, frames: %d", sourceName, audio_data->frames);
		}
	}
}

// OBSのすべての音声ソースを取得してPhase Meterに追加
static bool add_audio_source_enum(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);

	if (!source || !widget) {
		return true;
	}

	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_AUDIO) {
		const char *name = obs_source_get_name(source);
		if (name) {
			QString sourceName = QString::fromUtf8(name);
			// ランダムな色を生成
			QRandomGenerator *rand = QRandomGenerator::global();
			QColor color = QColor::fromHsv(rand->bounded(360), 255, 255);
			widget->addAudioSource(sourceName, color);
		}
	}

	return true;
}

// 音声監視のコールバックを追加
static bool add_monitoring_callback(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO) {
		obs_source_add_audio_capture_callback(source, audio_capture_callback, widget);
	}
	return true;
}

// 音声監視のコールバックを削除
static bool remove_monitoring_callback(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO) {
		obs_source_remove_audio_capture_callback(source, audio_capture_callback, widget);
	}
	return true;
}

// 音声監視の開始
static void start_audio_monitoring()
{
	if (!phaseMeterDock || phaseMeterDock.isNull() || audioMonitoringActive) {
		return;
	}

	PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
	if (!widget) {
		return;
	}

	obs_enum_sources(add_monitoring_callback, widget);
	audioMonitoringActive = true;

	blog(LOG_INFO, "Phase Meter: Audio monitoring started");
}

// 音声監視の停止
static void stop_audio_monitoring()
{
	if (!audioMonitoringActive) {
		return;
	}

	PhaseMeterWidget *widget = nullptr;
	if (phaseMeterDock && !phaseMeterDock.isNull()) {
		widget = phaseMeterDock->getPhaseMeterWidget();
	}

	obs_enum_sources(remove_monitoring_callback, widget);
	audioMonitoringActive = false;

	blog(LOG_INFO, "Phase Meter: Audio monitoring stopped");
}

// ソースが作成された時のハンドラ
static void source_create_handler(void *data, calldata_t *calldata)
{
	(void)data; // 未使用パラメータを明示的にマーク

	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(calldata, "source"));
	if (!source || moduleUnloading) {
		return;
	}

	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_AUDIO) {
		if (phaseMeterDock && !phaseMeterDock.isNull()) {
			PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
			if (widget) {
				const char *name = obs_source_get_name(source);
				if (name) {
					QString sourceName = QString::fromUtf8(name);
					QRandomGenerator *rand = QRandomGenerator::global();
					QColor color = QColor::fromHsv(rand->bounded(360), 255, 255);
					widget->addAudioSource(sourceName, color);

					// 新しいソースに監視コールバックを追加
					if (audioMonitoringActive) {
						obs_source_add_audio_capture_callback(source, audio_capture_callback,
										      widget);
					}
				}
			}
		}
	}
}

// ソースが削除された時のハンドラ
static void source_destroy_handler(void *data, calldata_t *calldata)
{
	(void)data; // 未使用パラメータを明示的にマーク

	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(calldata, "source"));
	if (!source || moduleUnloading) {
		return;
	}

	if (phaseMeterDock && !phaseMeterDock.isNull()) {
		PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
		if (widget) {
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				widget->removeAudioSource(sourceName);

				// 監視コールバックを削除
				if (audioMonitoringActive) {
					obs_source_remove_audio_capture_callback(source, audio_capture_callback,
										 widget);
				}
			}
		}
	}
}

// メニューアクションのセットアップ
static void setupMenuAction(QMainWindow *mainWindow)
{
	QAction *action = new QAction("Phase Meter", mainWindow);
	action->setCheckable(true);
	action->setChecked(true);

	QObject::connect(action, &QAction::toggled, [](bool checked) {
		if (phaseMeterDock && !phaseMeterDock.isNull()) {
			phaseMeterDock->setVisible(checked);
		}
	});

	QObject::connect(phaseMeterDock.data(), &QDockWidget::visibilityChanged, [action](bool visible) {
		if (action) {
			action->setChecked(visible);
		}
	});

	// View > Docks メニューを探して追加
	QMenuBar *menuBar = mainWindow->menuBar();
	if (menuBar) {
		QMenu *viewMenu = nullptr;
		QMenu *docksMenu = nullptr;

		for (QMenu *menu : menuBar->findChildren<QMenu *>()) {
			QString title = menu->title().toLower();
			if (title.contains("view") || title.contains("表示")) {
				viewMenu = menu;
				for (QMenu *submenu : menu->findChildren<QMenu *>()) {
					QString subTitle = submenu->title().toLower();
					if (subTitle.contains("dock") || subTitle.contains("ドック")) {
						docksMenu = submenu;
						break;
					}
				}
				break;
			}
		}

		if (docksMenu) {
			docksMenu->addAction(action);
		} else if (viewMenu) {
			viewMenu->addAction(action);
		} else {
			// フォールバック: 最初のメニューに追加
			QList<QMenu *> menus = menuBar->findChildren<QMenu *>();
			if (!menus.isEmpty()) {
				menus.first()->addAction(action);
			}
		}
	}
}

// Phase Meterドックの作成
static void createPhaseMeterDock()
{
	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow || !phaseMeterDock.isNull()) {
		return;
	}

	phaseMeterDock = new PhaseMeterDock(mainWindow);
	mainWindow->addDockWidget(Qt::RightDockWidgetArea, phaseMeterDock);

	// メニューアクションの設定
	setupMenuAction(mainWindow);

	// 音声ソースを列挙して追加
	PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
	if (widget) {
		obs_enum_sources(add_audio_source_enum, widget);

		updateTimer = new QTimer();
		updateTimer->setInterval(33); // 30fps
		QObject::connect(updateTimer, &QTimer::timeout, []() {
			if (phaseMeterDock && !phaseMeterDock.isNull()) {
				PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
				if (widget) {
					QMutexLocker locker(&pendingDataMutex);
					if (!pendingAudioData.isEmpty()) {
						for (auto it = pendingAudioData.begin(); it != pendingAudioData.end();
						     ++it) {
							const QString &sourceName = it.key();
							const QVector<float> &leftData = it.value().first;
							const QVector<float> &rightData = it.value().second;

							widget->updateAudioData(sourceName, leftData.data(),
										rightData.data(), leftData.size());
						}
						pendingAudioData.clear();
					}
				}
			}
		});
		updateTimer->start();

		start_audio_monitoring();
	}

	blog(LOG_INFO, "Phase Meter: Dock created successfully");
}

// OBSのイベントハンドラ
static void obs_event_handler(enum obs_frontend_event event, void *data)
{
	(void)data; // 未使用パラメータを明示的にマーク

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// OBSの読み込み完了後に音声ソースを再列挙
		if (phaseMeterDock && !phaseMeterDock.isNull()) {
			PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
			if (widget) {
				widget->refreshAudioSources();
			}
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		moduleUnloading = true;
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "Phase Meter: Loading plugin...");

	// OBSイベントハンドラを登録
	obs_frontend_add_event_callback(obs_event_handler, nullptr);

	// ソース作成・削除のシグナルハンドラを登録
	signal_handler_t *core_signals = obs_get_signal_handler();
	signal_handler_connect(core_signals, "source_create", source_create_handler, nullptr);
	signal_handler_connect(core_signals, "source_destroy", source_destroy_handler, nullptr);

	// 短い遅延でドックを作成
	QTimer::singleShot(500, createPhaseMeterDock);

	blog(LOG_INFO, "Phase Meter: Plugin loaded successfully");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "Phase Meter: Unloading plugin...");

	moduleUnloading = true;

	// 音声監視を停止
	stop_audio_monitoring();

	if (updateTimer) {
		updateTimer->stop();
		delete updateTimer;
		updateTimer = nullptr;
	}

	// 少し待機してからリソースを解放
	if (QApplication::instance()) {
		QApplication::processEvents();
		QThread::msleep(100); // 100ms待機
	}

	// シグナルハンドラを切断
	signal_handler_t *core_signals = obs_get_signal_handler();
	signal_handler_disconnect(core_signals, "source_create", source_create_handler, nullptr);
	signal_handler_disconnect(core_signals, "source_destroy", source_destroy_handler, nullptr);

	// イベントハンドラを削除
	obs_frontend_remove_event_callback(obs_event_handler, nullptr);

	// ドックの削除
	if (phaseMeterDock && !phaseMeterDock.isNull()) {
		phaseMeterDock->hide();
		phaseMeterDock->deleteLater();
		phaseMeterDock = nullptr;
	}

	// イベントループを処理
	if (QApplication::instance()) {
		QApplication::processEvents();
		QApplication::processEvents();
	}

	blog(LOG_INFO, "Phase Meter: Plugin unloaded successfully");
}

void obs_module_post_load(void) {}
