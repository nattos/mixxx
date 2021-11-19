#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "library/coverartcache.h"
#include "mixer/playermanager.h"
#include "util/assert.h"
#include "util/compatibility/qatomic.h"
#include "util/defs.h"
#include "util/logger.h"
#include "util/sleepableqthread.h"
#include "track/track.h"


namespace {
    constexpr int kMaxThumbnailSize = 128;
    MPMediaItemArtwork* g_pCachedHandler = nil;
    NSImage* g_pCachedImage = nil;
    CoverInfo g_cachedCoverInfo;
    CGSize g_cachedImageSize;
} // namespace

void UpdateNowPlayingInfo(bool loaded, bool playing, const TrackPointer& pTrack, double playPositionPercent) {
    MPNowPlayingInfoCenter* nowPlayingInfoCenter = [MPNowPlayingInfoCenter defaultCenter];

    NSMutableDictionary* songInfo = [NSMutableDictionary dictionaryWithDictionary:@{
        MPNowPlayingInfoPropertyMediaType: @(MPNowPlayingInfoMediaTypeAudio),
        MPNowPlayingInfoPropertyPlaybackRate: (loaded && playing) ? @1 : @0,
        MPNowPlayingInfoPropertyIsLiveStream: @0,
    }];

    if (pTrack) {
        double trackDuration = pTrack->getDuration();
        songInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(playPositionPercent * trackDuration);
        songInfo[MPMediaItemPropertyPlaybackDuration] = @(trackDuration);
        songInfo[MPMediaItemPropertyArtist] = pTrack->getArtist().toNSString();
        songInfo[MPMediaItemPropertyTitle] = pTrack->getTitle().toNSString();
        songInfo[MPMediaItemPropertyAlbumTitle] = pTrack->getAlbum().toNSString();

        if (@available(macOS 10.13.2, *)) {
            CoverInfo coverInfo = pTrack->getCoverInfoWithLocation();
            if (g_pCachedImage && g_cachedCoverInfo == coverInfo) {
                // Reuse g_pCachedHandler.
            } else if (coverInfo.hasImage()) {
                TrackWeakPointer pWeakTrack = pTrack;
                CoverArtCache* pCache = CoverArtCache::instance();
                QPixmap pixmap = pCache->tryLoadCover(nullptr, coverInfo, kMaxThumbnailSize, CoverArtCache::Loading::NoSignal);
                if (!pixmap.isNull()) {
                    g_pCachedHandler = [[MPMediaItemArtwork alloc] initWithBoundsSize:CGSizeMake(kMaxThumbnailSize, kMaxThumbnailSize) requestHandler: ^NSImage*(CGSize requestedSize) {
                        TrackPointer pInnerTrack = pWeakTrack.lock();
                        if (!pInnerTrack) {
                            return nil;
                        }
                        CoverInfo coverInfo = pInnerTrack->getCoverInfoWithLocation();
                        if (g_pCachedImage &&
                                g_cachedImageSize.width == requestedSize.width &&
                                g_cachedImageSize.height == requestedSize.height &&
                                g_cachedCoverInfo == coverInfo) {
                            return g_pCachedImage;
                        }

                        CoverArtCache* pInnerCache = CoverArtCache::instance();
                        QPixmap innerPixmap = pInnerCache->tryLoadCover(nullptr, coverInfo, kMaxThumbnailSize, CoverArtCache::Loading::NoSignal);
                        if (innerPixmap.isNull()) {
                            return nil;
                        }
                        CGImageRef imageRef = innerPixmap.toImage().toCGImage();
                        NSImage* pImage = [[NSImage alloc] initWithCGImage:imageRef size:requestedSize];
                        g_pCachedImage = pImage;
                        g_cachedImageSize = requestedSize;
                        g_cachedCoverInfo = coverInfo;
                        return pImage;
                    }];
                } else {
                    g_pCachedHandler = nil;
                }
            } else {
                g_pCachedHandler = nil;
            }
            songInfo[MPMediaItemPropertyArtwork] = g_pCachedHandler;
        }
    }

    nowPlayingInfoCenter.nowPlayingInfo = songInfo;
    nowPlayingInfoCenter.playbackState = !loaded ? MPNowPlayingPlaybackStateStopped : playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
}

void DoNowPlayingStuff(PlayerManager* pPlayerManager) {
    MPNowPlayingInfoCenter* infoCenter = [MPNowPlayingInfoCenter defaultCenter];
    infoCenter.nowPlayingInfo = @{
        MPNowPlayingInfoPropertyPlaybackRate: @1.0,
    };

    MPRemoteCommandCenter* commandCenter = [MPRemoteCommandCenter sharedCommandCenter];

    [commandCenter.togglePlayPauseCommand addTargetWithHandler:^(MPRemoteCommandEvent*) {
        pPlayerManager->slotPlayPlayOrPause(1.);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.togglePlayPauseCommand.enabled = YES;

    [commandCenter.playCommand addTargetWithHandler:^(MPRemoteCommandEvent*) {
        pPlayerManager->slotPlayPlayOrPause(1.);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.playCommand.enabled = YES;

    [commandCenter.pauseCommand addTargetWithHandler:^(MPRemoteCommandEvent*) {
        pPlayerManager->slotPlayPause(1.);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.pauseCommand.enabled = YES;

    [commandCenter.nextTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent*) {
        pPlayerManager->slotPlayNextTrack(1.);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.nextTrackCommand.enabled = YES;

    [commandCenter.previousTrackCommand addTargetWithHandler:^(MPRemoteCommandEvent*) {
        pPlayerManager->slotPlayPrevTrack(1.);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.previousTrackCommand.enabled = YES;

    [commandCenter.changePlaybackPositionCommand addTargetWithHandler:^(MPRemoteCommandEvent* rawEvent) {
        MPChangePlaybackPositionCommandEvent* event = (MPChangePlaybackPositionCommandEvent*)rawEvent;
        pPlayerManager->slotPlaySeekToTime(event.positionTime);
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.changePlaybackPositionCommand.enabled = YES;

    UpdateNowPlayingInfo(false, false, nullptr, 0.0);
}






