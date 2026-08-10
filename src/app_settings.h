#pragma once

// Core (L1): the settings the rest of the application actually depends on.
//
// Every one of these used to be read where it was needed, spelled out as a
// string with its default written beside it. The same key was read in several
// places with different defaults, and a value that made no sense -- a
// negative retention, an empty rclone path -- was each caller's problem to
// notice. See docs/API.md S6.
//
// This is not a wrapper around every key in the file. It covers what has to
// be right for the application to work, so that the default and the rule for
// what counts as a usable value exist once. Anything purely about how the
// window looks stays where it is drawn.

#include <QString>

namespace AppSettings {

// Where rclone is, and which config file it should read. Empty when the user
// has not chosen one, which is a state the application starts in.
QString rclonePath();
void setRclonePath(const QString &path);

QString rcloneConfPath();
void setRcloneConfPath(const QString &path);

// Whether each job writes a log file, and for how many days those are kept.
// Zero days means keep everything; a negative number is meaningless and falls
// back to the default rather than deleting today's logs.
bool logToFile();
int logRetentionDays();

// The same two questions for the run history, which is kept by count as well
// as by age -- a machine that runs a task every ten minutes fills a year's
// worth of rows long before a year has passed.
int historyRetentionDays();
int historyMaxRuns();

// Whether the queue was left running. Remembered so that a machine that
// restarts overnight picks the queue back up.
bool queueIsRunning();
void setQueueIsRunning(bool running);

// A script to run when the queue empties, if the user asked for one. Returns
// an empty string when there is nothing to run -- including when a path is
// set but the feature is switched off, so callers have one thing to check
// instead of two.
QString queueFinishedScript();

// The defaults, in one place. Exposed so a test can say what it expects
// rather than repeating the number.
namespace Default {
constexpr bool kLogToFile = true;
constexpr int kLogRetentionDays = 7;
constexpr int kHistoryRetentionDays = 90;
constexpr int kHistoryMaxRuns = 2000;
} // namespace Default

} // namespace AppSettings
