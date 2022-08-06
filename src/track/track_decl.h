#pragma once

/// Forward declarations for Track objects and their pointers

#include <QList>
#include <QMetaType>
#include <memory>

class Track;

typedef std::shared_ptr<Track> TrackPointer;
typedef std::weak_ptr<Track> TrackWeakPointer;
typedef QList<TrackPointer> TrackPointerList;

struct TrackCursor {
    TrackPointer Track;
    int TrackIndex = 0;
    int LastTrackOffset = 0;
    std::function<bool(const TrackCursor&)> IsActive;
    std::function<TrackCursor(const TrackCursor&, int)> GetTrackWithOffset;
    std::function<void(const TrackCursor&)> OnTrackLoaded;
};

Q_DECLARE_METATYPE(TrackCursor);

enum class ExportTrackMetadataResult {
    Succeeded,
    Failed,
    Skipped,
};

Q_DECLARE_METATYPE(TrackPointer);
