#pragma once

// Precompiled header for the rbcore library.
//
// Deliberately narrower than pch.h: no QtGui, no QtWidgets. Anything that
// compiles against this header can also compile into a headless build, which
// is the whole point of keeping rbcore separate (docs/ARCHITECTURE.md).

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <QtCore>
#include <QtDebug>
#include <QtNetwork>

#ifdef _MSC_VER
#pragma warning pop
#endif
