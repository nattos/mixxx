#pragma once

#include <QDrag>
#include <QDropEvent>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>

#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "util/fileinfo.h"
#include "widget/trackdroptarget.h"

class DragAndDropHelper final {
  public:
    DragAndDropHelper() = delete;

    static QList<mixxx::FileInfo> supportedTracksFromUrls(
            const QList<QUrl>& urls,
            bool firstOnly,
            bool acceptPlaylists);

    static bool allowDeckCloneAttempt(
            const QDropEvent& event,
            const QString& group);

    static bool dragEnterAccept(
            const QMimeData& mimeData,
            const QString& sourceIdentifier,
            bool firstOnly,
            bool acceptPlaylists);

    static QDrag* dragTrack(
            TrackPointer pTrack,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static QDrag* dragTrackLocations(
            const QList<QString>& locations,
            QWidget* pDragSource,
            const QString& sourceIdentifier);

    static void handleTrackDragEnterEvent(
            QDragEnterEvent* event,
            const QString& group,
            UserSettingsPointer pConfig);

    static void handleTrackDropEvent(
            QDropEvent* event,
            TrackDropTarget& target,
            const QString& group,
            UserSettingsPointer pConfig);

    static bool hasUrls(const QMimeData* mimeData);
    static QList<QUrl> getUrls(const QMimeData* mimeData);
};
