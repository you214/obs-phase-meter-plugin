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
#include <QRandomGenerator>
#include <QTime>
#include "phase-meter-dock.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-phase-meter", "en-US")

// グローバル変数をスマートポインタで管理
static QPointer<PhaseMeterDock> phaseMeterDock = nullptr;
static QMutex filterMutex;
static QList<struct phase_meter_filter *> activeFilters;
static bool moduleUnloading = false;

// 音声データを取得するためのフィルタ
struct phase_meter_filter {
	obs_source_t *context;
	QPointer<PhaseMeterWidget> widget;
	QString sourceName;
	bool isDestroying;

	phase_meter_filter() : context(nullptr), isDestroying(false) {}
};


// OBSのすべての音声ソースを取得してPhase Meterに追加
static bool add_audio_source_enum(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);

	if (!source || !widget)
		return true;

	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_AUDIO) {
		const char *name = obs_source_get_name(source);
		if (name) {
			QString sourceName = QString::fromUtf8(name);
			// デスクトップ音声やマイクなどの音声ソースを追加
			QRandomGenerator *rand = QRandomGenerator::global();
			rand->seed(QTime::currentTime().msec());
			widget->addAudioSource(sourceName, QColor::fromHsv(rand->generate() % 360, 255, 255));
		}
	}

	return true;
}

// 音声データを監視するコールバック
static void audio_capture_callback(void *data, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	if (moduleUnloading || !data || !source || !audio_data || muted) {
		return;
	}

	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);
	const char *sourceName = obs_source_get_name(source);

	if (!sourceName || audio_data->frames == 0) {
		return;
	}

	// ステレオ音声データがあるかチェック
	if (audio_data->data[0] && audio_data->data[1]) {
		const float *left = reinterpret_cast<const float *>(audio_data->data[0]);
		const float *right = reinterpret_cast<const float *>(audio_data->data[1]);

		QString sourceNameQt = QString::fromUtf8(sourceName);
		widget->updateAudioData(sourceNameQt, left, right, audio_data->frames);
	}
}

// OBSのイベントハンドラ
static void obs_event_handler(enum obs_frontend_event event, void *data)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// OBSの読み込み完了後に音声ソースを列挙
		if (phaseMeterDock && !phaseMeterDock.isNull()) {
			PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
			if (widget) {
				obs_enum_sources(add_audio_source_enum, widget);
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

// ソースが作成・削除された時のハンドラ
static void source_create_handler(void *data, calldata_t *calldata)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(calldata, "source"));
	if (!source || moduleUnloading)
		return;

	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_AUDIO) {
		if (phaseMeterDock && !phaseMeterDock.isNull()) {
			PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
			if (widget) {
				const char *name = obs_source_get_name(source);
				if (name) {
					QString sourceName = QString::fromUtf8(name);
					QRandomGenerator *rand = QRandomGenerator::global();
					rand->seed(QTime::currentTime().msec());
					widget->addAudioSource(sourceName,
							       QColor::fromHsv(rand->generate() % 360, 255, 255));
				}
			}
		}
	}
}

static void source_destroy_handler(void *data, calldata_t *calldata)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(calldata, "source"));
	if (!source || moduleUnloading)
		return;

	if (phaseMeterDock && !phaseMeterDock.isNull()) {
		PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
		if (widget) {
			const char *name = obs_source_get_name(source);
			if (name) {
				QString sourceName = QString::fromUtf8(name);
				widget->removeAudioSource(sourceName);
			}
		}
	}
}


// start_audio_monitoringのためのコールバック
static bool add_monitoring_callback(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO) {
		obs_source_add_audio_capture_callback(source, audio_capture_callback, widget);
	}
	return true; // 列挙を続ける
}

// stop_audio_monitoringのためのコールバック
static bool remove_monitoring_callback(void *data, obs_source_t *source)
{
	PhaseMeterWidget *widget = static_cast<PhaseMeterWidget *>(data);
	uint32_t flags = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO) {
		// addした時と同じwidgetポインタを渡して、コールバックを正しく削除する
		obs_source_remove_audio_capture_callback(source, audio_capture_callback, widget);
	}
	return true; // 列挙を続ける
}

// 音声監視の開始
static void start_audio_monitoring()
{
	if (!phaseMeterDock || phaseMeterDock.isNull())
		return;

	PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
	if (!widget)
		return;

	// すべての音声ソースに対してコールバックを呼び出し、監視を開始する
	obs_enum_sources(add_monitoring_callback, widget);
}

// 音声監視の停止
static void stop_audio_monitoring()
{
	if (!phaseMeterDock || phaseMeterDock.isNull())
		return;

	PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
	if (!widget)
		return;

	// すべての音声ソースに対してコールバックを呼び出し、監視を停止する
	obs_enum_sources(remove_monitoring_callback, widget);
}


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
	// OBSイベントハンドラを登録
	obs_frontend_add_event_callback(obs_event_handler, nullptr);

	// ソース作成・削除のシグナルハンドラを登録
	signal_handler_t *core_signals = obs_get_signal_handler();
	signal_handler_connect(core_signals, "source_create", source_create_handler, nullptr);
	signal_handler_connect(core_signals, "source_destroy", source_destroy_handler, nullptr);

	// 少し遅延してからドックを作成（OBSの初期化完了を待つ）
	QTimer::singleShot(2000, []() {
		if (moduleUnloading)
			return;

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

			// ツールメニューまたはドックメニューに追加
			QMenuBar *menuBar = mainWindow->menuBar();
			if (menuBar) {
				QMenu *viewMenu = nullptr;
				QMenu *docksMenu = nullptr;

				// View > Docks メニューを探す
				for (QMenu *menu : menuBar->findChildren<QMenu *>()) {
					QString title = menu->title().toLower();
					if (title.contains("view") || title.contains("表示")) {
						viewMenu = menu;
						// Docksサブメニューを探す
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

			// 音声ソースを列挙して追加
			PhaseMeterWidget *widget = phaseMeterDock->getPhaseMeterWidget();
			if (widget) {
				obs_enum_sources(add_audio_source_enum, widget);

				// 音声監視を開始
				QTimer::singleShot(1000, start_audio_monitoring);
			}
		}
	});

	return true;
}

void obs_module_unload(void)
{
	moduleUnloading = true;

	// 音声監視を停止
	stop_audio_monitoring();

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
		QApplication::processEvents(); // 2回実行して確実に
	}
}