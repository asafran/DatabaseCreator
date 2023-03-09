#include "mainwindow.h"
#include "./ui_mainwindow.h"


#include <QFileDialog>
#include <QColorDialog>
#include <QErrorMessage>
#include <QMessageBox>

#ifdef tiles_FOUND
#    include "tile.h"
#    include "tools.h"
#endif

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

    vsg::Logger::instance() = vsg::NullLogger::create();

    _builder = vsg::Builder::create();
    _builder->options = _options;

    connect(ui->convButt, &QPushButton::clicked, this, &MainWindow::generate);

    connect(ui->imageButt, &QPushButton::clicked, this, [this]
    {
        if (const auto file = QFileDialog::getOpenFileName(this, tr("Open image"), qApp->applicationDirPath()); !file.isEmpty())
            ui->imagePath->setText(file);
    });
#ifdef tiles_FOUND
    vsg::RegisterWithObjectFactoryProxy<route::Tile>();
    vsg::RegisterWithObjectFactoryProxy<route::SceneGroup>();
#endif

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

    QString mapFolder = "/home/asafr/Documents/dems/retile";
    QString outFolder = "/home/asafr/RRS/routes/vsg";
    /*{
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
    }*/

    QRegularExpression re("([_]\\d+){2}[.]");
    QStringList files;

    QDirIterator it(mapFolder);
    while (it.hasNext()) {
        QString file = it.next();
        auto match = re.match(file);
        if(match.hasMatch())
            files.append(file);
    }

    auto group = vsg::Group::create();

    auto ellipsoidModel = vsg::EllipsoidModel::create();
    group->setObject("EllipsoidModel", ellipsoidModel);

    vsg::ref_ptr<vsg::Data> image;

    if(ui->imageRadio->isChecked())
        image = vsg::read_cast<vsg::Data>(ui->imagePath->text().toStdString(), _options);

    bool isText = ui->textBox->isChecked();
    bool generateTexture = ui->colorRadio->isChecked();

    auto width = ui->aoSpin->value();
    auto transition = ui->transSpin->value();
    auto classic = ui->classicBox->isChecked();

    int r = ui->rSpin->value() / 255.0f;
    int g = ui->gSpin->value() / 255.0f;
    int b = ui->bSpin->value() / 255.0f;

    vsg::vec4 colour(r, g, b, 1.0f);

    objtools::PhongStateInfo si;
    si.image = image;
    si.material.ambient.set(ui->ar->value(), ui->ag->value(), ui->ab->value(), ui->as->value());
    si.material.diffuse.set(ui->dr->value(), ui->dg->value(), ui->db->value(), ui->ds->value());
    si.material.specular.set(ui->sr->value(), ui->sg->value(), ui->sb->value(), ui->ss->value());
    si.material.emissive.set(ui->er->value(), ui->eg->value(), ui->eb->value(), ui->es->value());
    si.material.shininess = ui->shininess->value();

    vsg::read_cast<vsg::Data>(files.begin()->toStdString(), _options);

    //vsgXchange::initGDAL();

    auto load = [ =, options=_options, builder=_builder] (QString tilepath) mutable
    {
        auto terrain = vsg::read_cast<vsg::Data>(tilepath.toStdString(), options);
        auto transform = terrain->getObject<vsg::doubleArray>("GeoTransform");
        if(!transform)
            throw DatabaseException(tilepath);

        si.displacementMap = terrain;

        auto aspect = std::abs(transform->at(5)) / transform->at(1);

        if(generateTexture || !si.image)
        {
            auto width = ui->width->value();
            auto height = static_cast<int>(static_cast<double>(width) * aspect);

            si.image = vsg::vec4Array2D::create(width, height, colour);
            si.image->properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        }

        vsg::ref_ptr<vsg::StateGroup> state;

#ifdef tiles_FOUND
        auto height = static_cast<int>(static_cast<double>(width) * aspect);
        auto aoMap = vsg::floatArray2D::create(width, height, 1.0f);
        si.aoMap = aoMap;

        state = vsg::StateGroup::create();
        objtools::assignStateGroup(state, si, options);
#else
        vsg::StateInfo si;
        si.displacementMap = terrain;
        si.image = image;
        state = builder->createStateGroup(si);
#endif
        auto [bounds, geometry] = assignGeometry(terrain, ellipsoidModel, state);

        QFileInfo fi(tilepath);
        auto out = (outFolder + "/" + fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();

        if(classic)
            vsg::write(geometry, out);
        else
        {
#ifdef tiles_FOUND

            QRegularExpression regexp("[_](\\d+)");
            auto match = regexp.match(tilepath);
            auto row = match.captured(0).toInt();
            auto col = match.captured(1).toInt();

            auto tile = route::Tile::create();
            tile->alwaysVisible = route::SceneGroup::create();
            tile->lowDetail = route::SceneGroup::create();
            tile->midDetail = route::SceneGroup::create();
            tile->highDetail = route::SceneGroup::create();
            tile->editorVisible = route::SceneGroup::create();
            tile->terrainNode = geometry;
            tile->texture = si.image;
            tile->terrain = si.displacementMap;
            tile->geoTransform = transform;
            //tile->aoMap = aoMap;
            tile->row = row;
            tile->col = col;
            vsg::write(tile, out);
#endif
        }

        auto plod = vsg::PagedLOD::create();
        plod->children[0] = vsg::PagedLOD::Child{transition, {}};
        plod->children[1] = vsg::PagedLOD::Child{0.0, vsg::Node::create()};
        plod->bound = bounds;

        plod->filename = (fi.completeBaseName() + (isText ? ".vsgt" : ".vsgb")).toStdString();

        return plod;
    };

    auto future = QtConcurrent::mapped(files, load);
    future.then([group, outFolder, isText](QFuture<vsg::ref_ptr<vsg::PagedLOD>> f)
    {
        std::move(f.begin(), f.end(), std::back_inserter(group->children));
        vsg::write(group, outFolder.toStdString() + "/database" + (isText ? ".vsgt" : ".vsgb"));
    });

    _watcher->setFuture(future);
}
