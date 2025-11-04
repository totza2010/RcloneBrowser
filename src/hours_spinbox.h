#pragma once
#include <QSpinBox>

class HoursSpinBox : public QSpinBox {
  Q_OBJECT

public:
  HoursSpinBox(QWidget *parent = Q_NULLPTR);

  ~HoursSpinBox();

public slots:
  int valueFromText(const QString &text) const;
  QString textFromValue(int value) const;
};
