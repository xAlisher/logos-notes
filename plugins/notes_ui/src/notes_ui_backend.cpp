#include "notes_ui_backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include "logos_sdk.h"   // generated: modules().logos_beacon (Qt-typed)

// Map a Qt LogosResult to the QString contract QML parses (callModuleParse):
//  - failure          → {"error": "..."}  (QML checks parsed.error / "error" prefix)
//  - scalar/string    → the raw value string
//  - object/array     → compact JSON of the value
// Errors resolve through logos.watch's SUCCESS callback (the bridge only rejects on
// QtRO transport failure), so QML inspects the returned string, not the reject path.
static QString resultToJson(const LogosResult& r)
{
    if (!r.success) {
        QJsonObject o;
        o["error"] = r.getError();
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    QVariant v = r.value;
    if (v.typeId() == QMetaType::QString)
        return v.toString();
    QJsonDocument doc = QJsonDocument::fromVariant(v);
    return doc.isNull() ? r.getString()
                        : QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString NotesUiBackend::getBeaconConfig()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getBeaconConfig());
}

QString NotesUiBackend::getInscriptionLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getInscriptionLog());
}

QString NotesUiBackend::pinCid(QString cid, QString label)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    // notes passes only (cid, label); fill logos_beacon's source/slot args:
    // source "notes" (matches the stash upload source), slots unknown → 0.
    return resultToJson(modules().logos_beacon.pinCid(
        cid, label, QStringLiteral("notes"), 0, 0));
}
