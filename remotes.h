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
    explicit ManageRemotes(QWidget *parent = 0, const QStringList &items = QStringList());
    bool isChanged();
    void listFlatpakRemotes();

signals:

public slots:
    void removeItem();
    void addItem();
    void userSelected(bool selected);

private:
    bool changed;
    Cmd *cmd;
    QComboBox *box;
    QLineEdit *edit;
    QCheckBox *cb_user;
    QString run_as_user;
    QString user_switch;
    QString end_quote;
};

#endif // REMOTES_H
