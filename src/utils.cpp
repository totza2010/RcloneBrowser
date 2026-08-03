#include "utils.h"

// Core (L0): this file must not depend on QtWidgets --
// see docs/ARCHITECTURE.md. Widget-bound settings helpers live in
// widget_settings.cpp.

static QString gRclone;
static QString gRcloneConf;
static QString gRclonePassword;

// Software versions comparison
// source: https://helloacm.com/how-to-compare-version-numbers-in-c/
std::vector<std::string> split(const std::string &s, char d) {
  std::vector<std::string> r;
  int j = 0;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (s[i] == d) {
      r.push_back(s.substr(j, i - j));
      j = i + 1;
    }
  }
  r.push_back(s.substr(j));
  return r;
}

unsigned int compareVersion(std::string version1, std::string version2) {
  auto v1 = split(version1, '.');
  auto v2 = split(version2, '.');
  unsigned int max = std::max(v1.size(), v2.size());

  while (v1.size() < max) v1.push_back("0");
  while (v2.size() < max) v2.push_back("0");

  for (unsigned int i = 0; i < max; i++) {
    try {
      unsigned int n1 = std::stoi(v1[i].empty() ? "0" : v1[i]);
      unsigned int n2 = std::stoi(v2[i].empty() ? "0" : v2[i]);
      if (n1 > n2) return 1;
      if (n1 < n2) return 2;
    } catch (const std::invalid_argument &) {
      // fallback default
      return 0;
    }
  }
  return 0;
}

static QString GetIniFilename() {
#ifdef Q_OS_MACOS
  QFileInfo applicationPath = qApp->applicationFilePath();
  //  qDebug() << QString(applicationPath.absolutePath());
  // on macOS excecutable file is located in
  // ./rclone-browser.app/Contents/MasOS/ to get actual bundle folder we have to
  // traverse three levels up
  QFileInfo MacOSPath = applicationPath.dir().path();
  QFileInfo ContentsPath = MacOSPath.dir().path();
  QFileInfo appBundlePath = ContentsPath.dir().path();
  //  qDebug() << QString("utils.cpp appBundle.absolutePath: " +
  //                      appBundlePath.absolutePath());
  //  qDebug() << QString(
  //      "utils.cpp ini file:" +
  //      appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini"));
  return appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini");
#else
#ifdef Q_OS_WIN
  QFileInfo applicationPath(qApp->applicationFilePath());
  return applicationPath.dir().filePath(applicationPath.baseName() + ".ini");
#else
  QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
  return xdg_config_home + "/rclone-browser/rclone-browser.ini";
#endif
#endif
}

bool IsPortableMode() {
  QString ini = GetIniFilename();
  QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
  //  qDebug() << QString("utils.cpp $XDG_CONFIG_HOME: " + xdg_config_home);
  QString appimage = qgetenv("APPIMAGE");
  //  qDebug() << QString("utils.cpp $APPIMAGE: " + appimage);

  // cat ".config" from $XDG_CONFIG_HOME
  // it should be the same as appimage if run from AppImage
  xdg_config_home = xdg_config_home.left(xdg_config_home.length() - 7);
  //  qDebug() << QString("utils.cpp $XDG_CONFIG_HOME-7: " + xdg_config_home);

  if (!xdg_config_home.isEmpty() && !appimage.isEmpty() &&
      xdg_config_home == appimage) {

    return true;
  }

  if (QFileInfo(ini).exists(ini)) {

    return true;
  } else {
    return false;
  }

  //  return QFileInfo(ini).exists();
}

std::unique_ptr<QSettings> GetSettings() {
  if (IsPortableMode()) {
    return std::unique_ptr<QSettings>(
        new QSettings(GetIniFilename(), QSettings::IniFormat));
  }
  return std::unique_ptr<QSettings>(new QSettings);
}

QStringList GetRcloneConf() {
  if (gRcloneConf.isEmpty()) {
    return QStringList();
  }

  QString conf = gRcloneConf;
  if (IsPortableMode() && QFileInfo(conf).isRelative()) {
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/rclone-browser to get actual bundle
    // folder we have to traverse three levels up
    conf = QDir(qApp->applicationDirPath() + "/../../..").filePath(conf);
#else
#ifdef Q_OS_WIN
    conf = QDir(qApp->applicationDirPath()).filePath(conf);
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    conf = QDir(xdg_config_home + "/..").filePath(conf);
#endif
#endif
    //    qDebug() << QString("utils.cpp conf: " + conf);
  }
  return QStringList() << "--config" << conf;
}

void SetRcloneConf(const QString &rcloneConf) { gRcloneConf = rcloneConf; }

QString GetRclone() {
  QString rclone = gRclone;
  if (IsPortableMode() && QFileInfo(rclone).isRelative()) {
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/rclone-browser to get actual bundle
    // folder we have to traverse three levels up
    rclone = QDir(qApp->applicationDirPath() + "/../../..").filePath(rclone);
#else
#ifdef Q_OS_WIN
    rclone = QDir(qApp->applicationDirPath()).filePath(rclone);
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    rclone = QDir(xdg_config_home + "/..").filePath(rclone);
#endif
#endif
    //    qDebug() << QString("utils.cpp rclone portable: " + rclone);
  }

  return rclone;
}

void SetRclone(const QString &rclone) { gRclone = rclone.trimmed(); }

void UseRclonePassword(QProcess *process) {
  if (!gRclonePassword.isEmpty()) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RCLONE_CONFIG_PASS", gRclonePassword);
    process->setProcessEnvironment(env);
  }
}

void SetRclonePassword(const QString &rclonePassword) {
  gRclonePassword = rclonePassword;
}

QStringList GetRemoteModeRcloneOptions() {
  auto settings = GetSettings();
  QString googleDriveMode =
      settings->value("Settings/remoteMode", "main").toString();

  QStringList driveSharedOption;

  if (googleDriveMode == "shared") {
    driveSharedOption << "--drive-shared-with-me";
  }
  if (googleDriveMode == "trash") {
    driveSharedOption << "--drive-trashed-only";
  }
  return driveSharedOption;
}

QStringList GetDefaultOptionsList(const QString &settingsOptions) {
  auto settings = GetSettings();
  QString defaultOptions =
      settings->value("Settings/" + settingsOptions).toString();
  //      settings->value("Settings/defaultRcloneOptions").toString();
  QStringList defaultOptionsList;

  if (!defaultOptions.isEmpty()) {
    QRegularExpression re(R"( (?=[^"]*("[^"]*"[^"]*)*$))");

    for (QString arg : defaultOptions.split(re)) {
      if (!arg.isEmpty()) {
        defaultOptionsList << arg.replace("\"", "");
      }
    }
  }

  return defaultOptionsList;
}

QStringList GetShowHidden() {
  auto settings = GetSettings();
  bool showHidden = settings->value("Settings/showHidden", true).toBool();
  QStringList showHiddenOption;
  if (!showHidden) {
    showHiddenOption << "--exclude"
                     << ".*/**"
                     << "--exclude"
                     << ".*";
  }
  return showHiddenOption;
}

QDir GetConfigDir() {

  QDir outputDir;

  if (IsPortableMode()) {
    // in portable mode tasks' file will be saved in the same folder as
    // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
    outputDir = QDir(qApp->applicationDirPath());
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

  } else {
    // get data location folder from Qt  - OS dependend
    outputDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  }

  // if (!outputDir.exists()) {
  //   outputDir.mkpath(".");
  // }
  // QString filePath = outputDir.absoluteFilePath(persistenceFileName);
  // QFile *file = new QFile(filePath);

  // if (!file->open(mode)) {
  //   qDebug() << QString("Could not open ") << file->fileName();
  //   delete file;
  //   file = nullptr;
  // }
  return outputDir;
}

// build rclone cmd string (used for info, e.g. to show rclone cmd in transfer
// dialog)
QStringList GetRcloneCmd(const QStringList &args) {

  QStringList rcloneTransferCmd;

  QString rcloneCmd = (QDir::toNativeSeparators(GetRclone()));
  QStringList rcloneConf = GetRcloneConf();

  QStringList rcloneOptions = args;

  // rclone executable
  if (!rcloneCmd.isEmpty()) {
    if (rcloneCmd.contains(" ")) {
      rcloneTransferCmd << "\"" + rcloneCmd + "\"";
    } else {
      rcloneTransferCmd << rcloneCmd;
    }
  }

  // rclone config
  if (!rcloneConf.isEmpty()) {
    // --config
    rcloneTransferCmd << rcloneConf.at(0);
    // file location
    if (rcloneConf.at(1).contains(" ")) {
      rcloneTransferCmd << "\"" + QDir::toNativeSeparators(rcloneConf.at(1)) +
                               "\"";
    } else {
      rcloneTransferCmd << QDir::toNativeSeparators(rcloneConf.at(1));
    }
  }

  if (!rcloneOptions.isEmpty() && rcloneOptions.count() > 1) {
    // copy/move/sync
    rcloneTransferCmd << rcloneOptions.takeAt(0);

    // source and destination
    if (rcloneOptions.at(0).contains(" ")) {
      rcloneTransferCmd << "\"" + rcloneOptions.takeAt(0) + "\"";
    } else {
      rcloneTransferCmd << rcloneOptions.takeAt(0);
    }

    if (rcloneOptions.at(0).contains(" ")) {
      rcloneTransferCmd << "\"" + rcloneOptions.takeAt(0) + "\"";
    } else {
      rcloneTransferCmd << rcloneOptions.takeAt(0);
    }
  }

  // rclone remaining options
  for (int j = 0; j < rcloneOptions.count(); ++j) {
    if (rcloneOptions.at(j).contains(" ")) {
      rcloneTransferCmd << "\"" + rcloneOptions.at(j) + "\"";
    } else {
      rcloneTransferCmd << rcloneOptions.at(j);
    }
  }

  return rcloneTransferCmd;
}

// TEST: (V-08) mount remote ที่ตั้ง RC port -> ตรวจว่า mount/unmount/mount script
// ยังทำงานครบ และ command line ของ process rclone ไม่มี --rc-user/--rc-pass แล้ว
QString GenerateRcCredential(int length) {
  static const QString alphabet(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

  QString out;
  out.reserve(length);
  for (int i = 0; i < length; ++i) {
    // bounded() is uniform; the previous "generate() % length" was biased
    // towards the first characters of the alphabet.
    out.append(alphabet.at(QRandomGenerator::global()->bounded(
        static_cast<int>(alphabet.length()))));
  }
  return out;
}

void UseRcCredentials(QProcess *process, const QString &user,
                      const QString &pass) {
  if (user.isEmpty() || pass.isEmpty()) {
    return;
  }
  QProcessEnvironment env = process->processEnvironment();
  if (env.isEmpty()) {
    env = QProcessEnvironment::systemEnvironment();
  }
  env.insert("RCLONE_RC_USER", user);
  env.insert("RCLONE_RC_PASS", pass);
  process->setProcessEnvironment(env);
}

// TEST: (V-03) ใส่ "--drive-token=SECRET123" ในช่อง rclone options ของ transfer
// dialog -> hover ปุ่ม output และกด copy ต้องเห็น "--drive-token=***" ทั้งสองทาง
QStringList RedactArgs(const QStringList &args) {
  // Matches "--rc-pass=secret", "--drive-token=...", "--sftp-key-pem=..." etc.
  // Only the part up to and including '=' is kept.
  static const QRegularExpression prefix(
      R"(^(--[a-z0-9-]*(pass|passw|password|token|secret|key|auth|user)[a-z0-9-]*)=)",
      QRegularExpression::CaseInsensitiveOption);

  QStringList out;
  out.reserve(args.size());
  for (const QString &arg : args) {
    QRegularExpressionMatch m = prefix.match(arg);
    if (m.hasMatch()) {
      out << m.captured(1) + "=***";
    } else {
      out << arg;
    }
  }
  return out;
}
