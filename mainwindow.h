#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Atmosphere.h"
#include <QMainWindow>
#include <QtConcurrent>
#include <vsgXchange/all.h>
#include <vsg/all.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DatabaseException : QException
{
public:
    DatabaseException(const QString &path)
        : err_path(path)
    {
    }
    QString getErrPath() { return err_path; }

    void raise() const override { throw *this; }
    DatabaseException *clone() const override { return new DatabaseException(*this); }
private:
    QString err_path;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void generate();
    void setImagePath();
    void setSettingsPath();

    void openImage(const QString &text);
    void openSettings(const QString &text);

private:
    Ui::MainWindow *ui;

    enum Mask : uint64_t
    {
        Tiles = 0b1,
        SceneObjects = 0b10,
        Points = 0b100,
        Letters = 0b1000,
        Tracks = 0b10000
    };

    vsg::ref_ptr<vsg::Data> _defaultTexture;
    vsg::ref_ptr<atmosphere::AtmosphereModelSettings> _atmosphereSettings;

    QFutureWatcher<vsg::ref_ptr<vsg::PagedLOD>> *_watcher;

    vsg::ref_ptr<vsg::Options> _options;
    vsg::ref_ptr<vsg::Builder> _builder;
};
#endif // MAINWINDOW_H
