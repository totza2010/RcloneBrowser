#include "script_runner.h"
#include "app_settings.h"
#include "debug_log.h"
#include "job_queue.h"
#include "job_registry.h"
#include "utils.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

ScriptRunner &ScriptRunner::instance() {
  static ScriptRunner runner;
  return runner;
}

ScriptRunner::ScriptRunner(QObject *parent) : QObject(parent) {}

QString ScriptRunner::reasonName(Reason reason) {
  switch (reason) {
  case Reason::QueueEmpty:
    return QStringLiteral("queue-empty");
  case Reason::TransferStarted:
    return QStringLiteral("transfer-started");
  case Reason::TransferFinished:
    return QStringLiteral("transfer-finished");
  case Reason::LastTransferFinished:
    return QStringLiteral("last-transfer-finished");
  }
  return QStringLiteral("?");
}

QString ScriptRunner::scriptFor(Reason reason) {
  switch (reason) {
  case Reason::QueueEmpty:
    return AppSettings::queueFinishedScript();
  case Reason::TransferStarted:
    return AppSettings::transferStartedScript();
  case Reason::TransferFinished:
  case Reason::LastTransferFinished:
    // One setting, two moments. Which one fires is decided where the job
    // ends, not here.
    return AppSettings::transferFinishedScript();
  }
  return QString();
}

void ScriptRunner::install() {
  if (mInstalled) {
    return;
  }
  mInstalled = true;

  QObject::connect(&JobQueue::instance(), &JobQueue::emptied, this,
                   [this]() { run(Reason::QueueEmpty); });

  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobStarted, this,
                   [this](RunningJob *job) {
                     if (job->kind() == JobKind::Transfer) {
                       run(Reason::TransferStarted);
                     }
                   });

  QObject::connect(&JobRegistry::instance(), &JobRegistry::jobFinished, this,
                   [this](RunningJob *job) {
                     if (job->kind() != JobKind::Transfer) {
                       return;
                     }
                     // Which of the two rules is in force is the user's
                     // choice. Upstream ran it after every transfer while
                     // labelling it "Last transfer job ends" -- two different
                     // promises -- so rather than pick one and take the other
                     // away, both are offered and the log says which fired.
                     // See VERIFY.md V-24 block 1.
                     if (AppSettings::runFinishedScriptForEveryTransfer()) {
                       run(Reason::TransferFinished);
                     } else if (JobRegistry::instance().runningCount(
                                    JobKind::Transfer) == 0) {
                       run(Reason::LastTransferFinished);
                     }
                   });

  qCDebug(rbScript) << "listening";
}

bool ScriptRunner::run(Reason reason, bool waitForIt) {
  const QString command = scriptFor(reason).trimmed();
  if (command.isEmpty()) {
    // Nothing configured, or the switch is off. Silent: this is the usual
    // case and a line every time a transfer starts would drown the log.
    return false;
  }

  // The same splitting every options field in the program uses, so a script
  // line and an rclone options line quote the same way.
  const QStringList parts = SplitRcloneOptions(command);
  if (parts.isEmpty()) {
    qCDebug(rbScript) << "refused reason=" << reasonName(reason)
                      << "because the command line is empty";
    return false;
  }

  const QString program = parts.first();
  const QStringList arguments = parts.mid(1);

  // Checked rather than left to fail silently. A script that was moved or
  // renamed is the most likely way for this to stop working, and until now
  // the only sign was that nothing happened.
  if (!QFileInfo::exists(program)) {
    qCDebug(rbScript) << "refused reason=" << reasonName(reason)
                      << "because there is no such file:" << program;
    return false;
  }

  auto *process = new QProcess(this);
  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this, process, reason](int code, QProcess::ExitStatus status) {
        qCDebug(rbScript) << "ended reason=" << reasonName(reason)
                          << "exit=" << code << "crashed="
                          << (status == QProcess::CrashExit);
        emit ended(reason, code);
        process->deleteLater();
      });

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [this, reason](QProcess::ProcessError error) {
                     qCDebug(rbScript) << "failed reason="
                                       << reasonName(reason)
                                       << "error=" << int(error);
                   });

  qCDebug(rbScript) << "running reason=" << reasonName(reason)
                    << "command=" << program << "args=" << arguments.size();

  process->start(program, arguments, QIODevice::ReadOnly);
  emit started(reason, command);

  if (waitForIt) {
    // Generous, because these are the user's own scripts and some of them
    // verify a backup. Long enough not to cut work short; short enough that
    // a script waiting for input cannot hang a scheduled run for ever.
    constexpr int kWaitMs = 10 * 60 * 1000;
    if (!process->waitForFinished(kWaitMs)) {
      qCDebug(rbScript) << "gave up waiting reason=" << reasonName(reason)
                        << "after" << kWaitMs / 1000 << "s";
      process->kill();
      process->waitForFinished(5000);
    }
  }
  return true;
}
