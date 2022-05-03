#ifndef CMD_HPP
#define CMD_HPP

#include <QProcess>
#include <QString>

class Cmd : public QProcess {
    Q_OBJECT
 public:
    explicit Cmd(QObject* parent = nullptr);

    void halt();
    bool run(const QString& cmd, bool quiet = false);
    bool run(const QString& cmd, QString& output, bool quiet = false);
    QString getCmdOut(const QString& cmd, bool quiet = false);

 signals:
    void finished();
    void errorAvailable(const QString& err);
    void outputAvailable(const QString& out);

 private:
    QString out_buffer{};
};

#endif  // CMD_HPP
