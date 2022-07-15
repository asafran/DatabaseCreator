#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QFileDialog>
#include <QColorDialog>
#include <QErrorMessage>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    _watcher = new QFutureWatcher<vsg::ref_ptr<vsg::PagedLOD>>();

    connect(_watcher, &QFutureWatcher<vsg::ref_ptr<vsg::PagedLOD>>::progressValueChanged, ui->progressBar, &QProgressBar::setValue);
    connect(_watcher, &QFutureWatcher<vsg::ref_ptr<vsg::PagedLOD>>::progressRangeChanged, ui->progressBar, &QProgressBar::setRange);

    _options = vsg::Options::create();
    _options->add(vsgXchange::all::create());

    _builder = vsg::Builder::create();
    _builder->options = _options;

    connect(ui->convButt, &QPushButton::clicked, this, &MainWindow::generate);
}

MainWindow::~MainWindow()
{
    delete ui;
}

inline vsg::dsphere assignGeometry(vsg::ref_ptr<vsg::Data> terrain, vsg::ref_ptr<vsg::EllipsoidModel> eps, vsg::ref_ptr<vsg::StateGroup> stateGroup, bool flat = false)
{
    uint32_t numRows = flat ? 32 : terrain->height();
    uint32_t numCols = flat ? 32 : terrain->width();
    uint32_t numVertices = numRows * numCols;
    uint32_t numTriangles = (numRows - 1) * (numCols - 1) * 2;

    auto transform = terrain->getObject<vsg::doubleArray>("GeoTransform");
    auto dx = flat ? (transform->at(1) * terrain->width() / 32) : transform->at(1);
    auto dy = flat ? (transform->at(5) * terrain->height() / 32) : transform->at(5);

    // set up vertex and index arrays
    auto vertices = vsg::vec3Array::create(numVertices);
    auto texcoords = vsg::vec2Array::create(numVertices);
    auto normals = vsg::vec3Array::create(numVertices, vsg::vec3(0.0f, 0.0f, 1.0f));
    auto colors = vsg::vec4Array::create(1, vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    //Xp = padfTransform[0] + P*padfTransform[1] + L*padfTransform[2];
    //Yp = padfTransform[3] + P*padfTransform[4] + L*padfTransform[5];

    vsg::vec2 texcoord_origin(0.0f, 0.0f);
    vsg::vec2 texcoord_dx(1.0f / (static_cast<float>(numCols) - 1.0f), 0.0f);
    vsg::vec2 texcoord_dy(0.0f, 1.0f / (static_cast<float>(numRows) - 1.0f));

    vsg::dvec3 lla = {transform->at(0) + (numCols*dx*0.5), transform->at(3) + (numRows*dy*0.5), 0.0};
    auto mat = eps->computeLocalToWorldTransform(lla);
    auto inv = vsg::inverse(mat);

    auto vertex_itr = vertices->begin();
    auto texcoord_itr = texcoords->begin();

    for (uint32_t r = 0; r < numRows; ++r)
    {
        for (uint32_t c = 0; c < numCols; ++c)
        {
            vsg::dvec3 lla = {transform->at(0) + c*dx, transform->at(3) + r*dy, 0.0};
            auto ecef = eps->convertLatLongAltitudeToECEF(lla);
            *vertex_itr = inv * ecef;
            *texcoord_itr = texcoord_origin + texcoord_dx * (static_cast<float>(c)) + texcoord_dy * (static_cast<float>(r));
            vertex_itr++;
            texcoord_itr++;
        }
    }

    uint32_t numIndices = (numRows - 1) * (numCols - 1) * 6;
    auto indices = vsg::ushortArray::create(numIndices);

    unsigned int i = 0;
    for (unsigned int r = 0; r < numRows - 1; ++r)
    {
        for (unsigned int c = 0; c < numCols - 1; ++c)
        {
            unsigned lower = numCols * r + c;
            unsigned upper = lower + numCols;

            indices->set(i++, lower);
            indices->set(i++, lower + 1);
            indices->set(i++, upper);

            indices->set(i++, upper);
            indices->set(i++, lower + 1);
            indices->set(i++, upper + 1);
        }
    }

    auto vid = vsg::VertexIndexDraw::create();
    vid->assignArrays(vsg::DataList{vertices, normals, texcoords, colors});
    vid->assignIndices(indices);
    vid->indexCount = numIndices;
    vid->instanceCount = 1;

    auto transformNode = vsg::MatrixTransform::create(mat);

    transformNode->addChild(vid);
    stateGroup->addChild(transformNode);

    auto ecef = eps->convertLatLongAltitudeToECEF(lla);
    vsg::dsphere bound;
    bound.center = ecef;
    bound.radius = vsg::length(eps->convertLatLongAltitudeToECEF({transform->at(0), transform->at(3), 0.0}) - ecef);

    return bound;
}

void MainWindow::generate()
{
    QStringList mapFiles;
    QString mergedFile;
    QString outFolder;
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFiles);
        dialog.setNameFilter(tr("Georeferenced images (*.tiff *.tif)"));
        if (dialog.exec())
            mapFiles = dialog.selectedFiles();
        else
            return;
    }
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::ExistingFile);
        dialog.setNameFilter(tr("Georeferenced images (*.tiff *.tif)"));
        if (dialog.exec())
            mergedFile = dialog.selectedFiles().front();
        else
            return;
    }
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setNameFilter(tr("VSG files (*.vsgb)"));
        if (dialog.exec())
            outFolder = dialog.selectedFiles().front();
        else
            return;
    }

    auto mergedTexture = vsg::read_cast<vsg::Data>(mergedFile.toStdString(), _options);
    if(!mergedTexture)
    {
        return;
    }

    auto group = vsg::Group::create();

    auto ellipsoidModel = vsg::EllipsoidModel::create();
    group->setObject("EllipsoidModel", ellipsoidModel);

    vsg::StateInfo si;
    si.image = mergedTexture;

    auto mergedState = _builder->createStateGroup(si);
    assignGeometry(mergedTexture, ellipsoidModel, mergedState, true);

    group->addChild(mergedState);

    bool isText = ui->textBox->isChecked();
    bool generateTexture = ui->textureBox->isChecked();

    int width = ui->width->value();
    int height = ui->height->value();

    int r = ui->rSpin->value() / 255.0f;
    int g = ui->gSpin->value() / 255.0f;
    int b = ui->bSpin->value() / 255.0f;

    vsg::vec4 colour(r, g, b, 1.0f);

    auto load = [ =, options=_options, builder=_builder, transition=ui->transSpin->value()] (QString tilepath)
    {
        auto terrain = vsg::read_cast<vsg::Data>(tilepath.toStdString(), options);

        vsg::StateInfo si;
        si.displacementMap = terrain;

        if(generateTexture)
        {
            auto texture = vsg::vec4Array2D::create(width, height, colour);
            texture->getLayout().format = VK_FORMAT_R32G32B32A32_SFLOAT;
            si.image = texture;
        }

        auto state = builder->createStateGroup(si);
        auto bound = assignGeometry(terrain, ellipsoidModel, state);

        auto sw = vsg::Switch::create();

        QFileInfo fi(tilepath);
        sw->addChild(Tiles, state);
        auto out = (outFolder + "/" + fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();
        vsg::write(sw, out);

        auto plod = vsg::PagedLOD::create();
        plod->children[0] = vsg::PagedLOD::Child{transition, {}};
        plod->children[1] = vsg::PagedLOD::Child{0.0, vsg::Node::create()};
        plod->bound = bound;

        plod->filename = (fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();

        return plod;
    };

    auto future = QtConcurrent::mapped(mapFiles, load);
    future.then([group, outFolder, isText](QFuture<vsg::ref_ptr<vsg::PagedLOD>> f)
    {
        std::move(f.begin(), f.end(), std::back_inserter(group->children));
        vsg::write(group, outFolder.toStdString() + "/database" + (isText ? ".vsgt" : ".vsgb"));
    });

    _watcher->setFuture(future);
}
