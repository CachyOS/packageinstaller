#ifndef ABOUT_HPP
#define ABOUT_HPP

#include "cmd.hpp"

#include <QString>

void displayDoc(const QString& url, bool runned_as_root = false);
void displayAboutMsgBox(const QString& title, const QString& message, const QString& licence_url, bool runned_as_root = false);

#endif  // ABOUT_HPP
