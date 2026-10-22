#include "iconprovider.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#include <functional>

int main(int argc, char *argv[])
{
    // MapLibre renders through OpenGL (GLES on the head unit), so the scene
    // graph must use the same API.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("godwit-hmi"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption simOption(QStringLiteral("sim"),
        QStringLiteral("Desktop simulator: the screen plus two on-screen encoders."));
    parser.addOption(simOption);
    QCommandLineOption appOption(QStringLiteral("app"), QStringLiteral("Screen to start on (nav, media, ...)."), QStringLiteral("id"));
    parser.addOption(appOption);
    QCommandLineOption darkOption(QStringLiteral("dark"), QStringLiteral("Start in the dark theme."));
    parser.addOption(darkOption);
    QCommandLineOption grabOption(QStringLiteral("grab"),
        QStringLiteral("Save the window to an image after --grab-after ms, then quit."), QStringLiteral("file"));
    parser.addOption(grabOption);
    QCommandLineOption grabAfterOption(QStringLiteral("grab-after"), QStringLiteral("Delay before --grab."), QStringLiteral("ms"), QStringLiteral("2500"));
    parser.addOption(grabAfterOption);
    parser.process(app);

    for (const char *face : { "Regular", "SemiBold", "Bold" })
        QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Godwit/assets/fonts/Inter-%1.ttf").arg(QLatin1StringView(face)));
    QFont font(QStringLiteral("Inter"));
    font.setHintingPreference(QFont::PreferNoHinting);
    QGuiApplication::setFont(font);

    QQmlApplicationEngine engine;

    // Desktop builds in the repository's build/ folder: use the map engine and
    // map data prepared beside this build (build/maplibre, build/maps), so the
    // simulator needs no environment variables. The environment still wins.
    const QDir buildDir(QCoreApplication::applicationDirPath() + QStringLiteral("/.."));
    if (QFileInfo::exists(buildDir.filePath(QStringLiteral("maplibre/qml/MapLibre/qmldir"))))
        engine.addImportPath(buildDir.filePath(QStringLiteral("maplibre/qml")));
    if (qEnvironmentVariableIsEmpty("GODWIT_MAP_STYLE_DIR") && QFileInfo::exists(buildDir.filePath(QStringLiteral("maps/styles"))))
        qputenv("GODWIT_MAP_STYLE_DIR", QDir::cleanPath(buildDir.filePath(QStringLiteral("maps"))).toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("GODWIT_MAP_DATA") && QFileInfo::exists(buildDir.filePath(QStringLiteral("maps/tiles.pmtiles"))))
        qputenv("GODWIT_MAP_DATA", QDir::cleanPath(buildDir.filePath(QStringLiteral("maps/tiles.pmtiles"))).toLocal8Bit());
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    if (parser.isSet(darkOption))
        engine.singletonInstance<QObject *>("Godwit", "Theme")->setProperty("dark", true);
    if (parser.isSet(appOption))
        engine.singletonInstance<QObject *>("Godwit", "Hmi")->setProperty("app", parser.value(appOption));
    engine.loadFromModule("Godwit", parser.isSet(simOption) ? "Simulator" : "Main");

    // MapLibre's Qt item frees its GL resources in its destructor without the
    // render context current, which crashes on exit. The process is ending
    // anyway, so detach the map and leave it to the OS.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine, [&engine] {
        QList<QQuickItem *> maps;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
            if (item->inherits("QMapLibre::MapQuickItem"))
                maps.append(item);
            const auto children = item->childItems();
            for (QQuickItem *child : children)
                find(child);
        };
        for (QObject *root : engine.rootObjects())
            if (auto *window = qobject_cast<QQuickWindow *>(root))
                find(window->contentItem());
        for (QQuickItem *map : std::as_const(maps)) {
            QQmlEngine::setObjectOwnership(map, QQmlEngine::CppOwnership);
            map->setParentItem(nullptr);
            map->setParent(nullptr);
        }
    });

    if (parser.isSet(grabOption) && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = parser.value(grabOption);
        QTimer::singleShot(parser.value(grabAfterOption).toInt(), &app, [window, file] {
            if (!window || !window->grabWindow().save(file))
                qWarning("grab: could not save %s", qPrintable(file));
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
