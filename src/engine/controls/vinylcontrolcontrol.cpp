#include "engine/controls/vinylcontrolcontrol.h"

#include "moc_vinylcontrolcontrol.cpp"
#include "track/track.h"
#include "vinylcontrol/vinylcontrol.h"
#include "engine/enginemaster.h"

VinylControlControl::VinylControlControl(const QString& group, UserSettingsPointer pConfig)
        : EngineControl(group, pConfig),
          m_bSeekRequested(false) {
    m_pControlVinylStatus = new ControlObject(ConfigKey(group, "vinylcontrol_status"));
    m_pControlVinylSpeedType = new ControlObject(ConfigKey(group, "vinylcontrol_speed_type"));

    //Convert the ConfigKey's value into a double for the CO (for fast reads).
    QString strVinylSpeedType = pConfig->getValueString(ConfigKey(group,
                                                      "vinylcontrol_speed_type"));
    if (strVinylSpeedType == MIXXX_VINYL_SPEED_33) {
        m_pControlVinylSpeedType->set(MIXXX_VINYL_SPEED_33_NUM);
    } else if (strVinylSpeedType == MIXXX_VINYL_SPEED_45) {
        m_pControlVinylSpeedType->set(MIXXX_VINYL_SPEED_45_NUM);
    } else {
        m_pControlVinylSpeedType->set(MIXXX_VINYL_SPEED_33_NUM);
    }

    m_pControlVinylSeek = new ControlObject(ConfigKey(group, "vinylcontrol_seek"));
    connect(m_pControlVinylSeek, &ControlObject::valueChanged,
            this, &VinylControlControl::slotControlVinylSeek,
            Qt::DirectConnection);

    m_pControlVinylRate = new ControlObject(ConfigKey(group, "vinylcontrol_rate"));
    m_pControlVinylScratching = new ControlPushButton(ConfigKey(group, "vinylcontrol_scratching"));
    m_pControlVinylScratching->set(0);
    m_pControlVinylScratching->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlVinylEnabled = new ControlPushButton(ConfigKey(group, "vinylcontrol_enabled"));
    m_pControlVinylEnabled->set(0);
    m_pControlVinylEnabled->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlVinylWantEnabled = new ControlPushButton(ConfigKey(group, "vinylcontrol_wantenabled"));
    m_pControlVinylWantEnabled->set(0);
    m_pControlVinylWantEnabled->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlVinylMode = new ControlPushButton(ConfigKey(group, "vinylcontrol_mode"));
    m_pControlVinylMode->setStates(3);
    m_pControlVinylMode->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlVinylCueing = new ControlPushButton(ConfigKey(group, "vinylcontrol_cueing"));
    m_pControlVinylCueing->setStates(3);
    m_pControlVinylCueing->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlVinylSignalEnabled = new ControlPushButton(ConfigKey(group, "vinylcontrol_signal_enabled"));
    m_pControlVinylSignalEnabled->set(1);
    m_pControlVinylSignalEnabled->setButtonMode(ControlPushButton::TOGGLE);

    m_pPlayEnabled = new ControlProxy(group, "play", this);
}

VinylControlControl::~VinylControlControl() {
    delete m_pControlVinylRate;
    delete m_pControlVinylSignalEnabled;
    delete m_pControlVinylCueing;
    delete m_pControlVinylMode;
    delete m_pControlVinylWantEnabled;
    delete m_pControlVinylEnabled;
    delete m_pControlVinylScratching;
    delete m_pControlVinylSeek;
    delete m_pControlVinylSpeedType;
    delete m_pControlVinylStatus;
}

void VinylControlControl::trackLoaded(TrackPointer pNewTrack) {
    m_pTrack = pNewTrack;
}

void VinylControlControl::notifySeekQueued() {
    // m_bRequested is set and unset in a single execution path,
    // so there are no issues with signals/slots causing timing
    // issues.
    if (m_pControlVinylMode->get() == MIXXX_VCMODE_ABSOLUTE &&
        m_pPlayEnabled->get() > 0.0 &&
        !m_bSeekRequested) {
        m_pControlVinylMode->set(MIXXX_VCMODE_RELATIVE);
    }
}

void VinylControlControl::slotControlVinylSeek(double fractionalPos) {
    // Prevent NaN's from sneaking into the engine.
    if (util_isnan(fractionalPos)) {
        return;
    }

    // Do nothing if no track is loaded.
    TrackPointer pTrack = m_pTrack;
    FrameInfo info = frameInfo();
    if (!pTrack || !info.trackEndPosition.isValid()) {
        return;
    }

    getEngineMaster()->requestAwake();

    const auto newPlayPos = mixxx::audio::kStartFramePos +
            (info.trackEndPosition - mixxx::audio::kStartFramePos) * fractionalPos;

    if (m_pControlVinylEnabled->get() > 0.0 && m_pControlVinylMode->get() == MIXXX_VCMODE_RELATIVE) {
        int cuemode = (int)m_pControlVinylCueing->get();

        //if in preroll, always seek
        if (newPlayPos < mixxx::audio::kStartFramePos) {
            seekExact(newPlayPos);
            return;
        }

        switch (cuemode) {
        case MIXXX_RELATIVE_CUE_OFF:
            return; // If off, do nothing.
        case MIXXX_RELATIVE_CUE_ONECUE: {
            //if onecue, just seek to the regular cue
            const mixxx::audio::FramePos mainCuePosition = pTrack->getMainCuePosition();
            if (mainCuePosition.isValid()) {
                seekExact(mainCuePosition);
            }
            return;
        }
        case MIXXX_RELATIVE_CUE_HOTCUE:
            // Continue processing in this function.
            break;
        default:
            qWarning() << "Invalid vinyl cue setting";
            return;
        }

        mixxx::audio::FrameDiff_t shortestDistance = 0;
        mixxx::audio::FramePos nearestPlayPos;

        const QList<CuePointer> cuePoints(pTrack->getCuePoints());
        QListIterator<CuePointer> it(cuePoints);
        while (it.hasNext()) {
            CuePointer pCue(it.next());
            if (pCue->getType() != mixxx::CueType::HotCue || pCue->getHotCue() == -1) {
                continue;
            }

            const mixxx::audio::FramePos cuePosition = pCue->getPosition();
            if (!cuePosition.isValid()) {
                continue;
            }
            // pick cues closest to newPlayPos
            if (!nearestPlayPos.isValid() || (fabs(newPlayPos - cuePosition) < shortestDistance)) {
                nearestPlayPos = cuePosition;
                shortestDistance = fabs(newPlayPos - cuePosition);
            }
        }

        if (!nearestPlayPos.isValid()) {
            if (newPlayPos >= mixxx::audio::kStartFramePos) {
                //never found an appropriate cue, so don't seek?
                return;
            }
            //if negative, allow a seek by falling down to the bottom
        } else {
            m_bSeekRequested = true;
            seekExact(nearestPlayPos);
            m_bSeekRequested = false;
            return;
        }
    }

    // Just seek where it wanted to originally.
    m_bSeekRequested = true;
    seekExact(newPlayPos);
    m_bSeekRequested = false;
}

bool VinylControlControl::isEnabled()
{
    return m_pControlVinylEnabled->toBool();
}

bool VinylControlControl::isScratching()
{
    return m_pControlVinylScratching->toBool();
}
