#include "remotes.h"

#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include <QDebug>

ManageRemotes::ManageRemotes(QWidget *parent, const QStringList& items) :
    QDialog(parent)
{
    setWindowTitle(tr("Manage Flatpak Remotes"));
    changed = false;
    cmd = new Cmd(this);
    user = "--system ";

    QGridLayout *layout = new QGridLayout();
    setLayout(layout);

    box = new QComboBox(this);
    box->addItems(items);

    edit = new QLineEdit(this);
    edit->setMinimumWidth(400);
    edit->setPlaceholderText(tr("enter Flatpak remote URL"));

    cb_user = new QCheckBox(this);
    cb_user->setText(tr("Only for current user"));

    QLabel *label = new QLabel(tr("Add or remove Flatpak remotes"));
    layout->addWidget(label, 0, 0, 1, 5);
    label->setAlignment(Qt::AlignCenter);

    layout->addWidget(box, 1, 0, 1, 4);
    layout->addWidget(edit, 2, 0, 1, 4);
    layout->addWidget(cb_user, 3, 0, 1, 4);

    QPushButton *remove = new QPushButton(tr("Remove"));
    remove->setIcon(QIcon::fromTheme("remove"));
    remove->setAutoDefault(false);
    layout->addWidget(remove, 1, 4, 1, 1);

    QPushButton *add = new QPushButton(tr("Add"));
    add->setIcon(QIcon::fromTheme("add"));
    add->setAutoDefault(false);
    layout->addWidget(add, 2, 4, 1, 1);

    QPushButton *cancel = new QPushButton(tr("Close"));
    cancel->setIcon(QIcon::fromTheme("window-close"));
    cancel->setAutoDefault(false);
    layout->addWidget(cancel, 4, 4, 1, 1);

    connect(cancel, &QPushButton::clicked, this, &ManageRemotes::close);
    connect(remove, &QPushButton::clicked, this, &ManageRemotes::removeItem);
    connect(add, &QPushButton::clicked, this, &ManageRemotes::addItem);
    connect(cb_user, &QCheckBox::clicked, this, &ManageRemotes::userSelected);

    listFlatpakRemotes();
}

bool ManageRemotes::isChanged()
{
    return changed;
}

void ManageRemotes::removeItem()
{
    if (box->currentText() == "flathub") {
        QMessageBox::information(this, tr("Not removable"), tr("Flathub is the main Flatpak remote and won't be removed"));
        return;
    }
    changed = true;
    cmd->run("su $(logname) -c \"flatpak remote-delete " + user + box->currentText().toUtf8() + "\"");
    box->removeItem(box->currentIndex());
}

void ManageRemotes::addItem()
{
    setCursor(QCursor(Qt::BusyCursor));
    QString location = edit->text();
    QString name = edit->text().section("/", -1).section(".", 0, 0); // obtain the name before .flatpakremo

    if (cmd->run("su $(logname) -c \"flatpak remote-add --if-not-exists " + user + name.toUtf8() + " " + location.toUtf8() + "\"") != 0) {
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::critical(this, tr("Error adding remote"), tr("Could not add remote. Command returned an error, please double-check the remote address and try again"));
    } else {
        changed = true;
        setCursor(QCursor(Qt::ArrowCursor));
        QMessageBox::information(this, tr("Success"), tr("Remote added successfully"));
        edit->clear();
        box->addItem(name);
    }
}

void ManageRemotes::userSelected(bool selected)
{
    if (selected) {
        user = "--user ";
        setCursor(QCursor(Qt::BusyCursor));
        cmd->run("su $(logname) -c \"flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo\"");
        setCursor(QCursor(Qt::ArrowCursor));
    } else {
        user = "--system ";
    }
    listFlatpakRemotes();
}


// List the flatpak remote and loade them into combobox
void ManageRemotes::listFlatpakRemotes()
{
    qDebug() << "+++ Enter Function:" << __PRETTY_FUNCTION__ << "+++";
    box->clear();
    QStringList list = cmd->getOutput("su $(logname) -c \"flatpak remote-list " +  user + "\"").split("\n");
    box->addItems(list);
}
