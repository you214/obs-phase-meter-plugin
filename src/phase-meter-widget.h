#pragma once

#include <QWidget>
#include <QTimer>
#include <QMutex>
#include <QPainter>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <vector>
#include <memory>


class AudioSource {
public:
	QString name;
	QColor color;
	std::vector<float> leftChannel;
	std::vector<float> rightChannel;
	bool enabled;
	mutable QMutex dataMutex; // データ保護用（mutableを追加）

	AudioSource(const QString &n, const QColor &c) : name(n), color(c), enabled(true) {}

	// コピーコンストラクタとコピー代入演算子を削除（QMutexのため）
	AudioSource(const AudioSource &) = delete;
	AudioSource &operator=(const AudioSource &) = delete;

	// ムーブコンストラクタとムーブ代入演算子
	AudioSource(AudioSource &&other) noexcept
		: name(std::move(other.name)),
		  color(std::move(other.color)),
		  leftChannel(std::move(other.leftChannel)),
		  rightChannel(std::move(other.rightChannel)),
		  enabled(other.enabled)
	{
	}

	AudioSource &operator=(AudioSource &&other) noexcept
	{
		if (this != &other) {
			name = std::move(other.name);
			color = std::move(other.color);
			leftChannel = std::move(other.leftChannel);
			rightChannel = std::move(other.rightChannel);
			enabled = other.enabled;
		}
		return *this;
	}
};


class PhaseMeterWidget : public QWidget {
	Q_OBJECT

public:
	explicit PhaseMeterWidget(QWidget *parent = nullptr);
	~PhaseMeterWidget() override;

	void addAudioSource(const QString &name, const QColor &color = Qt::green);
	void removeAudioSource(const QString &name);
	void updateAudioData(const QString &sourceName, const float *left, const float *right, size_t frames);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private slots:
	void onSourceSelectionChanged();
	void onColorButtonClicked();
	void updateDisplay();

private:
	void setupUI();
	void drawPhaseMeter(QPainter &painter, const QRect &rect);
	void drawCorrelationMeter(QPainter &painter, const QRect &rect);
	void drawAudioSource(QPainter &painter, const QPoint &center, int radius, const AudioSource &source);
	void cleanup();

	QVBoxLayout *m_mainLayout;
	QHBoxLayout *m_controlLayout;
	QComboBox *m_sourceCombo;
	QPushButton *m_colorButton;
	QLabel *m_correlationLabel;

	std::vector<std::unique_ptr<AudioSource>> m_audioSources;
	QTimer *m_updateTimer;
	QMutex m_sourcesMutex; // オーディオソース保護用
	bool m_isDestroying;
    
	// Phase meter specific
	static constexpr int PHASE_METER_SIZE = 200;
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int BUFFER_SIZE = 1024;
};