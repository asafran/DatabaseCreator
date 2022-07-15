#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtConcurrent>
#include <vsgXchange/all.h>
#include <vsg/all.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void generate();

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

    QFutureWatcher<vsg::ref_ptr<vsg::PagedLOD>> *_watcher;

    vsg::ref_ptr<vsg::Options> _options;
    vsg::ref_ptr<vsg::Builder> _builder;
};
#endif // MAINWINDOW_H
