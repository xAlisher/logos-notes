#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "core/NotesBackend.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("logos-notes");
    app.setOrganizationName("logos-co");

    NotesBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);

    const QUrl url("qrc:/qt/qml/LogosNotes/src/ui/main.qml");
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    [](const QUrl &url) {
            qCritical() << "QML load failed:" << url;
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);

    engine.load(url);
    return app.exec();
}
