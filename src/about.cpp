#include "about.hpp"
#include "cmd.hpp"
#include "version.hpp"

#include <memory>

#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

// display doc as normal user when run as root
void displayDoc(const QString& url, bool runned_as_root) {
    if (!runned_as_root)
        system("xdg-open " + url.toUtf8());
    else
        system("runuser $(logname) -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u $(logname)) xdg-open " + url.toUtf8() + "\"&");
}

void displayAboutMsgBox(const QString& title, const QString& message, const QString& licence_url, bool runned_as_root) {
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto* btnLicense   = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto* btnChangelog = msgBox.addButton(QObject::tr("Changelog"), QMessageBox::HelpRole);
    auto* btnCancel    = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licence_url, runned_as_root);
    } else if (msgBox.clickedButton() == btnChangelog) {
        std::unique_ptr<QDialog> changelog(new QDialog());
        changelog->setWindowTitle(QObject::tr("Changelog"));
        changelog->resize(600, 500);

        auto* text = new QTextEdit;
        text->setReadOnly(true);
        Cmd cmd;
        text->setText(cmd.getCmdOut("zless /usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName() + "/changelog.gz"));

        auto* btnClose = new QPushButton(QObject::tr("&Close"));
        btnClose->setIcon(QIcon::fromTheme("window-close"));
        QObject::connect(btnClose, &QPushButton::clicked, changelog.get(), &QDialog::close);

        auto* layout = new QVBoxLayout;
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
    }
}
