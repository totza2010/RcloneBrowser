#include "app_settings.h"
#include "script_runner.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// The scripts a user asks to have run at certain moments. These used to be
// started by MainWindow, so "run my check when the queue empties" quietly
// meant "...as long as a window is open" -- and when it did not happen,
// nothing said so.
//
// See docs/LAYER-SPLIT.md block 1 and VERIFY.md V-24.
class TestScriptRunner : public QObject {
  Q_OBJECT

private:
  using Reason = ScriptRunner::Reason;

  static QString appDir() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QCoreApplication::applicationDirPath();
#else
    return QDir(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")))
        .filePath("rclone-browser");
#endif
  }

  static QString iniPath() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QDir(appDir()).filePath(
        QFileInfo(QCoreApplication::applicationFilePath()).baseName() + ".ini");
#else
    return QDir(appDir()).filePath("rclone-browser.ini");
#endif
  }

  // Something that exists, starts, and stops straight away on any machine
  // the tests run on.
  static QString harmlessProgram() {
#if defined(Q_OS_WIN)
    return qEnvironmentVariable("COMSPEC",
                                QStringLiteral("C:/Windows/System32/cmd.exe"));
#else
    return QStringLiteral("/bin/sh");
#endif
  }

  static QString harmlessCommand() {
#if defined(Q_OS_WIN)
    return QStringLiteral("\"%1\" /c exit 0").arg(harmlessProgram());
#else
    return QStringLiteral("\"%1\" -c exit").arg(harmlessProgram());
#endif
  }

  static void configure(const QString &switchKey, const QString &pathKey,
                        bool on, const QString &command) {
    auto settings = GetSettings();
    settings->setValue(switchKey, on);
    settings->setValue(pathKey, command);
    settings->sync();
  }

  std::unique_ptr<QTemporaryDir> mScratch;

private slots:
  void initTestCase() {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    mScratch.reset(new QTemporaryDir);
    QVERIFY(mScratch->isValid());
    qputenv("XDG_CONFIG_HOME", mScratch->path().toLocal8Bit());
    QVERIFY(QDir().mkpath(appDir()));
#endif
    QFile ini(iniPath());
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.close();
    QVERIFY2(IsPortableMode(), "portable mode did not take effect");

    QVERIFY2(QFileInfo::exists(harmlessProgram()),
             qPrintable(harmlessProgram()));
  }

  void cleanupTestCase() { QFile::remove(iniPath()); }

  void init() {
    auto settings = GetSettings();
    for (const char *key :
         {"Settings/queueScriptRun", "Settings/jobStartScriptRun",
          "Settings/jobLastFinishedScriptRun"}) {
      settings->setValue(QLatin1String(key), false);
    }
    for (const char *key :
         {"Settings/queueScript", "Settings/transferOnScript",
          "Settings/transferOffScript"}) {
      settings->setValue(QLatin1String(key), QString());
    }
    settings->sync();
  }

  // Each moment reads its own pair of settings. Getting these crossed would
  // run the wrong script at the right time, which is worse than running none.
  void eachMomentReadsItsOwnSetting() {
    configure(QStringLiteral("Settings/queueScriptRun"),
              QStringLiteral("Settings/queueScript"), true,
              QStringLiteral("queue-one"));
    configure(QStringLiteral("Settings/jobStartScriptRun"),
              QStringLiteral("Settings/transferOnScript"), true,
              QStringLiteral("start-one"));
    configure(QStringLiteral("Settings/jobLastFinishedScriptRun"),
              QStringLiteral("Settings/transferOffScript"), true,
              QStringLiteral("finish-one"));

    QCOMPARE(ScriptRunner::scriptFor(Reason::QueueEmpty),
             QStringLiteral("queue-one"));
    QCOMPARE(ScriptRunner::scriptFor(Reason::TransferStarted),
             QStringLiteral("start-one"));
    QCOMPARE(ScriptRunner::scriptFor(Reason::LastTransferFinished),
             QStringLiteral("finish-one"));

    // The two finished reasons are one setting seen from two rules, not two
    // settings. Somebody who fills in the field once expects it to be the
    // script either way.
    QCOMPARE(ScriptRunner::scriptFor(Reason::TransferFinished),
             ScriptRunner::scriptFor(Reason::LastTransferFinished));
  }

  // Which rule is in force is the user's choice, and "last" is the default
  // because that is what the field has always been labelled.
  void theFinishedRuleDefaultsToTheLastTransferOnly() {
    GetSettings()->remove(QStringLiteral("Settings/jobFinishedScriptWhen"));
    GetSettings()->sync();
    QVERIFY(!AppSettings::runFinishedScriptForEveryTransfer());

    GetSettings()->setValue(QStringLiteral("Settings/jobFinishedScriptWhen"),
                            QStringLiteral("every"));
    GetSettings()->sync();
    QVERIFY(AppSettings::runFinishedScriptForEveryTransfer());

    // Anything that is not the word "every" means the default, rather than
    // a settings file with a typo quietly changing what runs.
    GetSettings()->setValue(QStringLiteral("Settings/jobFinishedScriptWhen"),
                            QStringLiteral("evry"));
    GetSettings()->sync();
    QVERIFY(!AppSettings::runFinishedScriptForEveryTransfer());

    GetSettings()->setValue(QStringLiteral("Settings/jobFinishedScriptWhen"),
                            QStringLiteral("last"));
    GetSettings()->sync();
  }

  // A path left behind after the feature is switched off must not run. The
  // switch and the path are two settings and every call site used to check
  // both by hand, which is two chances to check only one.
  void aPathLeftBehindWithTheSwitchOffDoesNotRun() {
    configure(QStringLiteral("Settings/queueScriptRun"),
              QStringLiteral("Settings/queueScript"), false,
              harmlessCommand());

    QVERIFY(ScriptRunner::scriptFor(Reason::QueueEmpty).isEmpty());
    QVERIFY(!ScriptRunner::instance().run(Reason::QueueEmpty));
  }

  void theSwitchOnWithNoPathDoesNothing() {
    configure(QStringLiteral("Settings/queueScriptRun"),
              QStringLiteral("Settings/queueScript"), true, QString());

    QVERIFY(!ScriptRunner::instance().run(Reason::QueueEmpty));
  }

  // The most likely way for this to stop working is the script being moved
  // or renamed, and until now the only sign was that nothing happened.
  void aProgramThatIsNotThereIsRefused() {
    configure(QStringLiteral("Settings/queueScriptRun"),
              QStringLiteral("Settings/queueScript"), true,
              QDir(appDir()).filePath(QStringLiteral("no-such-script-here")));

    QSignalSpy started(&ScriptRunner::instance(), &ScriptRunner::started);
    QVERIFY2(!ScriptRunner::instance().run(Reason::QueueEmpty),
             "a missing program was reported as started");
    QCOMPARE(started.count(), 0);
  }

  // And the whole point: it actually starts something, and says how it ended.
  void aRealProgramRunsAndSaysHowItEnded() {
    configure(QStringLiteral("Settings/jobStartScriptRun"),
              QStringLiteral("Settings/transferOnScript"), true,
              harmlessCommand());

    QSignalSpy started(&ScriptRunner::instance(), &ScriptRunner::started);
    QSignalSpy ended(&ScriptRunner::instance(), &ScriptRunner::ended);

    QVERIFY(ScriptRunner::instance().run(Reason::TransferStarted));
    QCOMPARE(started.count(), 1);

    QVERIFY2(ended.wait(15000), "the script never reported finishing");
    QCOMPARE(ended.count(), 1);
    QCOMPARE(ended.first().at(1).toInt(), 0);
  }

  // A program whose path has spaces in it has to be kept whole. The settings
  // field has always taken quotes for this, and that is not something to
  // change underneath somebody's existing configuration.
  void aQuotedPathWithSpacesIsKeptWhole() {
    QVERIFY2(harmlessCommand().contains(QLatin1Char('"')),
             "the fixture stopped exercising quoting");

    configure(QStringLiteral("Settings/queueScriptRun"),
              QStringLiteral("Settings/queueScript"), true, harmlessCommand());

    QSignalSpy ended(&ScriptRunner::instance(), &ScriptRunner::ended);
    QVERIFY2(ScriptRunner::instance().run(Reason::QueueEmpty),
             "the quoted program was not found");
    QVERIFY(ended.wait(15000));
  }

  void everyReasonHasANameForTheLog() {
    QSet<QString> seen;
    for (Reason reason :
         {Reason::QueueEmpty, Reason::TransferStarted,
          Reason::TransferFinished, Reason::LastTransferFinished}) {
      const QString name = ScriptRunner::reasonName(reason);
      QVERIFY(!name.isEmpty());
      QVERIFY2(name != QStringLiteral("?"), qPrintable(name));
      QVERIFY2(!seen.contains(name), qPrintable(name));
      seen.insert(name);
    }
  }
};

QTEST_MAIN(TestScriptRunner)
#include "test_script_runner.moc"
