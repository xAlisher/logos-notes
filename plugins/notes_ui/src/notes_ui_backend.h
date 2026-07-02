#pragma once

#include "rep_notes_ui_source.h"      // generated from src/notes_ui.rep (repc)
#include "logos_ui_plugin_context.h"  // modules() + onContextReady()

/**
 * @brief notes_ui backend — bridges QML to the universal logos_beacon module.
 *
 * QML (logos.module("notes_ui") + logos.watch) → this backend → the Qt-free
 * universal logos_beacon via modules().logos_beacon.*. Replaces notes' legacy
 * logos.callModule("logos_beacon", ...) path, which returns "null" for the
 * universal logos_beacon 2.0.0 (logos-notes#105). Every SLOT forwards to the
 * same-named logos_beacon method and returns its result as a JSON string.
 * The "notes" module stays on legacy callModule (a legacy Qt module).
 */
class NotesUiBackend : public NotesUiSimpleSource,
                       public LogosUiPluginContext
{
public:
    QString getBeaconConfig() override;
    QString getInscriptionLog() override;
    QString pinCid(QString cid, QString label) override;

protected:
    void onContextReady() override { setBeaconReady(true); }
};
