﻿#include "mainwindow.h"
#include "./ui_mainwindow.h"


#include <QFileDialog>
#include <QColorDialog>
#include <QErrorMessage>
#include <QMessageBox>

#include "tile.h"
#include "route.h"
#include "tools.h"

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
    _options->paths = vsg::getEnvPaths("RRS2_ROOT");

    auto ellipsoidModel = vsg::EllipsoidModel::create(vsg::WGS_84_RADIUS_EQUATOR, vsg::WGS_84_RADIUS_EQUATOR);
    _atmosphereSettings = atmosphere::AtmosphereModelSettings::create(ellipsoidModel);

    vsg::Logger::instance() = vsg::NullLogger::create();

    _builder = vsg::Builder::create();
    _builder->options = _options;

    connect(ui->convButt, &QPushButton::clicked, this, &MainWindow::generate);

    connect(ui->imageButt, &QPushButton::pressed, this, &MainWindow::setImagePath);
    connect(ui->settingsButt, &QPushButton::pressed, this, &MainWindow::setSettingsPath);

    connect(ui->imagePath, &QLineEdit::textChanged, this, &MainWindow::openImage);
    connect(ui->settingsPath, &QLineEdit::textChanged, this, &MainWindow::openSettings);

    vsg::RegisterWithObjectFactoryProxy<route::Tile>();
    vsg::RegisterWithObjectFactoryProxy<route::SceneGroup>();
    vsg::RegisterWithObjectFactoryProxy<route::Route>();
    vsg::RegisterWithObjectFactoryProxy<route::Topology>();
    vsg::RegisterWithObjectFactoryProxy<route::MTransform>();

    vsg::RegisterWithObjectFactoryProxy<atmosphere::AtmosphereModelSettings>();
    vsg::RegisterWithObjectFactoryProxy<atmosphere::AtmosphereData>();

}

MainWindow::~MainWindow()
{
    delete ui;
}

inline std::pair<vsg::dsphere, vsg::ref_ptr<vsg::MatrixTransform>> assignGeometry(vsg::ref_ptr<vsg::Data> terrain, vsg::ref_ptr<vsg::EllipsoidModel> eps, vsg::ref_ptr<vsg::StateGroup> state, bool flat = false)
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

    auto vid = vsg::VertexIndexDraw::create();
    vid->assignArrays(vsg::DataList{vertices, normals, texcoords, colors});
    vid->instanceCount = 1;

    uint32_t numIndices = (numRows - 1) * (numCols - 1) * 6;
    if(numVertices > std::numeric_limits<uint16_t>::max())
    {
        auto array = vsg::uintArray::create(numIndices);
        unsigned int i = 0;
        for (unsigned int r = 0; r < numRows - 1; ++r)
        {
            for (unsigned int c = 0; c < numCols - 1; ++c)
            {
                unsigned lower = numCols * r + c;
                unsigned upper = lower + numCols;

                array->set(i++, lower);
                array->set(i++, lower + 1);
                array->set(i++, upper);

                array->set(i++, upper);
                array->set(i++, lower + 1);
                array->set(i++, upper + 1);
            }
        }
        vid->assignIndices(array);
    }
    else
    {
        auto array = vsg::ushortArray::create(numIndices);
        unsigned int i = 0;
        for (unsigned int r = 0; r < numRows - 1; ++r)
        {
            for (unsigned int c = 0; c < numCols - 1; ++c)
            {
                unsigned lower = numCols * r + c;
                unsigned upper = lower + numCols;

                array->set(i++, lower);
                array->set(i++, lower + 1);
                array->set(i++, upper);

                array->set(i++, upper);
                array->set(i++, lower + 1);
                array->set(i++, upper + 1);
            }
        }
        vid->assignIndices(array);
    }

    vid->indexCount = numIndices;

    auto mt = vsg::MatrixTransform::create(mat);
    mt->addChild(state);
    state->addChild(vid);

    auto ecef = eps->convertLatLongAltitudeToECEF(lla);
    vsg::dsphere bound;
    bound.center = ecef;
    bound.radius = vsg::length(eps->convertLatLongAltitudeToECEF({transform->at(0), transform->at(3), 0.0}) - ecef);

    return {bound, mt};
}

void MainWindow::generate()
{
    ui->progressBar->setValue(0);

    QString mapFolder;
    QString outFolder;
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::Directory);
        if (dialog.exec())
            mapFolder = dialog.selectedFiles().front();
        else
            return;
    }
    {
        QFileDialog dialog(this);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        if (dialog.exec())
            outFolder = dialog.selectedFiles().front();
        else
            return;
    }

    QRegularExpression re("([_]\\d+){2}[.]");
    QStringList files;

    QDirIterator it(mapFolder);
    while (it.hasNext()) {
        QString file = it.next();
        auto match = re.match(file);
        if(match.hasMatch())
            files.append(file);
    }

    auto ellipsoidModel = _atmosphereSettings->ellipsoidModel;

    bool isText = ui->textBox->isChecked();
    bool generateTexture = ui->colorRadio->isChecked();

    auto width = ui->width->value();
    auto aoWidth = ui->aoSpin->value();
    auto transition = ui->transSpin->value();

    int r = ui->rSpin->value() / 255.0f;
    int g = ui->gSpin->value() / 255.0f;
    int b = ui->bSpin->value() / 255.0f;

    vsg::vec4 colour(r, g, b, 1.0f);

    state::PhongStateInfo si;
    si.image = _defaultTexture;
    si.material.ambient.set(ui->ar->value(), ui->ag->value(), ui->ab->value(), ui->as->value());
    si.material.diffuse.set(ui->dr->value(), ui->dg->value(), ui->db->value(), ui->ds->value());
    si.material.specular.set(ui->sr->value(), ui->sg->value(), ui->sb->value(), ui->ss->value());
    si.material.emissive.set(ui->er->value(), ui->eg->value(), ui->eb->value(), ui->es->value());
    si.material.shininess = ui->shininess->value();

    auto model = atmosphere::createAtmosphereModel(_atmosphereSettings, _options);
    auto viewDependent = atmosphere::AtmosphereLighting::create();
    model->viewDescriptorSetLayout = viewDependent->descriptorSetLayout;
    auto data = model->getData();

    _builder->options->shaderSets["phong"] = data->phongShaderSet;

    auto first = vsg::read_cast<vsg::Data>(files.begin()->toStdString(), _options);
    auto transform = first->getObject<vsg::doubleArray>("GeoTransform");
    auto lla = vsg::dvec3(transform->at(0), transform->at(3), 0.0);

    auto route = route::Route::create(data, ellipsoidModel->computeLocalToWorldTransform(lla));

    //vsgXchange::initGDAL();

    auto load = [ =, options=_options, builder=_builder] (QString tilepath)
    {
        auto localStateInfo = si;

        auto terrain = vsg::read_cast<vsg::Data>(tilepath.toStdString(), options);
        auto transform = terrain->getObject<vsg::doubleArray>("GeoTransform");
        if(!transform)
            throw DatabaseException(tilepath);

        localStateInfo.displacementMap = terrain;

        auto aspect = std::abs(transform->at(5)) / transform->at(1);

        if(generateTexture || !localStateInfo.image)
        {
            auto height = static_cast<int>(static_cast<double>(width) * aspect);

            vsg::Data::Properties prp{VK_FORMAT_R32G32B32A32_SFLOAT};
            localStateInfo.image = vsg::vec4Array2D::create(width, height, colour, prp);
        }

        vsg::ref_ptr<vsg::StateGroup> state;

        auto aoHeight = static_cast<int>(static_cast<double>(aoWidth) * aspect);
        vsg::Data::Properties prp{VK_FORMAT_R32_SFLOAT};
        localStateInfo.aoMap = vsg::floatArray2D::create(aoWidth, aoWidth, 1.0f, prp);

        state = vsg::StateGroup::create();
        state::assignStateGroup(state, localStateInfo, options);

        auto [bounds, geometry] = assignGeometry(terrain, ellipsoidModel, state);

        QFileInfo fi(tilepath);
        auto out = (outFolder + "/" + fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();

        QRegularExpression regexp("[_](\\d+)");
        auto match = regexp.match(tilepath);
        auto row = match.captured(0).toInt();
        auto col = match.captured(1).toInt();

        auto tile = route::Tile::create(localStateInfo, geometry, row, col);
        vsg::write(tile, out);

        auto plod = vsg::PagedLOD::create();
        plod->children[0] = vsg::PagedLOD::Child{transition, {}};
        plod->children[1] = vsg::PagedLOD::Child{0.0, vsg::Node::create()};
        plod->bound = bounds;

        plod->filename = (fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();

        return plod;
    };

    auto future = QtConcurrent::mapped(files, load);
    future.then([route, outFolder, isText](QFuture<vsg::ref_ptr<vsg::PagedLOD>> f)
    {
        std::copy(f.begin(), f.end(), std::back_inserter(route->plods->children));
        vsg::write(route, outFolder.toStdString() + "/database" + (isText ? ".vsgt" : ".vsgb"));
    });

    _watcher->setFuture(future);
}

void MainWindow::setImagePath()
{
    if (const auto file = QFileDialog::getOpenFileName(this, tr("Open image"), qApp->applicationDirPath()); !file.isEmpty())
        ui->imagePath->setText(file);
}

void MainWindow::setSettingsPath()
{
    if (const auto file = QFileDialog::getOpenFileName(this, tr("Open atmosphere settings"), qApp->applicationDirPath(), "*.vsgt"); !file.isEmpty())
        ui->settingsPath->setText(file);
}

void MainWindow::openImage(const QString &text)
{
    _defaultTexture = vsg::read_cast<vsg::Data>(text.toStdString(), _options);
    if(!_defaultTexture)
        statusBar()->showMessage(tr("Failed to read image"));
    else
        statusBar()->showMessage(tr("Image read successfully"));
}

void MainWindow::openSettings(const QString &text)
{
    auto fallback = _atmosphereSettings;
    _atmosphereSettings = vsg::read_cast<atmosphere::AtmosphereModelSettings>(text.toStdString(), _options);
    if(!_atmosphereSettings)
    {
        _atmosphereSettings = fallback;
        statusBar()->showMessage(tr("Failed to read atmosphere settings"));
    }
    else
        statusBar()->showMessage(tr("Atmosphere settings read successfully"));
}
