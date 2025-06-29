#include "phase-meter-dock.h"

PhaseMeterDock::PhaseMeterDock(QWidget* parent)
    : QDockWidget("Phase Meter", parent)
    , m_phaseMeterWidget(new PhaseMeterWidget(this))
{
    setWidget(m_phaseMeterWidget);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    setAllowedAreas(Qt::AllDockWidgetAreas);

    // 親が削除される時に適切にクリーンアップ
    setAttribute(Qt::WA_DeleteOnClose, false);
}

PhaseMeterDock::~PhaseMeterDock() = default;