#pragma once

#include "pch.h"
#include "ui_job_widget.h"

class JobWidget : public QWidget {
  Q_OBJECT

public:
  JobWidget(QProcess *process, const QString &info, const QStringList &args,
            const QString &source, const QString &dest, const QString &uniqueID,
            const QString &transferMode, const QString &requestId,
            QWidget *parent = nullptr);
  ~JobWidget();

  void showDetails();
  bool isRunning = true;
  QDateTime getStartDateTime();
  QString getStatus();

public slots:
  void cancel();
  QString getUniqueID();
  QString getRequestId();
  QString getTransferMode();

signals:
  void finished(const QString &info, const QString &jobFinalStatus);
  void closed();

private:
  Ui::JobWidget ui;

  QProcess *mProcess;
  int mLines = 0;

  QStringList mArgs;
  QHash<QString, QLabel *> mActive;
  QSet<QLabel *> mUpdated;

  QString mUniqueID = "";
  QString mTransferMode = "";
  QString mRequestId = "";
  QString mJobFinalStatus = "";

  // 0 - running, 1 - finished, 2 - error
  QString mStatus = "0_transfer_running";

  QDateTime mStartDateTime = QDateTime::currentDateTime();
  void updateStartInfo();
  void updateFinishInfo(const QString &ETA = "");
  qint64 parseETAtoSeconds(const QString &eta) {
    QRegularExpression re("(\\d+)([ywdhms])");
    QRegularExpressionMatchIterator i = re.globalMatch(eta);

    qint64 totalSeconds = 0;
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        qint64 value = match.captured(1).toLongLong();
        QString unit = match.captured(2);

        if (unit == "y") totalSeconds += value * 365 * 24 * 3600;  // ปี → วินาที
        if (unit == "w") totalSeconds += value * 7 * 24 * 3600;    // สัปดาห์
        if (unit == "d") totalSeconds += value * 24 * 3600;        // วัน
        if (unit == "h") totalSeconds += value * 3600;            // ชั่วโมง
        if (unit == "m") totalSeconds += value * 60;              // นาที
        if (unit == "s") totalSeconds += value;                   // วินาที
    }
    return totalSeconds;
  }
};
