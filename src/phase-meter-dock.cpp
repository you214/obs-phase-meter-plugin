#include "phase-meter-dock.h"

PhaseMeterDock::PhaseMeterDock(QWidget* parent)
    : QDockWidget("Phase Meter", parent)
    , m_phaseMeterWidget(new PhaseMeterWidget(this))
{
    setWidget(m_phaseMeterWidget);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    setAllowedAreas(Qt::AllDockWidgetAreas);
}

PhaseMeterDock::~PhaseMeterDock() = default;

// #include "phase-meter-dock.moc"