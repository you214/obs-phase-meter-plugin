#pragma once

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <memory>
#include <future>
#include <QImage>

class AudioSource {
public:
	QString name;
	QColor color;
	std::vector<float> leftChannel;
	std::vector<float> rightChannel;
	bool enabled;
	mutable QMutex dataMutex; // データ保護用

	AudioSource(const QString &n, const QColor &c) : name(n), color(c), enabled(true) {}
};

class PhaseMeterWidget : public QWidget {
	Q_OBJECT

public:
	explicit PhaseMeterWidget(QWidget *parent = nullptr);
	~PhaseMeterWidget() override;

	void addAudioSource(const QString &name, const QColor &color = Qt::green);
	void removeAudioSource(const QString &name);
	void updateAudioData(const QString &sourceName, const float *left, const float *right, size_t frames);
	void refreshAudioSources();                   // 音声ソース一覧を更新
	QStringList getAvailableAudioSources() const; // 利用可能な音声ソース一覧を取得

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private slots:
	void onSourceSelectionChanged();
	void onColorButtonClicked();
	void updateDisplay();
	void onColorSelected(const QColor &color);

private:
	void setupUI();
	void drawPhaseMeter(QPainter &painter, const QRect &rect);
	void drawAudioSource(QPainter &painter, const QPoint &center, int radius, const AudioSource &source);
	void cleanup();

	QVBoxLayout *m_mainLayout;
	QHBoxLayout *m_controlLayout;
	QComboBox *m_sourceCombo;
	QPushButton *m_colorButton;
	QLabel *m_correlationLabel;

	std::vector<std::unique_ptr<AudioSource>> m_audioSources;
	QTimer *m_updateTimer;
	mutable QMutex m_sourcesMutex; // オーディオソース保護用
	bool m_isDestroying;
	bool m_needsUpdate;

	QColorDialog *m_colorDialog;

	// Phase meter specific
	static constexpr int PHASE_METER_SIZE = 200;
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int BUFFER_SIZE = 1024;

	// 追加の構造体とメンバー
private:
	struct RenderData {
		QString name;
		QColor color;
		std::vector<float> leftChannel;
		std::vector<float> rightChannel;

		RenderData(const AudioSource &source)
			: name(source.name),
			  color(source.color),
			  leftChannel(source.leftChannel),
			  rightChannel(source.rightChannel)
		{
		}
	};

	struct ProcessedAudioData {
		QColor color;
		float correlation;
		std::vector<QPoint> points;
	};

	bool m_isProcessing;

	// 非同期処理用メソッド
	void drawGrid(QPainter &painter, const QRect &rect);
	void drawAudioDataAsync(QPainter &painter, const QRect &rect);
	std::vector<std::future<ProcessedAudioData>>
	processAudioSourcesParallel(const std::vector<RenderData> &renderData, const QPoint &center, int radius);
	ProcessedAudioData processAudioSourceData(const RenderData &data, const QPoint &center, int radius);
	float calculateCorrelationParallel(const std::vector<float> &left, const std::vector<float> &right,
					   size_t sampleCount);
	float calculateCorrelationSequential(const std::vector<float> &left, const std::vector<float> &right,
					     size_t sampleCount);
	std::vector<QPoint> calculatePhasePointsParallel(const std::vector<float> &left,
							 const std::vector<float> &right, const QPoint &center,
							 int radius, size_t sampleCount);
	void drawProcessedAudioSource(QPainter &painter, const ProcessedAudioData &data);
	void updateCorrelationDisplay(float correlation);
};