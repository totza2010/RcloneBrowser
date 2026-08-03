#pragma once

// L3 (GUI): recursive QSettings <-> widget binding -- see docs/ARCHITECTURE.md.
// Split out of utils.h so that utils stays free of QtWidgets and can move into
// the headless core.

class QSettings;
class QObject;

void ReadSettings(QSettings *settings, QObject *widget);
void WriteSettings(QSettings *settings, QObject *widget);
