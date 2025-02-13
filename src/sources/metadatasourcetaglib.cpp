#include "sources/metadatasourcetaglib.h"

#include <taglib/opusfile.h>
#include <taglib/tfilestream.h>
#include <taglib/vorbisfile.h>

#include <QFile>
#include <QFileInfo>
#include <memory>

#include "track/taglib/trackmetadata.h"
#include "util/logger.h"

namespace mixxx {

namespace {

Logger kLogger("MetadataSourceTagLib");

// TODO(uklotzde): Add a configurable option in the user settings
const bool kExportTrackMetadataIntoTemporaryFile = true;

// Appended to the original file name of the temporary file used for writing
const QString kSafelyWritableTempFileSuffix = QStringLiteral("_temp");

// Appended to the original file name for renaming and before deleting this
// file. Should not be longer than kSafelyWritableTempFileSuffix to avoid
// potential failures caused by exceeded path length.
const QString kSafelyWritableOrigFileSuffix = QStringLiteral("_orig");

// Workaround for missing functionality in TagLib 1.11.x that
// doesn't support to read text chunks from AIFF files.
// See also:
// http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/AIFF.html
// http://paulbourke.net/dataformats/audio/
//
//
class AiffFile : public TagLib::RIFF::AIFF::File {
  public:
    explicit AiffFile(TagLib::FileName fileName)
            : TagLib::RIFF::AIFF::File(fileName) {
    }

    explicit AiffFile(TagLib::IOStream* stream)
            : TagLib::RIFF::AIFF::File(stream) {
    }

    bool importTrackMetadataFromTextChunks(TrackMetadata* pTrackMetadata) /*non-const*/ {
        if (pTrackMetadata == nullptr) {
            return false; // nothing to do
        }
        bool imported = false;
        for (unsigned int i = 0; i < chunkCount(); ++i) {
            const TagLib::ByteVector chunkId(TagLib::RIFF::AIFF::File::chunkName(i));
            if (chunkId == "NAME") {
                pTrackMetadata->refTrackInfo().setTitle(decodeChunkText(
                        TagLib::RIFF::AIFF::File::chunkData(i)));
                imported = true;
            } else if (chunkId == "AUTH") {
                pTrackMetadata->refTrackInfo().setArtist(decodeChunkText(
                        TagLib::RIFF::AIFF::File::chunkData(i)));
                imported = true;
            } else if (chunkId == "ANNO") {
                pTrackMetadata->refTrackInfo().setComment(decodeChunkText(
                        TagLib::RIFF::AIFF::File::chunkData(i)));
                imported = true;
            }
        }
        return imported;
    }

  private:
    // From the specs: 13. TEXT CHUNKS - NAME, AUTHOR, COPYRIGHT, ANNOTATION
    // "text: contains pure ASCII characters"
    static QString decodeChunkText(const TagLib::ByteVector& chunkData) {
        return QString::fromLatin1(chunkData.data(), chunkData.size());
    }
};

} // anonymous namespace

std::pair<MetadataSourceTagLib::ImportResult, QDateTime>
MetadataSourceTagLib::afterImport(ImportResult importResult) const {
    return std::make_pair(importResult,
            MetadataSource::getFileSynchronizedAt(QFileInfo(m_fileName)));
}

std::pair<MetadataSourceTagLib::ExportResult, QDateTime>
MetadataSourceTagLib::afterExport(ExportResult exportResult) const {
    return std::make_pair(exportResult,
            MetadataSource::getFileSynchronizedAt(QFileInfo(m_fileName)));
}

std::pair<MetadataSource::ImportResult, QDateTime>
MetadataSourceTagLib::importTrackMetadataAndCoverImage(
        TrackMetadata* pTrackMetadata,
        QImage* pCoverImage) const {
    VERIFY_OR_DEBUG_ASSERT(pTrackMetadata || pCoverImage) {
        kLogger.warning()
                << "Nothing to import"
                << "from file" << m_fileName
                << "with type" << m_fileType;
        return afterImport(ImportResult::Unavailable);
    }
    if (kLogger.traceEnabled()) {
        kLogger.trace() << "Importing"
                        << ((pTrackMetadata && pCoverImage) ? "track metadata and cover art" : (pTrackMetadata ? "track metadata" : "cover art"))
                        << "from file" << m_fileName
                        << "with type" << m_fileType;
    }

    // Rationale: If a file contains different types of tags only
    // a single type of tag will be read. Tag types are read in a
    // fixed order. Both track metadata and cover art will be read
    // from the same tag types. Only the first available tag type
    // is read and data in subsequent tags is ignored.

    TagLib::FileStream fileStream(TAGLIB_FILENAME_FROM_QSTRING(m_fileName), true);
    switch (m_fileType) {
    case taglib::FileType::MP3: {
        TagLib::MPEG::File file(&fileStream, TagLib::ID3v2::FrameFactory::instance());
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        // ID3v2 tag takes precedence over APE tag
        if (taglib::hasID3v2Tag(file)) {
            const TagLib::ID3v2::Tag* pTag = file.ID3v2Tag();
            DEBUG_ASSERT(pTag);
            taglib::id3v2::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::id3v2::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        } else if (taglib::hasAPETag(file)) {
            const TagLib::APE::Tag* pTag = file.APETag();
            DEBUG_ASSERT(pTag);
            taglib::ape::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::ape::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        } else if (taglib::hasID3v1Tag(file)) {
            // Note (TagLib 1.1.11): TagLib::MPEG::File::tag() returns a
            // valid pointer even if neither an ID3v2 nor an ID3v1 tag is
            // present!
            // See also: https://bugs.launchpad.net/mixxx/+bug/1865957
            const TagLib::Tag* pTag = file.tag();
            if (pTag) {
                taglib::importTrackMetadataFromTag(pTrackMetadata, *pTag);
                return afterImport(ImportResult::Succeeded);
            }
        }
        break;
    }
    case taglib::FileType::MP4: {
        TagLib::MP4::File file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        if (taglib::hasMP4Tag(file)) {
            const TagLib::MP4::Tag* pTag = file.tag();
            DEBUG_ASSERT(pTag);
            taglib::mp4::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::mp4::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::FLAC: {
        TagLib::FLAC::File file(&fileStream, TagLib::ID3v2::FrameFactory::instance());
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        bool importSucceeded = false;
        bool coverImageImported = false;
        // VorbisComment tag takes precedence over ID3v2 tag
        if (taglib::hasXiphComment(file)) {
            TagLib::Ogg::XiphComment* pTag = file.xiphComment();
            DEBUG_ASSERT(pTag);
            taglib::xiph::importTrackMetadataFromTag(pTrackMetadata, *pTag, taglib::FileType::FLAC);
            coverImageImported = taglib::xiph::importCoverImageFromTag(pCoverImage, *pTag);
            importSucceeded = true;
        } else if (taglib::hasID3v2Tag(file)) {
            const TagLib::ID3v2::Tag* pTag = file.ID3v2Tag();
            DEBUG_ASSERT(pTag);
            taglib::id3v2::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            coverImageImported = taglib::id3v2::importCoverImageFromTag(pCoverImage, *pTag);
            importSucceeded = true;
        }
        // Only import cover images from picture list as a fallback if file tags
        // are available but no cover image has been found yet! Otherwise until
        // file tags have been successfully imported once, Mixxx would retry to
        // import the missing file tags over and over again when loading the
        // cover image.
        if (pCoverImage && // cover image is requested
                importSucceeded &&
                !coverImageImported) { // no cover image found in file tags
            // Read cover art directly from the file as a fallback
            *pCoverImage = taglib::xiph::importCoverImageFromPictureList(file.pictureList());
        }
        if (importSucceeded) {
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::OGG: {
        TagLib::Ogg::Vorbis::File file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        TagLib::Ogg::XiphComment* pTag = file.tag();
        if (pTag) {
            taglib::xiph::importTrackMetadataFromTag(pTrackMetadata, *pTag, taglib::FileType::OGG);
            taglib::xiph::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::OPUS: {
        TagLib::Ogg::Opus::File file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        TagLib::Ogg::XiphComment* pTag = file.tag();
        if (pTag) {
            taglib::xiph::importTrackMetadataFromTag(pTrackMetadata, *pTag, taglib::FileType::OPUS);
            taglib::xiph::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::WV: {
        TagLib::WavPack::File file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        if (taglib::hasAPETag(file)) {
            const TagLib::APE::Tag* pTag = file.APETag();
            DEBUG_ASSERT(pTag);
            taglib::ape::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::ape::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::WAV: {
        TagLib::RIFF::WAV::File file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        if (taglib::hasID3v2Tag(file)) {
            const TagLib::ID3v2::Tag* pTag = file.ID3v2Tag();
            DEBUG_ASSERT(pTag);
            taglib::id3v2::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::id3v2::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        } else if (file.hasInfoTag()) {
            const TagLib::RIFF::Info::Tag* pTag = file.InfoTag();
            DEBUG_ASSERT(pTag);
            taglib::riff::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    case taglib::FileType::AIFF: {
        AiffFile file(&fileStream);
        if (!taglib::readAudioPropertiesFromFile(pTrackMetadata, file)) {
            break;
        }
        if (taglib::hasID3v2Tag(file)) {
            const TagLib::ID3v2::Tag* pTag = file.tag();
            DEBUG_ASSERT(pTag);
            taglib::id3v2::importTrackMetadataFromTag(pTrackMetadata, *pTag);
            taglib::id3v2::importCoverImageFromTag(pCoverImage, *pTag);
            return afterImport(ImportResult::Succeeded);
        } else if (file.importTrackMetadataFromTextChunks(pTrackMetadata)) {
            return afterImport(ImportResult::Succeeded);
        }
        break;
    }
    default:
        kLogger.warning()
                << "Cannot import track metadata"
                << "from file" << m_fileName
                << "with unknown or unsupported type" << m_fileType;
        return afterImport(ImportResult::Failed);
    }

    kLogger.info()
            << "No track metadata or cover art found"
            << "in file" << m_fileName
            << "with type" << m_fileType;
    return afterImport(ImportResult::Unavailable);
}

namespace {

// Encapsulates subtle differences between TagLib::File::save()
// and variants of this function in derived subclasses.
class TagSaver {
  public:
    virtual ~TagSaver() = default;

    virtual bool hasModifiedTags() const = 0;

    virtual bool saveModifiedTags() = 0;
};

class MpegTagSaver : public TagSaver {
  public:
    MpegTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTagsBitmask(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~MpegTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTagsBitmask != TagLib::MPEG::File::NoTags;
    }

    bool saveModifiedTags() override {
        // NOTE(uklotzde, 2016-08-28): Only save the tags that have
        // actually been modified! Otherwise TagLib 1.11 adds unwanted
        // ID3v1 tags, even if the file does not already contain those
        // legacy tags.
        return m_file.save(m_modifiedTagsBitmask);
    }

  private:
    static int exportTrackMetadata(TagLib::MPEG::File* pFile, const TrackMetadata& trackMetadata) {
        int modifiedTagsBitmask = TagLib::MPEG::File::NoTags;
        if (pFile->isOpen()) {
            TagLib::ID3v2::Tag* pID3v2Tag = nullptr;
            if (taglib::hasAPETag(*pFile)) {
                if (taglib::ape::exportTrackMetadataIntoTag(pFile->APETag(), trackMetadata)) {
                    modifiedTagsBitmask |= TagLib::MPEG::File::APE;
                }
                // Only write ID3v2 tag if it already exists.
                if (pFile->hasID3v2Tag()) {
                    pID3v2Tag = pFile->ID3v2Tag(false);
                    DEBUG_ASSERT(pID3v2Tag);
                }
            } else {
                // Get or create ID3v2 tag
                pID3v2Tag = pFile->ID3v2Tag(true);
                DEBUG_ASSERT(pID3v2Tag);
            }
            if (taglib::id3v2::exportTrackMetadataIntoTag(pID3v2Tag, trackMetadata)) {
                modifiedTagsBitmask |= TagLib::MPEG::File::ID3v2;
            }
        }
        return modifiedTagsBitmask;
    }

    TagLib::MPEG::File m_file;
    int m_modifiedTagsBitmask;
};

class Mp4TagSaver : public TagSaver {
  public:
    Mp4TagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~Mp4TagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::MP4::File* pFile, const TrackMetadata& trackMetadata) {
        return pFile->isOpen() && taglib::mp4::exportTrackMetadataIntoTag(pFile->tag(), trackMetadata);
    }

    TagLib::MP4::File m_file;
    bool m_modifiedTags;
};

class FlacTagSaver : public TagSaver {
  public:
    FlacTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~FlacTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::FLAC::File* pFile, const TrackMetadata& trackMetadata) {
        bool modifiedTags = false;
        if (pFile->isOpen()) {
            TagLib::Ogg::XiphComment* pXiphComment = nullptr;
            if (taglib::hasID3v2Tag(*pFile)) {
                modifiedTags |= taglib::id3v2::exportTrackMetadataIntoTag(pFile->ID3v2Tag(), trackMetadata);
                // Only write VorbisComment tag if it already exists
                if (taglib::hasXiphComment(*pFile)) {
                    pXiphComment = pFile->xiphComment(false);
                    DEBUG_ASSERT(pXiphComment);
                }
            } else {
                // Get or create VorbisComment tag
                pXiphComment = pFile->xiphComment(true);
                DEBUG_ASSERT(pXiphComment);
            }
            modifiedTags |= taglib::xiph::exportTrackMetadataIntoTag(
                    pXiphComment, trackMetadata, taglib::FileType::FLAC);
        }
        return modifiedTags;
    }

    TagLib::FLAC::File m_file;
    bool m_modifiedTags;
};

class OggTagSaver : public TagSaver {
  public:
    OggTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~OggTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::Ogg::Vorbis::File* pFile,
            const TrackMetadata& trackMetadata) {
#if (TAGLIB_MAJOR_VERSION == 1) && (TAGLIB_MINOR_VERSION == 11) && \
        (TAGLIB_PATCH_VERSION == 1)
        // TagLib 1.11.1 suffers from a serious bug that corrupts OGG files
        // when writing tags: https://github.com/taglib/taglib/issues/864
        // Launchpad issue: https://bugs.launchpad.net/mixxx/+bug/1833190
        Q_UNUSED(pFile);
        Q_UNUSED(trackMetadata);
        kLogger.warning() << "Skipping export of metadata into Ogg file due to "
                             "serious bug in TagLib 1.11.1 "
                             "(https://github.com/taglib/taglib/issues/864)";
        return false;
#else
        return pFile->isOpen() &&
                taglib::xiph::exportTrackMetadataIntoTag(
                        pFile->tag(), trackMetadata, taglib::FileType::OGG);
#endif
    }

    TagLib::Ogg::Vorbis::File m_file;
    bool m_modifiedTags;
};

class OpusTagSaver : public TagSaver {
  public:
    OpusTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~OpusTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::Ogg::Opus::File* pFile,
            const TrackMetadata& trackMetadata) {
        return pFile->isOpen() &&
                taglib::xiph::exportTrackMetadataIntoTag(
                        pFile->tag(), trackMetadata, taglib::FileType::OPUS);
    }

    TagLib::Ogg::Opus::File m_file;
    bool m_modifiedTags;
};

class WavPackTagSaver : public TagSaver {
  public:
    WavPackTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~WavPackTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::WavPack::File* pFile, const TrackMetadata& trackMetadata) {
        return pFile->isOpen() && taglib::ape::exportTrackMetadataIntoTag(pFile->APETag(true), trackMetadata);
    }

    TagLib::WavPack::File m_file;
    bool m_modifiedTags;
};

bool exportTrackMetadataIntoRIFFTag(TagLib::RIFF::Info::Tag* pTag, const TrackMetadata& trackMetadata) {
    if (!pTag) {
        return false;
    }

    taglib::exportTrackMetadataIntoTag(pTag, trackMetadata, taglib::WriteTagFlag::OmitNone);

    return true;
}

class WavTagSaver : public TagSaver {
  public:
    WavTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~WavTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::RIFF::WAV::File* pFile, const TrackMetadata& trackMetadata) {
        bool modifiedTags = false;
        if (pFile->isOpen()) {
            TagLib::RIFF::Info::Tag* pInfoTag = nullptr;
            // Write into all available tags
            if (pFile->hasID3v2Tag()) {
                modifiedTags |= taglib::id3v2::exportTrackMetadataIntoTag(
                        pFile->ID3v2Tag(), trackMetadata);
                // Only write Info tag if it already exists
                if (pFile->hasInfoTag()) {
                    pInfoTag = pFile->InfoTag();
                    DEBUG_ASSERT(pInfoTag);
                }
            } else {
                // Get or create Info tag
                pInfoTag = pFile->InfoTag();
                DEBUG_ASSERT(pInfoTag);
            }
            modifiedTags |= exportTrackMetadataIntoRIFFTag(pInfoTag, trackMetadata);
        }
        return modifiedTags;
    }

    TagLib::RIFF::WAV::File m_file;
    bool m_modifiedTags;
};

class AiffTagSaver : public TagSaver {
  public:
    AiffTagSaver(const QString& fileName, const TrackMetadata& trackMetadata)
            : m_file(TAGLIB_FILENAME_FROM_QSTRING(fileName)),
              m_modifiedTags(exportTrackMetadata(&m_file, trackMetadata)) {
    }
    ~AiffTagSaver() override = default;

    bool hasModifiedTags() const override {
        return m_modifiedTags;
    }

    bool saveModifiedTags() override {
        return m_file.save();
    }

  private:
    static bool exportTrackMetadata(TagLib::RIFF::AIFF::File* pFile, const TrackMetadata& trackMetadata) {
        return pFile->isOpen() && taglib::id3v2::exportTrackMetadataIntoTag(pFile->tag(), trackMetadata);
    }

    TagLib::RIFF::AIFF::File m_file;
    bool m_modifiedTags;
};

/**
 * When writing the tags in-place directly into the original file
 * an intermediate failure might corrupt this precious file. For
 * example this might occur if the application crashes or is quit
 * unexpectedly, if the original file becomes unavailable while
 * writing by disconnecting a drive, if the file system is running
 * out of free space, or if an unexpected driver or hardware failure
 * occurs.
 *
 * To reduce the risk of corrupting the original file all write
 * operations are performed on a temporary file that is created
 * as an exact copy of the original file. Only after all write
 * operations have finished successfully the original file is
 * replaced with the temporary file.
 */
class SafelyWritableFile final {
  public:
    SafelyWritableFile(QString origFileName, bool useTemporaryFile) {
        // Both file names remain uninitialized until all prerequisite operations
        // in the constructor have been completed successfully. Otherwise failure
        // to create the temporary file will not be handled correctly!
        // See also: https://bugs.launchpad.net/mixxx/+bug/1815305
        DEBUG_ASSERT(m_origFileName.isNull());
        DEBUG_ASSERT(m_tempFileName.isNull());
        if (!QFileInfo(origFileName).isWritable()) {
            kLogger.warning()
                    << "Failed to prepare file for writing:"
                    << origFileName
                    << "is not writable.";
            // Abort constructor
            return;
        }
        if (useTemporaryFile) {
            QString tempFileName = origFileName + kSafelyWritableTempFileSuffix;
            QFile origFile(origFileName);
            if (!origFile.copy(tempFileName)) {
                kLogger.warning()
                        << origFile.errorString()
                        << "- Failed to clone original into temporary file before writing:"
                        << origFileName
                        << "->"
                        << tempFileName;
                // Abort constructor
                return;
            }
            QFile tempFile(tempFileName);
            DEBUG_ASSERT(tempFile.exists());
            // Both file sizes are expected to be equal after successfully
            // copying the file contents.
            VERIFY_OR_DEBUG_ASSERT(origFile.size() == tempFile.size()) {
                kLogger.warning()
                        << "Failed to verify size after cloning original into temporary file before writing:"
                        << origFile.size()
                        << "<>"
                        << tempFile.size();
                // Cleanup
                if (tempFile.exists() && !tempFile.remove()) {
                    kLogger.warning()
                            << tempFile.errorString()
                            << "- Failed to remove temporary file:"
                            << tempFileName;
                }
                // Abort constructor
                return;
            }
            // Successfully cloned original into temporary file for writing - finish initialization
            m_origFileName = std::move(origFileName);
            m_tempFileName = std::move(tempFileName);
        } else {
            // Directly write into original file - finish initialization
            m_origFileName = std::move(origFileName);
            DEBUG_ASSERT(m_tempFileName.isNull());
        }
    }
    ~SafelyWritableFile() {
        cancel();
    }

    const QString& fileName() const {
        if (m_tempFileName.isNull()) {
            // If m_tempFileName has not been initialized then no temporary
            // copy was requested in the constructor.
            return m_origFileName;
        } else {
            return m_tempFileName;
        }
    }

    bool isReady() const {
        return !fileName().isEmpty();
    }

    bool commit() {
        if (m_tempFileName.isNull()) {
            return true; // nothing to do
        }
        QFile newFile(m_tempFileName);
        if (!newFile.exists()) {
            kLogger.warning()
                    << "Temporary file not found:"
                    << newFile.fileName();
            return false;
        }
        QFile oldFile(m_origFileName);
        if (oldFile.exists()) {
            QString backupFileName = m_origFileName + kSafelyWritableOrigFileSuffix;
            DEBUG_ASSERT(!QFile::exists(backupFileName)); // very unlikely, otherwise renaming fails
            if (!oldFile.rename(backupFileName)) {
                kLogger.critical()
                        << oldFile.errorString()
                        << "- Failed to rename the original file for backup before writing:"
                        << oldFile.fileName()
                        << "->"
                        << backupFileName;
                return false;
            }
        }
        DEBUG_ASSERT(!QFile::exists(m_origFileName));
        if (!newFile.rename(m_origFileName)) {
            kLogger.critical()
                    << newFile.errorString()
                    << "- Failed to rename temporary file after writing:"
                    << newFile.fileName()
                    << "->"
                    << m_origFileName;
            if (oldFile.exists()) {
                // Try to restore the original file
                if (!oldFile.rename(m_origFileName)) {
                    // Undo operation failed
                    kLogger.warning()
                            << oldFile.errorString()
                            << "- Both the original and the temporary file are still available:"
                            << oldFile.fileName()
                            << newFile.fileName();
                }
                return false;
            }
        }
        if (oldFile.exists()) {
            if (!oldFile.remove()) {
                kLogger.warning()
                        << oldFile.errorString()
                        << "- Failed to remove backup file after writing:"
                        << oldFile.fileName();
                return false;
            }
        }
        // Prevent any further interaction and file access
        m_origFileName = QString();
        m_tempFileName = QString();
        return true;
    }

    void cancel() {
        if (m_tempFileName.isNull()) {
            return; // nothing to do
        }
        QFile tempFile(m_tempFileName);
        if (tempFile.exists() && !tempFile.remove()) {
            kLogger.warning()
                    << tempFile.errorString()
                    << "- Failed to remove temporary file:"
                    << m_tempFileName;
        }
        // Prevent any further interaction and file access
        m_origFileName = QString();
        m_tempFileName = QString();
    }

  private:
    QString m_origFileName;
    QString m_tempFileName;
};

} // anonymous namespace

std::pair<MetadataSource::ExportResult, QDateTime>
MetadataSourceTagLib::exportTrackMetadata(
        const TrackMetadata& trackMetadata) const {
    // NOTE(uklotzde): Log unconditionally (with debug level) to
    // identify files in the log file that might have caused a
    // crash while exporting metadata.
    kLogger.debug() << "Exporting track metadata"
                    << "into file" << m_fileName
                    << "with type" << m_fileType;

    SafelyWritableFile safelyWritableFile(m_fileName, kExportTrackMetadataIntoTemporaryFile);
    if (!safelyWritableFile.isReady()) {
        kLogger.warning()
                << "Unable to export track metadata into file"
                << m_fileName
                << "- Please check file permissions and storage space";
        return afterExport(ExportResult::Failed);
    }

    std::unique_ptr<TagSaver> pTagSaver;
    switch (m_fileType) {
    case taglib::FileType::MP3: {
        pTagSaver = std::make_unique<MpegTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::MP4: {
        pTagSaver = std::make_unique<Mp4TagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::FLAC: {
        pTagSaver = std::make_unique<FlacTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::OGG: {
        pTagSaver = std::make_unique<OggTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::OPUS: {
        pTagSaver = std::make_unique<OpusTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::WV: {
        pTagSaver = std::make_unique<WavPackTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::WAV: {
        pTagSaver = std::make_unique<WavTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    case taglib::FileType::AIFF: {
        pTagSaver = std::make_unique<AiffTagSaver>(safelyWritableFile.fileName(), trackMetadata);
        break;
    }
    default:
        kLogger.debug()
                << "Cannot export track metadata"
                << "into file" << m_fileName
                << "with unknown or unsupported type"
                << m_fileType;
        return afterExport(ExportResult::Unsupported);
    }

    if (pTagSaver->hasModifiedTags()) {
        if (pTagSaver->saveModifiedTags()) {
            // Close all file handles after modified tags have been saved into the temporary file!
            pTagSaver.reset();
            // Now we can safely replace the original file with the temporary file
            if (safelyWritableFile.commit()) {
                return afterExport(ExportResult::Succeeded);
            }
        }
        kLogger.warning() << "Failed to save tags of file" << m_fileName;
    } else {
        kLogger.warning() << "Failed to modify tags of file" << m_fileName;
    }
    return afterExport(ExportResult::Failed);
}

} // namespace mixxx
