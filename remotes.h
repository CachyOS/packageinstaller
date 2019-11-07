#ifndef REMOTES_H
#define REMOTES_H

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>

#include <cmd.h>

class ManageRemotes : public QDialog
{
    Q_OBJECT
public:
    explicit ManageRemotes(QWidget *parent = nullptr);
    bool isChanged() const;
    void listFlatpakRemotes() const;
    QString getInstallRef() const;
    QString getUser() const;

signals:

public slots:
    void removeItem();
    void addItem();
    void setInstall();
    void userSelected(int selected);

private:
    bool changed;
    Cmd *cmd;
    QComboBox *comboRemote;
    QComboBox *comboUser;
    QLineEdit *editAddRemote;
    QLineEdit *editInstallFlatpakref;
    QString user;
    QString install_ref;
};

#endif // REMOTES_H
