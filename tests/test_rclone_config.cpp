#include "rclone_config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

// What a remote is made of, as rclone describes it -- and above all, what
// of it may be shown.
//
// See docs/REMOTE-CONFIG-UI.md (R1, R2) and VERIFY.md V-45.
class TestRcloneConfig : public QObject {
  Q_OBJECT

private:
  static QByteArray providers() {
    return R"([
      {"Name":"teldrive","Description":"Tel Drive","Prefix":"teldrive","Options":[
        {"Name":"access_token","Help":"Access Token Cookie","Type":"string","DefaultStr":"",
         "Required":false,"IsPassword":false,"Sensitive":true,"Advanced":false,"Hide":0},
        {"Name":"chunk_size","Help":"Chunk Size\nLonger.","Type":"SizeSuffix","Default":536870912,
         "DefaultStr":"512Mi","Required":false,"IsPassword":false,"Sensitive":false,"Advanced":false,
         "Hide":0,"Examples":[{"Value":"1Gi","Help":"bigger"}]},
        {"Name":"upload_concurrency","Help":"Upload Concurrency","Type":"int","Default":4,
         "DefaultStr":"4","Advanced":true}
      ]},
      {"Name":"sftp","Description":"SSH/SFTP","Prefix":"sftp","Options":[
        {"Name":"host","Help":"SSH host","Type":"string","Required":true},
        {"Name":"pass","Help":"SSH password","Type":"string","IsPassword":true}
      ]},
      {"Description":"no name, so not a backend"}
    ])";
  }

private slots:
  void everyBackendIsReadWithItsOptions() {
    const QList<Provider> list = ParseProviders(providers());
    QCOMPARE(list.size(), 2);
    const Provider *teldrive = FindProvider(list, QStringLiteral("teldrive"));
    QVERIFY(teldrive != nullptr);
    QCOMPARE(teldrive->description, QStringLiteral("Tel Drive"));
    QCOMPARE(teldrive->options.size(), 3);
    QVERIFY(teldrive->options.at(0).sensitive);
    QCOMPARE(teldrive->options.at(1).defaultValue, QStringLiteral("512Mi"));
    QCOMPARE(teldrive->options.at(1).examples.size(), 1);
    QVERIFY(teldrive->options.at(2).advanced);
    QVERIFY(FindProvider(list, QStringLiteral("sftp"))->options.at(1).isPassword);
    QVERIFY(FindProvider(list, QStringLiteral("nothing")) == nullptr);
  }

  // The rule that matters: nothing secret leaves, whichever way it is
  // marked or not marked.
  void aSecretIsSaidToBeSetAndNeverShown() {
    const QList<Provider> list = ParseProviders(providers());
    QJsonObject section;
    section.insert(QStringLiteral("type"), QStringLiteral("teldrive"));
    section.insert(QStringLiteral("access_token"), QStringLiteral("SECRET-1"));
    section.insert(QStringLiteral("chunk_size"), QStringLiteral("1Gi"));
    section.insert(QStringLiteral("client_secret"), QStringLiteral("SECRET-2"));
    section.insert(QStringLiteral("region"), QStringLiteral("home"));

    const QJsonArray settings =
        DescribeSettings(section, FindProvider(list, QStringLiteral("teldrive")));
    const QString all = QString::fromUtf8(QJsonDocument(settings).toJson());
    QVERIFY2(!all.contains(QStringLiteral("SECRET")), qPrintable(all));
    QVERIFY(!all.contains(QStringLiteral("\"type\":\"teldrive\"")));

    // In the backend's order, the undescribed after.
    QCOMPARE(settings.size(), 4);
    QCOMPARE(settings.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("access_token"));
    QCOMPARE(settings.at(0).toObject().value(QStringLiteral("secret")).toBool(), true);
    QVERIFY(!settings.at(0).toObject().contains(QStringLiteral("value")));
    QCOMPARE(settings.at(1).toObject().value(QStringLiteral("value")).toString(),
             QStringLiteral("1Gi"));
    QCOMPARE(settings.at(1).toObject().value(QStringLiteral("help")).toString(),
             QStringLiteral("Chunk Size")); // the first line only
    for (const QJsonValue &value : settings) {
      const QJsonObject s = value.toObject();
      if (s.value(QStringLiteral("name")).toString() == QLatin1String("client_secret")) {
        // Not described by the backend: judged by its name.
        QVERIFY(s.value(QStringLiteral("secret")).toBool());
        QVERIFY(s.value(QStringLiteral("unknown")).toBool());
      }
      if (s.value(QStringLiteral("name")).toString() == QLatin1String("region")) {
        QCOMPARE(s.value(QStringLiteral("value")).toString(), QStringLiteral("home"));
      }
    }
  }

  void aRemoteNameIsHeldToRclonesRule() {
    for (const char *good : {"gdrive", "films drive", "a.b-c_d+e@f", "ไดรฟ์"}) {
      QVERIFY2(WhyNotARemoteName(QString::fromUtf8(good)).isEmpty(), good);
    }
    for (const char *bad : {"", "-x", " x", "x ", "a:b", "a/b", "a\\b"}) {
      QVERIFY2(!WhyNotARemoteName(QString::fromUtf8(bad)).isEmpty(), bad);
    }
  }

  // With no description at all -- rclone not asked yet, or a kind it does
  // not describe -- nothing is shown. A name was not enough: a teldrive
  // host, which rclone marks sensitive, went out under "api_host".
  void withoutTheBackendNothingIsShown() {
    QJsonObject section;
    section.insert(QStringLiteral("type"), QStringLiteral("teldrive"));
    section.insert(QStringLiteral("token"), QStringLiteral("{\"access_token\":\"X\"}"));
    section.insert(QStringLiteral("api_host"), QStringLiteral("https://private.example"));
    section.insert(QStringLiteral("chunk_size"), QStringLiteral("512M"));
    const QJsonArray settings = DescribeSettings(section, nullptr);
    QCOMPARE(settings.size(), 3);
    for (const QJsonValue &value : settings) {
      QVERIFY(value.toObject().value(QStringLiteral("secret")).toBool());
      QVERIFY(!value.toObject().contains(QStringLiteral("value")));
    }
    const QString all = QString::fromUtf8(QJsonDocument(settings).toJson());
    QVERIFY(!all.contains(QStringLiteral("private.example")));
    QVERIFY(!all.contains(QStringLiteral("512M")));
    QVERIFY(LooksSecret(QStringLiteral("client_secret")));
    QVERIFY(LooksSecret(QStringLiteral("service_account_credentials")));
    QVERIFY(!LooksSecret(QStringLiteral("chunk_size")));
  }
};

QTEST_APPLESS_MAIN(TestRcloneConfig)
#include "test_rclone_config.moc"
