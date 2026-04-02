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
                                       const QString& confirm,
                                       const QString& backupPath = {});
    Q_INVOKABLE QString unlockWithPin(const QString& pin);

    // Note CRUD
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE QString loadNotes();
    Q_INVOKABLE QString loadNote(int id);
    Q_INVOKABLE QString saveNote(int id, const QString& text);
    Q_INVOKABLE QString deleteNote(int id);

    Q_INVOKABLE QString lockSession();
    Q_INVOKABLE QString getKeySource();
    Q_INVOKABLE QString getAccountFingerprint();
    Q_INVOKABLE QString exportBackup(const QString& filePath);
    Q_INVOKABLE QString exportBackupAuto();
    Q_INVOKABLE QString listBackups();
    Q_INVOKABLE QString importBackup(const QString& filePath,
                                      const QString& mnemonic = {});
    Q_INVOKABLE QString resetAndWipe();

    // Keycard module integration (receives key from keycard-basecamp)
    Q_INVOKABLE QString importWithKeycardKey(const QString& hexKey,
                                              const QString& backupPath = {});
    Q_INVOKABLE QString unlockWithKeycardKey(const QString& hexKey);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    static QString ok();
    static QString successJson();
    static QString errorJson(const QString& msg);

    NotesBackend m_backend;
};
