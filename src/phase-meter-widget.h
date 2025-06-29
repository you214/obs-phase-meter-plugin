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
#include <vector>
#include <memory>

class AudioSource {
public:
    QString name;
    QColor color;
    std::vector<float> leftChannel;
    std::vector<float> rightChannel;
    bool enabled;
    
    AudioSource(const QString& n, const QColor& c) : name(n), color(c), enabled(true) {}
};

class PhaseMeterWidget : public QWidget {
	Q_OBJECT

public:
	explicit PhaseMeterWidget(QWidget *parent = nullptr);
	~PhaseMeterWidget();

	void addAudioSource(const QString &name, const QColor &color = Qt::green);
	void removeAudioSource(const QString &name);
	void updateAudioData(const QString &sourceName, const float *left, const float *right, size_t frames);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void onSourceSelectionChanged();
	void onColorButtonClicked();
	void updateDisplay();

private:
	void setupUI();
	void drawPhaseMeter(QPainter &painter, const QRect &rect);
	void drawCorrelationMeter(QPainter &painter, const QRect &rect);
	void drawAudioSource(QPainter &painter, const QPoint &center, int radius, const AudioSource &source);


	QVBoxLayout *m_mainLayout;
	QHBoxLayout *m_controlLayout;
	QComboBox *m_sourceCombo;
	QPushButton *m_colorButton;
	QLabel *m_correlationLabel;

	std::vector<std::unique_ptr<AudioSource>> m_audioSources;
	QTimer *m_updateTimer;

	// Phase meter specific
	static constexpr int PHASE_METER_SIZE = 200;
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int BUFFER_SIZE = 1024;
};