#include "remotes.h"

#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <QDebug>

ManageRemotes::ManageRemotes(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Manage Flatpak Remotes"));
    changed = false;
    cmd = new Cmd(this);
    user = "--system ";

    QGridLayout *layout = new QGridLayout();
    setLayout(layout);

    comboUser = new QComboBox(this);
    comboUser->addItem(tr("For all users"));
    comboUser->addItem(tr("For current user"));

    comboRemote = new QComboBox(this);

    editAddRemote = new QLineEdit(this);
    editAddRemote->setMinimumWidth(400);
    editAddRemote->setPlaceholderText(tr("enter Flatpak remote URL"));

    editInstallFlatpakref = new QLineEdit(this);
    editInstallFlatpakref->setPlaceholderText(tr("enter Flatpakref location to install app"));

    QLabel *label = new QLabel(tr("Add or remove flatpak remotes (repos), or install apps using flatpakref URL or path"));
    layout->addWidget(label, 0, 0, 1, 5);
    label->setAlignment(Qt::AlignCenter);

    layout->addWidget(comboUser, 1, 0, 1, 4);
    layout->addWidget(comboRemote, 2, 0, 1, 4);
    layout->addWidget(editAddRemote, 3, 0, 1, 4);
    layout->addWidget(editInstallFlatpakref, 4, 0, 1, 4);

    QPushButton *remove = new QPushButton(tr("Remove remote"));
    remove->setIcon(QIcon::fromTheme("remove"));
    remove->setAutoDefault(false);
    layout->addWidget(remove, 2, 4, 1, 1);

    QPushButton *add = new QPushButton(tr("Add remote"));
    add->setIcon(QIcon::fromTheme("add"));
    add->setAutoDefault(false);
    layout->addWidget(add, 3, 4, 1, 1);

    QPushButton *install = new QPushButton(tr("Install app"));
    install->setIcon(QIcon::fromTheme("run-install"));
    install->setAutoDefault(false);
    layout->addWidget(install, 4, 4, 1, 1);

    QPushButton *cancel = new QPushButton(tr("Close"));
    cancel->setIcon(QIcon::fromTheme("window-close"));
    cancel->setAutoDefault(false);
    layout->addWidget(cancel, 5, 4, 1, 1);

    connect(cancel, &QPushButton::clicked, this, &ManageRemotes::close);
    connect(remove, &QPushButton::clicked, this, &ManageRemotes::removeItem);
    connect(add, &QPushButton::clicked, this, &ManageRemotes::addItem);
    connect(install, &QPushButton::clicked, this, &ManageRemotes::setInstall);
    connect(comboUser, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &ManageRemotes::userSelected);

    listFlatpakRemotes();
}

bool ManageRemotes::isChanged() const
{
    return changed;
}

void ManageRemotes::removeItem()
{
    if (comboRemote->currentText() == "flathub") {
        QMessageBox::information(this, tr("Not removable"), tr("Flathub is the main Flatpak remote and won't be removed"));
        return;
    }
    changed = true;
    cmd->run("su $(logname) -c \"flatpak remote-delete " + user + comboRemote->currentText().toUtf8() + "\"");
    comboRemote->removeItem(comboRemote->currentIndex());
}

void ManageRemotes::addItem()
{
    setCursor(QCursor(Qt::BusyCursor));
    QString location = editAddRemote->text();
    QString name = editAddRemote->text().section("/", -1).section(".", 0, 0); // obtain the name before .flatpakremo

    if (!cmd->run("su $(logname) -c \"flatpak remote-add --if-not-exists " + user + name.toUtf8() + " " + location.toUtf8() + "\"")) {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error adding remote"), tr("Could not add remote - command returned an error. Please double-check the remote address and try again"));
    } else {
        changed = true;
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Success"), tr("Remote added successfully"));
        editAddRemote->clear();
        comboRemote->addItem(name);
    }
}

void ManageRemotes::setInstall()
{
    install_ref = editInstallFlatpakref->text();
    this->close();
}

void ManageRemotes::userSelected(int index)
{
    if (index == 0) {
        user = "--system ";
    } else {
        user = "--user ";
        setCursor(QCursor(Qt::BusyCursor));
        cmd->run("su $(logname) -c \"flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
        setCursor(QCursor(Qt::ArrowCursor));
    }
    listFlatpakRemotes();
}


// List the flatpak remote and loade them into combobox
void ManageRemotes::listFlatpakRemotes() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    comboRemote->clear();
    QStringList list = cmd->getCmdOut("su $(logname) -c \"flatpak remote-list " +  user + "\"").split("\n");
    comboRemote->addItems(list);
}

QString ManageRemotes::getInstallRef() const
{
    return install_ref;
}

QString ManageRemotes::getUser() const
{
    return user;
}
