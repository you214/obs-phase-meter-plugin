#pragma once

#include <QDockWidget>
#include "phase-meter-widget.h"

class PhaseMeterDock : public QDockWidget {
    Q_OBJECT

public:
    explicit PhaseMeterDock(QWidget* parent = nullptr);
    ~PhaseMeterDock();
    
    PhaseMeterWidget* getPhaseMeterWidget() const { return m_phaseMeterWidget; }

private:
    PhaseMeterWidget* m_phaseMeterWidget;
};