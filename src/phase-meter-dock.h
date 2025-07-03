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
#pragma once

#include <QDockWidget>
#include "phase-meter-widget.h"

class PhaseMeterDock : public QDockWidget {
	Q_OBJECT

public:
	explicit PhaseMeterDock(QWidget *parent = nullptr);
	~PhaseMeterDock();

	PhaseMeterWidget *getPhaseMeterWidget() const { return m_phaseMeterWidget; }

private:
	PhaseMeterWidget *m_phaseMeterWidget;
};