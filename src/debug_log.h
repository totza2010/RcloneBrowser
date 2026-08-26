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
//
// A folder per subject, under logs/:
//
//   logs/all/all.txt              everything, in the order it happened
//   logs/queue/queue.txt          rb.queue
//   logs/scheduler/scheduler.txt  rb.sched
//   logs/jobs/jobs.txt            rb.job -- what the program did about a job
//   logs/database/database.txt    rb.db
//   logs/app/app.txt              rb.app, and anything else, Qt's own included
//   logs/transfers/               what rclone said, one file per run (job_log.h)
//
// Folders rather than one flat directory because each of these keeps its
// rotations beside it: six subjects at ten generations apiece is seventy
// files to read past before finding the one that matters.
//
// Both the combined file and the split ones, on purpose. Splitting alone
// would have made the queue and scheduler faults harder to find rather than
// easier: what settled them was following one request id from rb.sched to
// rb.queue to rb.job, and that story only exists where the lines are in one
// order. The split files are for reading a subsystem on its own.
//
// Each grows to a set size and is then rotated -- queue.txt becomes
// queue.0.txt, queue.0.txt becomes queue.1.txt, and so on. The name of the
// current file never changes, which is the point: "the queue log" is always
// queue.txt rather than a file named after the moment a run happened to
// start, which you would have to go looking for.

#include <QLoggingCategory>
#include <QString>

// The parts that have something worth saying. Silent unless turned on, so
// adding a line to one of these costs nothing in a normal run.
Q_DECLARE_LOGGING_CATEGORY(rbQueue)
Q_DECLARE_LOGGING_CATEGORY(rbSched)
Q_DECLARE_LOGGING_CATEGORY(rbScript)
Q_DECLARE_LOGGING_CATEGORY(rbJob)
Q_DECLARE_LOGGING_CATEGORY(rbDb)
Q_DECLARE_LOGGING_CATEGORY(rbApp)

// How to read the queue lines, because two of the numbers are measured at a
// moment that is easy to guess wrong:
//
//   enqueue   count      the list *after* this one was added
//   starting  waiting    the list *before* this one starts, so it does not
//                        count the entry being started
//   job ended count      the list *before* the entry is taken out, so
//                        "count=3" means three were queued when it ended,
//                        not three left afterwards
//   clear     kept/of    how many stayed (the running one) out of how many
//
// A purge and two removals leave the same count behind; the difference is
// that removals say so, one line each.

// The scheduler lines answer a different question, and it is worth saying
// which. A schedule that never runs used to look the same in a log whether
// it was not yet due, refused, or simply not being checked at all -- three
// causes, one silence. So the check itself speaks:
//
//   check       once a minute per schedule, whether or not anything happens.
//               Its absence is the finding: no check line means the timer is
//               not running, which is not the same as "nothing was due".
//   due         the minute arrived. Always followed by either fired or held.
//   fired       a run was actually asked for, with the request id.
//   held        due, but not started, and why (the whole reason for the line)
//   missed      the minute went by unchecked -- sleep or hibernation -- and
//               the next run was moved on rather than run late.
//   status      what the schedule was told about its run. "ignored" means the
//               request id did not match the one it is waiting for, which is
//               how a schedule gets stuck showing a run that has since ended.
//
// A run started here appears again under rb.queue with the same request id:
// that is what makes "the schedule asked" and "the queue started it" one
// story rather than two.
//
// TEST: (V-23) tick Preferences -> Misc. -> Diagnostics and start the
// program normally, with no RB_DEBUG set: the files must appear, split by
// subsystem, and rotate rather than growing without limit.
//
// TEST: (V-22) run with RB_DEBUG=1 and let a schedule come due. Every
// schedule must log "check" once a minute whether or not anything happens,
// and a minute that comes due must be followed by either "fired" or "held".

namespace DebugLog {

// Starts capturing, if the user asked for it. Call once, early in main(),
// before anything that might have something to say.
//
// Turned on by the setting in Preferences, or by RB_DEBUG=1 in the
// environment for a single run that leaves the setting alone -- which is
// what a support request wants. The setting is the ordinary way in: tick the
// box and every run from then on writes a log, with no special command and
// nothing to remember to undo.
void install();

bool isEnabled();

// Where the combined file is, empty when logging is off.
QString filePath();

// The directory the files go in: alongside the job logs, because somebody
// collecting evidence wants both.
QString logDir();

// Turns capturing on or off now, without touching the setting. Use this for
// a temporary trace; setEnabledForNextRun() is what a preference writes.
void setEnabled(bool on);

// Writes the setting and applies it immediately, so that ticking the box and
// reproducing the problem is one step rather than two.
void setEnabledForNextRun(bool on);

// Deletes rotated logs older than the job-log retention. The file each
// subsystem is currently writing is never touched however old it looks.
// Called at startup.
int purgeOldLogs();

} // namespace DebugLog
