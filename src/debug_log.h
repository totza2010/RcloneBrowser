#pragma once

// Core (L0): the application's own debug log.
//
// Until now the only way to see what the application itself was thinking was
// to add a qDebug(), rebuild, and take it out again -- which is how a whole
// afternoon went: the first probe printed to a console a GUI build does not
// have, the second wrote to a file that a failed link meant nothing ever
// opened. A switch that is always there and costs nothing when it is off is
// worth more than a better probe.
//
// This is not the job log. That records what rclone said about one transfer
// (job_log.h); this records what the application did.

#include <QLoggingCategory>
#include <QString>

// The parts that have something worth saying. Silent unless turned on, so
// adding a line to one of these costs nothing in a normal run.
Q_DECLARE_LOGGING_CATEGORY(rbQueue)
Q_DECLARE_LOGGING_CATEGORY(rbJob)
Q_DECLARE_LOGGING_CATEGORY(rbDb)
Q_DECLARE_LOGGING_CATEGORY(rbApp)

namespace DebugLog {

// Starts capturing, if the user asked for it. Call once, early in main(),
// before anything that might have something to say.
//
// Turned on by the setting, or by RB_DEBUG=1 in the environment for a single
// run that leaves the setting alone -- which is what a support request wants:
// no clicking through a dialog to reproduce something once.
void install();

bool isEnabled();

// Where this run is being written, empty when it is off.
QString filePath();

// The directory the files go in: alongside the job logs, because somebody
// collecting evidence wants both.
QString logDir();

// Turns it on or off for the next run. The current run is not disturbed,
// because a message handler swapped mid-run loses whatever was in flight.
void setEnabled(bool on);

// Deletes debug logs older than the job-log retention. Called at startup.
int purgeOldLogs();

} // namespace DebugLog
