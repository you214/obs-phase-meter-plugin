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

PhaseMeterDock::~PhaseMeterDock() {
	if (m_phaseMeterWidget) {
		m_phaseMeterWidget->deleteLater();
		m_phaseMeterWidget = nullptr;
	}
}