#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "interface.h"
#include "core/NotesBackend.h"

class NotesPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.NotesModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit NotesPlugin(QObject* parent = nullptr);

    QString name()    const override { return QStringLiteral("notes"); }
    QString version() const override { return QStringLiteral("1.0.0"); }

    // Shell injection — called first, before initialize()
    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // Shell calls after initLogos; returns "ok"
    Q_INVOKABLE QString initialize();

    // Screen routing — called by QML on startup
    Q_INVOKABLE QString isInitialized();

    // Account management
    Q_INVOKABLE QString importMnemonic(const QString& mnemonic,
                                       const QString& pin,
                                       const QString& confirm);
    Q_INVOKABLE QString unlockWithPin(const QString& pin);

    // Note persistence
    Q_INVOKABLE QString loadNote();
    Q_INVOKABLE QString saveNote(const QString& text);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    static QString ok();
    static QString successJson();
    static QString errorJson(const QString& msg);

    NotesBackend m_backend;
};
