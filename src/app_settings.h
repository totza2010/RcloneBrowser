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

// The same for the scheduler as a whole: whether it is switched on. Unlike
// the queue this has always defaulted to on, which is why the default is
// stated here rather than left to whoever asks.
bool schedulerIsRunning();
void setSchedulerIsRunning(bool running);

// A script to run when the queue empties, if the user asked for one. Returns
// an empty string when there is nothing to run -- including when a path is
// set but the feature is switched off, so callers have one thing to check
// instead of two.
QString queueFinishedScript();

// The same for a transfer starting, and for one finishing.
QString transferStartedScript();
QString transferFinishedScript();

// Whether the finished script runs after every transfer or only once nothing
// is transferring any more.
//
// Upstream ran it after every one while labelling it "Last transfer job
// ends", which is two different promises. Rather than pick one and take the
// other away, both are offered and the label says what it does. "Last" is
// the default because it is what the label always said. See VERIFY.md V-24.
bool runFinishedScriptForEveryTransfer();

// Whether the application writes its own debug log. Off by default: it says
// a great deal and most runs have nothing to explain.
//
// This is the setting the checkbox in Preferences writes. RB_DEBUG=1 in the
// environment still forces it on for one run without touching the setting,
// which is what a support request wants -- but wanting a log should not
// require knowing that.
bool debugLog();
void setDebugLog(bool on);

// How large one log file may grow before it is rotated, and how many of the
// older ones are kept. Per file, and there is one file per subsystem plus
// the combined one, so the worst case is roughly
// (subsystems + 1) x keep x size.
//
// In kilobytes rather than megabytes because a megabyte is the wrong unit at
// the bottom of the range: checking that rotation works at all meant writing
// a megabyte of log first, which takes long enough that nobody does it. The
// floor is low enough to watch a file roll over in a minute.
int logMaxFileKb();
int logKeepFiles();

// The defaults, in one place. Exposed so a test can say what it expects
// rather than repeating the number.
namespace Default {
constexpr bool kLogToFile = true;
constexpr int kLogRetentionDays = 7;
constexpr int kHistoryRetentionDays = 90;
constexpr int kHistoryMaxRuns = 2000;
constexpr bool kDebugLog = false;
constexpr int kLogMaxFileKb = 5 * 1024;
constexpr int kLogMinFileKb = 16;
constexpr int kLogKeepFiles = 10;
} // namespace Default

} // namespace AppSettings
