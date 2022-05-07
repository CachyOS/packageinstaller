#include "about.hpp"
#include "version.hpp"

#include <QIcon>
#include <QMessageBox>
#include <QPushButton>

// display doc as normal user when run as root
int displayDoc(const QString& url, bool runned_as_root) {
    int status{};
    if (!runned_as_root)
        status = system("xdg-open " + url.toUtf8());
    else
        status = system("runuser $(logname) -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u $(logname)) xdg-open " + url.toUtf8() + "\"&");

    return status;
}

void displayAboutMsgBox(const QString& title, const QString& message, const QString& licence_url, bool runned_as_root) {
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto* btnLicense = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto* btnCancel  = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licence_url, runned_as_root);
    }
}
