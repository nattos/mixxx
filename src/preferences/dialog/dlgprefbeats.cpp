#include "preferences/dialog/dlgprefbeats.h"

#include "analyzer/analyzerbeats.h"
#include "control/controlobject.h"
#include "defs_urls.h"
#include "moc_dlgprefbeats.cpp"

namespace {
    struct BpmRange {
        int Min;
        int Max;
    };
    const BpmRange kPreferredBpmRange[] = {
        { 70, 180 },
        { 48, 95 },
        { 58, 115 },
        { 68, 135 },
        { 78, 155 },
        { 88, 175 },
        { 98, 195 },
        { 108, 215 },
        { 118, 235 },
        { 128, 255 },
    };
    const int kPreferredBpmRangeDefaultIndex = 0;
} // namespace

DlgPrefBeats::DlgPrefBeats(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_bpmSettings(pConfig),
          m_bAnalyzerEnabled(m_bpmSettings.getBpmDetectionEnabledDefault()),
          m_bFixedTempoEnabled(m_bpmSettings.getFixedTempoAssumptionDefault()),
          m_iPreferredMinBpm(m_bpmSettings.getBpmDetectionPreferredMinBpmDefault()),
          m_iPreferredMaxBpm(m_bpmSettings.getBpmDetectionPreferredMaxBpmDefault()),
          m_bFastAnalysisEnabled(m_bpmSettings.getFastAnalysisDefault()),
          m_bReanalyze(m_bpmSettings.getReanalyzeWhenSettingsChangeDefault()),
          m_bReanalyzeImported(m_bpmSettings.getReanalyzeImportedDefault()) {
    setupUi(this);

    m_availablePlugins = AnalyzerBeats::availablePlugins();
    for (const auto& info : qAsConst(m_availablePlugins)) {
        comboBoxBeatPlugin->addItem(info.name(), info.id());
    }

    int bpmRangeIndex = 0;
    for (const auto& range : kPreferredBpmRange) {
        comboBoxBpmRange->addItem(QString("") + QVariant(range.Min).toString() + "-" + QVariant(range.Max).toString(), bpmRangeIndex++);
    }
    comboBoxBpmRange->setCurrentIndex(kPreferredBpmRangeDefaultIndex);

    loadSettings();

    // Connections
    connect(comboBoxBeatPlugin,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefBeats::pluginSelected);
    connect(checkBoxAnalyzerEnabled,
            &QCheckBox::stateChanged,
            this,
            &DlgPrefBeats::analyzerEnabled);
    connect(comboBoxBpmRange,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefBeats::slotPreferredBpmRangeChanged);
    connect(checkBoxFixedTempo,
            &QCheckBox::stateChanged,
            this,
            &DlgPrefBeats::fixedtempoEnabled);
    connect(checkBoxFastAnalysis,
            &QCheckBox::stateChanged,
            this,
            &DlgPrefBeats::fastAnalysisEnabled);
    connect(checkBoxReanalyze,
            &QCheckBox::stateChanged,
            this,
            &DlgPrefBeats::slotReanalyzeChanged);
    connect(checkBoxReanalyzeImported,
            &QCheckBox::stateChanged,
            this,
            &DlgPrefBeats::slotReanalyzeImportedChanged);
}

DlgPrefBeats::~DlgPrefBeats() {
}

QUrl DlgPrefBeats::helpUrl() const {
    return QUrl(MIXXX_MANUAL_BEATS_URL);
}

void DlgPrefBeats::loadSettings() {
    m_selectedAnalyzerId = m_bpmSettings.getBeatPluginId();
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabled();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumption();
    m_iPreferredMinBpm = m_bpmSettings.getBpmDetectionPreferredMinBpm();
    m_iPreferredMaxBpm = m_bpmSettings.getBpmDetectionPreferredMaxBpm();
    m_bReanalyze =  m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysis();

    slotUpdate();
}

void DlgPrefBeats::slotResetToDefaults() {
    if (m_availablePlugins.size() > 0) {
        m_selectedAnalyzerId = m_availablePlugins[0].id();
    }
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabledDefault();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumptionDefault();
    m_iPreferredMinBpm = m_bpmSettings.getBpmDetectionPreferredMinBpmDefault();
    m_iPreferredMaxBpm = m_bpmSettings.getBpmDetectionPreferredMaxBpmDefault();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysisDefault();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChangeDefault();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImportedDefault();

    slotUpdate();
}

void DlgPrefBeats::pluginSelected(int i) {
    if (i == -1) {
        return;
    }
    m_selectedAnalyzerId = m_availablePlugins[i].id();
    slotUpdate();
}

void DlgPrefBeats::analyzerEnabled(int i) {
    m_bAnalyzerEnabled = static_cast<bool>(i);
    slotUpdate();
}

void DlgPrefBeats::slotPreferredBpmRangeChanged(int i) {
    if (i < 0) {
        return;
    }
    const auto& range = kPreferredBpmRange[i];
    m_iPreferredMinBpm = range.Min;
    m_iPreferredMaxBpm = range.Max;
    slotUpdate();
}

void DlgPrefBeats::fixedtempoEnabled(int i) {
    m_bFixedTempoEnabled = static_cast<bool>(i);
    slotUpdate();
}

void DlgPrefBeats::slotUpdate() {
    checkBoxFixedTempo->setEnabled(m_bAnalyzerEnabled);
    comboBoxBeatPlugin->setEnabled(m_bAnalyzerEnabled);
    checkBoxAnalyzerEnabled->setChecked(m_bAnalyzerEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setEnabled(m_bAnalyzerEnabled && m_bFixedTempoEnabled);
    checkBoxReanalyze->setEnabled(m_bAnalyzerEnabled);
    checkBoxReanalyzeImported->setEnabled(m_bAnalyzerEnabled);

    if (!m_bAnalyzerEnabled) {
        return;
    }

    if (m_availablePlugins.size() > 0) {
        bool found = false;
        for (int i = 0; i < m_availablePlugins.size(); ++i) {
            const auto& info = m_availablePlugins.at(i);
            if (info.id() == m_selectedAnalyzerId) {
                found = true;
                comboBoxBeatPlugin->setCurrentIndex(i);
                if (!m_availablePlugins[i].isConstantTempoSupported()) {
                    checkBoxFixedTempo->setEnabled(false);
                }
                break;
            }
        }
        if (!found) {
            comboBoxBeatPlugin->setCurrentIndex(0);
            m_selectedAnalyzerId = m_availablePlugins[0].id();
        }
    }
    {
        bool found = false;
        int bpmRangeIndex = 0;
        for (const auto& range : kPreferredBpmRange) {
            if (range.Min == m_iPreferredMinBpm && range.Max == m_iPreferredMaxBpm) {
                comboBoxBpmRange->setCurrentIndex(bpmRangeIndex);
                found = true;
                break;
            }
            bpmRangeIndex++;
        }
        if (!found) {
            comboBoxBpmRange->setCurrentIndex(kPreferredBpmRangeDefaultIndex);
        }
    }

    checkBoxFixedTempo->setChecked(m_bFixedTempoEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setChecked(m_bFastAnalysisEnabled && m_bFixedTempoEnabled);

    checkBoxReanalyze->setChecked(m_bReanalyze);
    checkBoxReanalyzeImported->setChecked(m_bReanalyzeImported);
}

void DlgPrefBeats::slotReanalyzeChanged(int value) {
    m_bReanalyze = static_cast<bool>(value);
    slotUpdate();
}

void DlgPrefBeats::slotReanalyzeImportedChanged(int value) {
    m_bReanalyzeImported = static_cast<bool>(value);
    slotUpdate();
}

void DlgPrefBeats::fastAnalysisEnabled(int i) {
    m_bFastAnalysisEnabled = static_cast<bool>(i);
    slotUpdate();
}

void DlgPrefBeats::slotApply() {
    m_bpmSettings.setBeatPluginId(m_selectedAnalyzerId);
    m_bpmSettings.setBpmDetectionEnabled(m_bAnalyzerEnabled);
    m_bpmSettings.setFixedTempoAssumption(m_bFixedTempoEnabled);
    m_bpmSettings.setBpmDetectionPreferredMinBpm(m_iPreferredMinBpm);
    m_bpmSettings.setBpmDetectionPreferredMaxBpm(m_iPreferredMaxBpm);
    m_bpmSettings.setReanalyzeWhenSettingsChange(m_bReanalyze);
    m_bpmSettings.setReanalyzeImported(m_bReanalyzeImported);
    m_bpmSettings.setFastAnalysis(m_bFastAnalysisEnabled);
}
