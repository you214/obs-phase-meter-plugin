#include "phase-meter-widget.h"
#include <QApplication>
#include <QColorDialog>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QMutexLocker>
#include <QThreadPool>
#include <QFuture>
#include <QtConcurrent>
#include <cmath>
#include <algorithm>
#include <execution>
#include <future>
#include <thread>
#include <numeric>

PhaseMeterWidget::PhaseMeterWidget(QWidget *parent)
	: QWidget(parent),
	  m_updateTimer(new QTimer(this)),
	  m_isDestroying(false),
	  m_needsUpdate(false),
	  m_isProcessing(false)
{
	setupUI();

	// 30FPSに変更（負荷軽減）
	m_updateTimer->setInterval(33); // 33ms = 30fps
	connect(m_updateTimer, &QTimer::timeout, this, &PhaseMeterWidget::updateDisplay);
	m_updateTimer->start();

	// 非同期処理用のスレッドプールを設定
	QThreadPool::globalInstance()->setMaxThreadCount(
		std::max(2, static_cast<int>(std::thread::hardware_concurrency() / 2)));
}

PhaseMeterWidget::~PhaseMeterWidget()
{
	cleanup();
}

void PhaseMeterWidget::setupUI()
{
	m_mainLayout = new QVBoxLayout(this);
	m_controlLayout = new QHBoxLayout();

	// ソース選択コンボボックス
	m_sourceCombo = new QComboBox();
	m_sourceCombo->addItem("All Sources");
	connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&PhaseMeterWidget::onSourceSelectionChanged);

	// 色選択ボタン
	m_colorButton = new QPushButton("Color");
	connect(m_colorButton, &QPushButton::clicked, this, &PhaseMeterWidget::onColorButtonClicked);

	// 相関値表示ラベル
	m_correlationLabel = new QLabel("Correlation: 0.00");

	m_controlLayout->addWidget(new QLabel("Source:"));
	m_controlLayout->addWidget(m_sourceCombo);
	m_controlLayout->addWidget(m_colorButton);
	m_controlLayout->addStretch();
	m_controlLayout->addWidget(m_correlationLabel);

	m_mainLayout->addLayout(m_controlLayout);
	m_mainLayout->addStretch();

	setMinimumSize(300, 350);
}

void PhaseMeterWidget::addAudioSource(const QString &name, const QColor &color)
{
	if (m_isDestroying)
		return;

	QMutexLocker locker(&m_sourcesMutex);

	// 既に存在するかチェック
	auto it = std::find_if(m_audioSources.begin(), m_audioSources.end(),
			       [&name](const auto &source) { return source->name == name; });

	if (it == m_audioSources.end()) {
		auto source = std::make_unique<AudioSource>(name, color);
		m_audioSources.push_back(std::move(source));

		// UIの更新はメインスレッドで実行
		QMetaObject::invokeMethod(
			this,
			[this, name]() {
				if (!m_isDestroying && m_sourceCombo) {
					m_sourceCombo->addItem(name);
				}
			},
			Qt::QueuedConnection);
	}
}

void PhaseMeterWidget::removeAudioSource(const QString &name)
{
	if (m_isDestroying)
		return;

	QMutexLocker locker(&m_sourcesMutex);

	auto it = std::find_if(m_audioSources.begin(), m_audioSources.end(),
			       [&name](const auto &source) { return source->name == name; });

	if (it != m_audioSources.end()) {
		m_audioSources.erase(it);

		// UIの更新はメインスレッドで実行
		QMetaObject::invokeMethod(
			this,
			[this, name]() {
				if (!m_isDestroying && m_sourceCombo) {
					for (int i = 1; i < m_sourceCombo->count(); ++i) {
						if (m_sourceCombo->itemText(i) == name) {
							m_sourceCombo->removeItem(i);
							break;
						}
					}
				}
			},
			Qt::QueuedConnection);
	}
}

void PhaseMeterWidget::updateAudioData(const QString &sourceName, const float *left, const float *right, size_t frames)
{
	if (m_isDestroying || !left || !right || frames == 0)
		return;

	// フレーム数を制限（メモリ使用量とCPU負荷を軽減）
	const size_t maxFrames = 1024;
	size_t actualFrames = std::min(frames, maxFrames);

	QMutexLocker locker(&m_sourcesMutex);

	auto it = std::find_if(m_audioSources.begin(), m_audioSources.end(),
			       [&sourceName](const auto &source) { return source->name == sourceName; });

	if (it != m_audioSources.end()) {
		auto &source = *it;
		QMutexLocker dataLocker(&source->dataMutex);

		// データをコピー（安全に、サイズ制限付き）
		try {
			source->leftChannel.assign(left, left + actualFrames);
			source->rightChannel.assign(right, right + actualFrames);
			m_needsUpdate = true; // 更新フラグを設定
		} catch (...) {
			// メモリエラーを無視
		}
	}
}

void PhaseMeterWidget::paintEvent(QPaintEvent *event)
{
	if (m_isDestroying)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	QRect meterRect = rect();
	if (m_controlLayout && m_controlLayout->geometry().isValid()) {
		meterRect.setTop(m_controlLayout->geometry().bottom() + 10);
	}
	meterRect.adjust(10, 10, -10, -10);

	if (meterRect.isValid()) {
		drawPhaseMeter(painter, meterRect);
	}
}

void PhaseMeterWidget::updateDisplay()
{
	// 更新が必要な場合のみ再描画
	if (!m_isDestroying && m_needsUpdate && !m_isProcessing) {
		m_needsUpdate = false;
		update();
	}
}

void PhaseMeterWidget::drawPhaseMeter(QPainter &painter, const QRect &rect)
{
	// 背景を描画
	painter.fillRect(rect, Qt::black);

	// グリッドを描画
	drawGrid(painter, rect);

	// 音声データを非同期で処理して描画
	drawAudioDataAsync(painter, rect);
}

void PhaseMeterWidget::drawGrid(QPainter &painter, const QRect &rect)
{
	painter.setPen(QPen(Qt::darkGray, 1));
	QPoint center = rect.center();
	int radius = std::min(rect.width(), rect.height()) / 2 - 20;

	// 円を描画
	painter.drawEllipse(center, radius, radius);

	// 十字線を描画
	painter.drawLine(center.x() - radius, center.y(), center.x() + radius, center.y());
	painter.drawLine(center.x(), center.y() - radius, center.x(), center.y() + radius);

	// 対角線を描画
	int diagonalOffset = static_cast<int>(radius * 0.707); // cos(45度)
	painter.drawLine(center.x() - diagonalOffset, center.y() - diagonalOffset, center.x() + diagonalOffset,
			 center.y() + diagonalOffset);
	painter.drawLine(center.x() - diagonalOffset, center.y() + diagonalOffset, center.x() + diagonalOffset,
			 center.y() - diagonalOffset);
}

void PhaseMeterWidget::drawAudioDataAsync(QPainter &painter, const QRect &rect)
{
	if (m_isProcessing)
		return;

	QPoint center = rect.center();
	int radius = std::min(rect.width(), rect.height()) / 2 - 20;
	int selectedIndex = m_sourceCombo->currentIndex();

	// 描画データを並列で準備
	std::vector<RenderData> renderData;

	{
		QMutexLocker locker(&m_sourcesMutex);

		if (selectedIndex == 0) { // All Sources
			// 全ソース表示時は最大3つまでに制限
			int count = 0;
			for (const auto &source : m_audioSources) {
				if (source->enabled && count < 3) {
					QMutexLocker dataLocker(&source->dataMutex);
					if (!source->leftChannel.empty() && !source->rightChannel.empty()) {
						renderData.emplace_back(*source);
						count++;
					}
				}
			}
		} else if (selectedIndex > 0 && selectedIndex - 1 < static_cast<int>(m_audioSources.size())) {
			const auto &source = m_audioSources[selectedIndex - 1];
			if (source->enabled) {
				QMutexLocker dataLocker(&source->dataMutex);
				if (!source->leftChannel.empty() && !source->rightChannel.empty()) {
					renderData.emplace_back(*source);
				}
			}
		}
	}

	// 並列処理で各ソースの描画データを計算
	if (!renderData.empty()) {
		auto futures = processAudioSourcesParallel(renderData, center, radius);

		// 結果を描画
		for (auto &future : futures) {
			if (future.valid()) {
				try {
					auto result = future.get();
					drawProcessedAudioSource(painter, result);
				} catch (...) {
					// エラーを無視
				}
			}
		}
	}
}

std::vector<std::future<PhaseMeterWidget::ProcessedAudioData>>
PhaseMeterWidget::processAudioSourcesParallel(const std::vector<RenderData> &renderData, const QPoint &center,
					      int radius)
{

	std::vector<std::future<ProcessedAudioData>> futures;

	// 各ソースを並列で処理
	for (const auto &data : renderData) {
		auto future = std::async(std::launch::async, [=, &data]() -> ProcessedAudioData {
			return processAudioSourceData(data, center, radius);
		});
		futures.push_back(std::move(future));
	}

	return futures;
}

PhaseMeterWidget::ProcessedAudioData PhaseMeterWidget::processAudioSourceData(const RenderData &data, const QPoint &center, int radius)
{

	ProcessedAudioData result;
	result.color = data.color;
	result.correlation = 0.0f;

	// サンプル数を制限
	const size_t maxSamples = 512;
	size_t sampleCount = std::min(data.leftChannel.size(), maxSamples);

	if (sampleCount == 0)
		return result;

	// 並列で相関値を計算
	result.correlation = calculateCorrelationParallel(data.leftChannel, data.rightChannel, sampleCount);

	// 並列でフェーズポイントを計算
	result.points = calculatePhasePointsParallel(data.leftChannel, data.rightChannel, center, radius, sampleCount);

	return result;
}

float PhaseMeterWidget::calculateCorrelationParallel(const std::vector<float> &left, const std::vector<float> &right,
						     size_t sampleCount)
{

	if (sampleCount < 4) {
		// サンプル数が少ない場合は逐次処理
		return calculateCorrelationSequential(left, right, sampleCount);
	}

	// 並列でドット積と二乗和を計算
	const size_t numThreads = std::min(sampleCount / 128, static_cast<size_t>(std::thread::hardware_concurrency()));
	const size_t chunkSize = sampleCount / numThreads;

	std::vector<std::future<std::tuple<float, float, float>>> futures;

	for (size_t i = 0; i < numThreads; ++i) {
		size_t start = i * chunkSize;
		size_t end = (i == numThreads - 1) ? sampleCount : (i + 1) * chunkSize;

		auto future = std::async(
			std::launch::async, [&left, &right, start, end]() -> std::tuple<float, float, float> {
				float dotProduct = 0.0f;
				float leftSum = 0.0f;
				float rightSum = 0.0f;

				// C++20の並列アルゴリズムを使用
				auto leftBegin = left.begin() + start;
				auto leftEnd = left.begin() + end;
				auto rightBegin = right.begin() + start;

				// 内積を計算
				dotProduct = std::transform_reduce(std::execution::unseq, leftBegin, leftEnd,
								   rightBegin, 0.0f, std::plus<float>(),
								   std::multiplies<float>());

				// 左チャンネルの二乗和
				leftSum = std::transform_reduce(std::execution::unseq, leftBegin, leftEnd, 0.0f,
								std::plus<float>(), [](float x) { return x * x; });

				// 右チャンネルの二乗和
				rightSum = std::transform_reduce(std::execution::unseq, rightBegin, right.begin() + end,
								 0.0f, std::plus<float>(),
								 [](float x) { return x * x; });

				return std::make_tuple(dotProduct, leftSum, rightSum);
			});

		futures.push_back(std::move(future));
	}

	// 結果を集約
	float totalDotProduct = 0.0f;
	float totalLeftSum = 0.0f;
	float totalRightSum = 0.0f;

	for (auto &future : futures) {
		if (future.valid()) {
			auto [dotProduct, leftSum, rightSum] = future.get();
			totalDotProduct += dotProduct;
			totalLeftSum += leftSum;
			totalRightSum += rightSum;
		}
	}

	if (totalLeftSum > 0 && totalRightSum > 0) {
		return totalDotProduct / std::sqrt(totalLeftSum * totalRightSum);
	}

	return 0.0f;
}

float PhaseMeterWidget::calculateCorrelationSequential(const std::vector<float> &left, const std::vector<float> &right,
						       size_t sampleCount)
{

	float correlation = 0.0f;
	float leftSum = 0.0f;
	float rightSum = 0.0f;

	for (size_t i = 0; i < sampleCount; ++i) {
		correlation += left[i] * right[i];
		leftSum += left[i] * left[i];
		rightSum += right[i] * right[i];
	}

	if (leftSum > 0 && rightSum > 0) {
		correlation /= std::sqrt(leftSum * rightSum);
	}

	return correlation;
}

std::vector<QPoint> PhaseMeterWidget::calculatePhasePointsParallel(const std::vector<float> &left,
								   const std::vector<float> &right,
								   const QPoint &center, int radius, size_t sampleCount)
{

	const int maxPoints = 50;
	const int step = std::max(1, static_cast<int>(sampleCount / maxPoints));

	std::vector<QPoint> points;
	points.reserve(maxPoints);

	// 並列でポイントを計算
	std::vector<size_t> indices;
	for (size_t i = 0; i < sampleCount; i += step) {
		indices.push_back(i);
	}

	std::mutex pointsMutex;

	std::for_each(std::execution::par_unseq, indices.begin(), indices.end(), [&](size_t i) {
		float leftVal = left[i];
		float rightVal = right[i];

		float magnitude = std::sqrt(leftVal * leftVal + rightVal * rightVal);

		if (magnitude > 0.01f) {
			magnitude = std::min(magnitude, 1.0f);
			float angle = std::atan2(rightVal, leftVal);

			int x = center.x() + static_cast<int>(magnitude * radius * std::cos(angle));
			int y = center.y() + static_cast<int>(magnitude * radius * std::sin(angle));

			std::lock_guard<std::mutex> lock(pointsMutex);
			points.emplace_back(x, y);
		}
	});

	return points;
}

void PhaseMeterWidget::drawProcessedAudioSource(QPainter &painter, const ProcessedAudioData &data)
{
	painter.setPen(QPen(data.color, 2));

	// 点を描画
	for (const auto &point : data.points) {
		painter.drawEllipse(point, 1, 1);
	}

	// 相関値を更新（頻度制限付き）
	updateCorrelationDisplay(data.correlation);
}

void PhaseMeterWidget::updateCorrelationDisplay(float correlation)
{
	static int updateCounter = 0;
	if (++updateCounter % 10 == 0) { // 10回に1回だけ更新
		QMetaObject::invokeMethod(
			this,
			[this, correlation]() {
				if (!m_isDestroying && m_correlationLabel) {
					m_correlationLabel->setText(
						QString("Correlation: %1").arg(correlation, 0, 'f', 2));
				}
			},
			Qt::QueuedConnection);
	}
}

void PhaseMeterWidget::onSourceSelectionChanged()
{
	if (!m_isDestroying) {
		m_needsUpdate = true;
	}
}

void PhaseMeterWidget::onColorButtonClicked()
{
	if (m_isDestroying)
		return;

	int selectedIndex = m_sourceCombo->currentIndex();
	if (selectedIndex > 0) {
		QMutexLocker locker(&m_sourcesMutex);
		if (selectedIndex - 1 < static_cast<int>(m_audioSources.size())) {
			auto &source = m_audioSources[selectedIndex - 1];
			QColor newColor = QColorDialog::getColor(source->color, this, "Select Color");
			if (newColor.isValid()) {
				source->color = newColor;
				m_needsUpdate = true;
			}
		}
	}
}

void PhaseMeterWidget::cleanup()
{
	m_isDestroying = true;

	if (m_updateTimer) {
		m_updateTimer->stop();
	}

	// 進行中の非同期処理を待機
	QThreadPool::globalInstance()->waitForDone(1000);

	QMutexLocker locker(&m_sourcesMutex);
	m_audioSources.clear();
}

void PhaseMeterWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	m_needsUpdate = true;
}

void PhaseMeterWidget::closeEvent(QCloseEvent *event)
{
	cleanup();
	QWidget::closeEvent(event);
}

void PhaseMeterWidget::refreshAudioSources()
{
	if (m_isDestroying)
		return;

	// コンボボックスをクリア（"All Sources"以外）
	while (m_sourceCombo->count() > 1) {
		m_sourceCombo->removeItem(1);
	}

	// 現在の音声ソースを再追加
	QMutexLocker locker(&m_sourcesMutex);
	for (const auto &source : m_audioSources) {
		m_sourceCombo->addItem(source->name);
	}
}

QStringList PhaseMeterWidget::getAvailableAudioSources() const
{
	QStringList sources;
	QMutexLocker locker(&m_sourcesMutex);

	for (const auto &source : m_audioSources) {
		sources.append(source->name);
	}

	return sources;
}