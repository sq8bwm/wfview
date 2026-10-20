#include "CWSenderController.h"
#include "logcategories.h"

namespace
{
// Used when the rig file has no CW Pitch command.
constexpr int defaultPitchMin = 300;
constexpr int defaultPitchMax = 900;
}

CWSenderController::CWSenderController(QObject* parent)
    : QObject(parent)
    , m_visible(false)
    , m_wpm(20)
    , m_dashRatio(3.0)
    , m_pitch(600)
    , m_pitchMin(defaultPitchMin)
    , m_pitchMax(defaultPitchMax)
    , m_breakInMode(0)
    , m_cutNumbers(false)
    , m_sendImmediate(false)
    , m_sidetoneEnable(false)
    , m_sidetoneLevel(50)
    , m_sidetoneAvailable(false)
    , m_sequenceNumber(1)
    , m_maxChars(0)
    , m_macroEditMode(false)
    , m_receiveVisible(true)
    , m_canSendCW(false)
    , m_currentMode(modeUnknown)
    , m_queue(nullptr)
    , m_rigCaps(nullptr)
    , m_rig(nullptr)
    , m_queueSignalsConnected(false)
    , m_tone(std::make_shared<CwSidetoneGenerator>())
{
    m_tone->setSpeed(m_wpm);
    m_tone->setRatio(m_dashRatio);
    m_tone->setFrequency(m_pitch);
    m_tone->setLevel(m_sidetoneLevel);

    m_queue = cachingQueue::getInstance();
    if (m_queue) {
        connect(this, &CWSenderController::sendCW, m_queue, [=](const QString &cwMessage) {
            m_queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(cwMessage)));
        });

        connect(this, &CWSenderController::stopCW, m_queue, [=]() {
            m_queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(QString())));
        });

        connect(this, &CWSenderController::setBreakInModeSignal, m_queue, [=](const quint8 &bmode) {
            m_queue->add(priorityImmediate, queueItem(funcBreakIn, QVariant::fromValue<uchar>(bmode)));
        });

        connect(this, &CWSenderController::setDashRatioSignal, m_queue, [=](const quint8& ratio) {
            m_queue->addUnique(priorityImmediate, queueItem(funcDashRatio, QVariant::fromValue<uchar>(ratio)));
        });

        connect(this, &CWSenderController::setPitchSignal, m_queue, [=](const quint16& pitch) {
            m_queue->addUnique(priorityImmediate, queueItem(funcCwPitch, QVariant::fromValue<ushort>(pitch)));
        });

        connect(this, &CWSenderController::getCWSettings, m_queue, [=]() {
            m_queue->add(priorityImmediate, funcKeySpeed);
            m_queue->add(priorityImmediate, funcBreakIn);
            m_queue->add(priorityImmediate, funcCwPitch);
            m_queue->add(priorityImmediate, funcDashRatio);
        });
        m_queueSignalsConnected = true;
    }

    receiveRigCaps(m_queue ? m_queue->getRigCaps() : nullptr);
}


void CWSenderController::receiveRigCaps(rigCapabilities* caps)
{
    m_rigCaps = caps;
    const bool supported = m_rigCaps && m_rigCaps->commands.contains(funcSendCW);
    if (m_canSendCW != supported) {
        m_canSendCW = supported;
        emit canSendCWChanged();
    }

    // The pitch range comes from the rig file, in Hz.
    int pitchMin = defaultPitchMin;
    int pitchMax = defaultPitchMax;
    if (m_rigCaps && m_rigCaps->commands.contains(funcCwPitch)) {
        const funcType pitchCmd = m_rigCaps->commands.value(funcCwPitch);
        if (pitchCmd.maxVal > pitchCmd.minVal) {
            pitchMin = pitchCmd.minVal;
            pitchMax = pitchCmd.maxVal;
        }
    }
    if (m_pitchMin != pitchMin || m_pitchMax != pitchMax) {
        m_pitchMin = pitchMin;
        m_pitchMax = pitchMax;
        emit pitchRangeChanged();
    }

    const int chars = supported ? m_rigCaps->commands.find(funcSendCW).value().bytes : 0;
    if (m_maxChars != chars) {
        m_maxChars = chars;
        emit maxCharsChanged();
    }

    if (!supported)
        setStatusMessage("CW sending is not supported by the current radio.");
    else if (m_statusMessage == "CW sending is not supported by the current radio.")
        setStatusMessage(QString());
}

CWSenderController::~CWSenderController()
{
    qDebug(logCW()) << "Running CW Sender Controller destructor.";
    m_tone->stop();
}

// Property setters
void CWSenderController::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        emit visibleChanged();
        if (m_visible)
            emit getCWSettings();
    }
}

void CWSenderController::setWpm(int wpm)
{
    if (m_wpm != wpm) {
        m_wpm = wpm;
        m_queue->addUnique(priorityImmediate, queueItem(funcKeySpeed, QVariant::fromValue<ushort>(wpm)));
        m_tone->setSpeed(wpm);
        emit wpmChanged();
    }
}

void CWSenderController::setDashRatio(double ratio)
{
    if (m_dashRatio != ratio) {
        m_dashRatio = ratio;
        // Round rather than truncate: 2.9 * 10 is 28.999... in floating point.
        const quint8 ratioInt = static_cast<quint8>(qRound(ratio * 10));
        emit setDashRatioSignal(ratioInt);
        m_tone->setRatio(ratioInt / 10.0);
        emit dashRatioChanged();
    }
}

void CWSenderController::setPitch(int pitch)
{
    if (m_pitch != pitch) {
        m_pitch = pitch;
        emit setPitchSignal(static_cast<quint16>(pitch));
        m_tone->setFrequency(pitch);
        emit pitchChanged();
    }
}

void CWSenderController::setBreakInMode(int mode)
{
    if (m_breakInMode != mode && mode >= 0 && mode < 3) {
        m_breakInMode = mode;
        emit setBreakInModeSignal(static_cast<quint8>(mode));
        emit breakInModeChanged();
    }
}

void CWSenderController::setCutNumbers(bool val)
{
    if (m_cutNumbers != val) {
        m_cutNumbers = val;
        emit cutNumbersChanged();
        emit keyerSettingsChanged();
    }
}

void CWSenderController::setSendImmediate(bool val)
{
    if (m_sendImmediate != val) {
        m_sendImmediate = val;
        emit sendImmediateChanged();
        emit keyerSettingsChanged();
    }
}

void CWSenderController::setSidetoneEnable(bool val)
{
    if (m_sidetoneEnable != val) {
        m_sidetoneEnable = val;
        if (!val)
            m_tone->stop();
        emit sidetoneEnableChanged();
        emit keyerSettingsChanged();
    }
}

void CWSenderController::setSidetoneLevel(int val)
{
    if (m_sidetoneLevel != val) {
        m_sidetoneLevel = val;
        m_tone->setLevel(val);
        emit sidetoneLevelChanged();
        emit keyerSettingsChanged();
    }
}

void CWSenderController::setSidetoneAvailable(bool available)
{
    if (m_sidetoneAvailable != available) {
        m_sidetoneAvailable = available;
        if (!available)
            m_tone->stop();
        emit sidetoneAvailableChanged();
    }
}

void CWSenderController::setSequenceNumber(int val)
{
    if (m_sequenceNumber != val) {
        m_sequenceNumber = val;
        emit sequenceNumberChanged();
    }
}

void CWSenderController::setMacroEditMode(bool val)
{
    if (m_macroEditMode != val) {
        m_macroEditMode = val;
        emit macroEditModeChanged();
    }
}

void CWSenderController::setReceiveVisible(bool val)
{
    if (m_receiveVisible != val) {
        m_receiveVisible = val;
        emit receiveVisibleChanged();
    }
}

void CWSenderController::loadSettings(bool cutNumbers, bool sendImmediate, bool sidetoneEnabled, int sidetoneLevel, const QStringList& macros)
{
    m_cutNumbers = cutNumbers;
    m_sendImmediate = sendImmediate;
    m_sidetoneEnable = sidetoneEnabled;
    m_sidetoneLevel = sidetoneLevel;

    if (macros.length() == 10) {
        for (int i = 0; i < 10; ++i)
            m_macroText[i + 1] = macros.at(i);
    }

    m_tone->setLevel(m_sidetoneLevel);
    if (!m_sidetoneEnable)
        m_tone->stop();

    emit cutNumbersChanged();
    emit sendImmediateChanged();
    emit sidetoneEnableChanged();
    emit sidetoneLevelChanged();
    for (int i = 1; i <= 10; ++i)
        emit macroButtonTextChanged(i, getMacroButtonText(i));
}

// Macro management
QStringList CWSenderController::getMacroText() const
{
    QStringList mlist;
    for(int i = 1; i < 11; i++) {
        mlist << m_macroText[i];
    }
    return mlist;
}

void CWSenderController::setMacroText(const QStringList& macros)
{
    if(macros.length() != 10) {
        qCritical(logCW()) << "Macro list must be exactly 10. Rejecting macro text load.";
        return;
    }
    
    for(int i = 0; i < 10; i++) {
        m_macroText[i + 1] = macros.at(i);
    }
    
    // Update button texts
    for(int i = 1; i <= 10; i++) {
        emit macroButtonTextChanged(i, getMacroButtonText(i));
    }
    emit keyerSettingsChanged();
}

QString CWSenderController::getMacro(int index) const
{
    if(index >= 1 && index <= 10) {
        return m_macroText[index];
    }
    return QString();
}

QString CWSenderController::getMacroButtonText(int index) const
{
    if(index < 1 || index > 10 || m_macroText[index].isEmpty()) {
        return QString();
    }
    
    const QString btnText = m_macroText[index];
    if(btnText.length() <= 8)
        return btnText;

    return btnText.left(7) + "...";
}

// Invokable methods
void CWSenderController::sendText(const QString& text)
{
    if(text.isEmpty() || !m_canSendCW || text.length() > m_maxChars) {
        return;
    }
    
    if(m_currentMode != modeCW && m_currentMode != modeCW_R) {
        setStatusMessage("Note: Mode needs to be set to CW or CW-R to send CW.");
        return;
    }
    
    emit transcriptTextAppended(text.toUpper() + "\n");
    emit clearTextInput();
    setStatusMessage("Sending CW");
    emit sendCW(text);
    handleSidetoneText(text);
}

void CWSenderController::stopSending()
{
    emit stopCW();
    handleStopSidetone();
    setStatusMessage("Stopping CW transmission.");
}

void CWSenderController::runMacro(int macroNumber)
{
    if(macroNumber < 1 || macroNumber > 10 || m_macroText[macroNumber].isEmpty() || !m_canSendCW) {
        return;
    }
    
    if(m_currentMode != modeCW && m_currentMode != modeCW_R) {
        setStatusMessage("Note: Mode needs to be set to CW or CW-R to send CW.");
        return;
    }
    
    QString outText = processMacroText(m_macroText[macroNumber]);
    
    if (m_cutNumbers) {
        outText.replace("0", "T");
        outText.replace("9", "N");
    }
    
    emit transcriptTextAppended(outText.toUpper() + "\n");
    setStatusMessage(QString("Sending CW macro %1").arg(macroNumber));
    
    // Send in chunks if needed
    for (int i = 0; i < outText.size(); i += m_maxChars) {
        const QString chunk = outText.mid(i, m_maxChars);
        emit sendCW(chunk);
        handleSidetoneText(chunk);
    }
}

void CWSenderController::editMacro(int macroNumber, const QString& newText)
{
    if(macroNumber < 1 || macroNumber > 10) {
        return;
    }
    
    if(newText.length() > 60) {
        setStatusMessage(QString("Macro text too long (max 60 chars, got %1)").arg(newText.length()));
        return;
    }
    
    m_macroText[macroNumber] = newText.toUpper();
    emit macroButtonTextChanged(macroNumber, getMacroButtonText(macroNumber));
    emit keyerSettingsChanged();
}

void CWSenderController::textChanged(const QString& text)
{
    updateTextColor(text);
    
    if (m_sendImmediate && !text.isEmpty() && text.back() == ' ') {
        if(m_currentMode != modeCW && m_currentMode != modeCW_R) {
            setStatusMessage("Note: Mode needs to be set to CW or CW-R to send CW.");
        } else {
            QString toSend = text.mid(0, m_maxChars);
            if (!toSend.isEmpty()) {
                emit clearTextInput();
                emit transcriptTextAppended(toSend.toUpper());
                emit sendCW(toSend);
                handleSidetoneText(toSend);
            }
        }
    }
}

void CWSenderController::appendReceiveText(const QString& text)
{
    emit receiveTextAppended(text);
}

void CWSenderController::appendTranscriptText(const QString& text)
{
    emit transcriptTextAppended(text);
}

// Handlers from rig
void CWSenderController::handleKeySpeed(quint8 wpm)
{
    if (wpm != m_wpm && wpm >= 6 && wpm <= 48) {
        m_wpm = wpm;
        m_tone->setSpeed(wpm);
        emit wpmChanged();
    }
}

void CWSenderController::handleDashRatio(quint8 ratio)
{
    double calc = double(ratio) / 10.0;
    if (calc != m_dashRatio && calc >= 2.5 && calc <= 4.5) {
        m_dashRatio = calc;
        m_tone->setRatio(calc);
        emit dashRatioChanged();
    }
}

void CWSenderController::handlePitch(quint16 cwPitch)
{
    if (cwPitch != m_pitch && cwPitch >= m_pitchMin && cwPitch <= m_pitchMax) {
        m_pitch = cwPitch;
        m_tone->setFrequency(cwPitch);
        emit pitchChanged();
    }
}

void CWSenderController::handleBreakInMode(quint8 b)
{
    if(b < 3 && b != m_breakInMode) {
        m_breakInMode = b;
        emit breakInModeChanged();
    }
}

void CWSenderController::handleCurrentModeUpdate(rigMode_t mode)
{
    if (mode != modeUnknown) {
        m_currentMode = mode;
        if(m_currentMode != modeCW && m_currentMode != modeCW_R) {
            setStatusMessage("Note: Mode needs to be set to CW or CW-R to send CW.");
        } else if (m_statusMessage.startsWith("Note: Mode needs")) {
            setStatusMessage(QString());
        }
    }
}

void CWSenderController::handleSidetoneText(QString text)
{
    if (!m_sidetoneEnable || !m_sidetoneAvailable || text.isEmpty())
        return;

    m_tone->send(text);
}

void CWSenderController::handleStopSidetone()
{
    m_tone->stop();
}

void CWSenderController::receive(QString text)
{
    emit receiveTextAppended(text);
}

void CWSenderController::receiveEnabled(bool en)
{
    setReceiveVisible(en);
}

// Private methods
void CWSenderController::setStatusMessage(const QString& msg)
{
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void CWSenderController::updateTextColor(const QString& text)
{
    QString color;
    if (m_maxChars > 0 && text.length() > m_maxChars) {
        color = "red";
    } else if (m_maxChars > 0 && text.length() > m_maxChars / 1.2) {
        color = "yellow";
    } else {
        color = "";
    }
    emit textColorChanged(color);
}

QString CWSenderController::processMacroText(const QString& macroText)
{
    QString outText = macroText;
    
    if(macroText.contains("%1")) {
        outText = macroText.arg(m_sequenceNumber, 3, 10, QChar('0'));
        m_sequenceNumber++;
        emit sequenceNumberChanged();
    }
    
    return outText;
}
