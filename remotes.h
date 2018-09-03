#ifndef REMOTES_H
#define REMOTES_H

#include <QDialog>
#include <QComboBox>

class ManageRemotes : public QDialog
{
    Q_OBJECT
public:
    explicit ManageRemotes(const QStringList &items);
    bool isChanged();

signals:

public slots:
    void removeItem();
    void addItem();

private:
    bool changed;
    QComboBox *box;
    QLineEdit *edit;
};

#endif // REMOTES_H
